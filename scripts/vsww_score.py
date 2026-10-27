#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
"""Scores a recording with vsWakeWord, Kiosk Satellite's wake word engine,
for scripts/audio_test.py. It runs with the vs-wake-word repository's own
Python environment and uses that repository's benchmark runner, so the
features, the CTC decoding, the stream matcher, the cooldown and the
high-confidence bypass are the engine's own, from the model's manifest.

  vsww_score.py <vs-wake-word repo> <model folder> <16 kHz mono s16 file>

Prints JSON: the detection times in seconds and, for every 80 ms step where
the matcher fired, its time and matched confidence.
"""

import json
import sys
from pathlib import Path

import numpy as np


def main():
    repo, model_dir, audio_path = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
    sys.path.insert(0, str(repo))
    sys.path.insert(0, str(repo / "scripts" / "bench"))
    import benchmark_standard as b
    from wakeword.ctc_train import greedy_ctc_decode_with_confidence
    from wakeword.features import normalize_audio

    name = model_dir.name
    manifest = model_dir / f"{name}.json"
    runner = b.VwwRunner(model_dir / f"{name}.onnx", manifest, require_cuda=False)
    rt = json.loads(manifest.read_text(encoding="utf-8-sig")).get("runtime", {})
    runner.stream_match = bool(rt.get("stream_match", False))
    runner.stream_buffer_ms = int(rt.get("stream_buffer_ms", 2500))
    runner.stream_lag_frames = int(rt.get("stream_lag_frames", 20))
    blank = int(runner.manifest["ctc"].get("blank_id", 1))

    audio = normalize_audio(np.fromfile(audio_path, dtype=np.int16).astype(np.float32) / 32768)
    win = runner.cfg.window_samples
    if len(audio) < win:
        audio = np.pad(audio, (0, win - len(audio)))
    starts = list(range(0, len(audio) - win + 1, b.HOP_SAMPLES))
    feats = []
    for c0 in range(0, len(starts), 2048):
        clips = [audio[s:s + win] for s in starts[c0:c0 + 2048]]
        if runner.batch_extractor is not None:
            feats.append(runner.batch_extractor.extract_many(clips))
        else:
            feats.append(np.asarray([runner.extractor.extract(c) for c in clips], dtype=np.float32))
    lp_batch = runner._infer_logits(np.concatenate(feats).astype(np.float32))

    # The stream matcher of benchmark_standard.VwwRunner._run_stream_match,
    # which is what the engine runs live, with the time of each step kept.
    t_out = lp_batch.shape[1]
    frames_per_hop = max(1, round(t_out * b.HOP_MS / runner.cfg.window_ms))
    lag = min(runner.stream_lag_frames, t_out - frames_per_hop - 1)
    frame_ms = runner.cfg.window_ms / t_out
    buf_frames = max(t_out, int(runner.stream_buffer_ms / frame_ms))
    pieces = [lp_batch[0][: t_out - lag]]
    for w in range(1, len(lp_batch)):
        pieces.append(lp_batch[w][t_out - lag - frames_per_hop: t_out - lag])
    stream = np.concatenate(pieces, axis=0)

    detections, fired_steps = [], []
    hit_count, hit_group, hit_required, hit_bypass_min, bypass_consec = 0, -1, 0, 0, 0
    last_detect = -10_000
    for step in range(len(lp_batch)):
        end = min(len(stream), (t_out - lag) + step * frames_per_hop)
        start = max(0, end - buf_frames)
        t = (starts[step] + win) / b.SR - lag * frame_ms / 1000
        ids, confs = greedy_ctc_decode_with_confidence(stream[start:end])
        fired, conf, target, group = runner.matcher_fires(ids, confs)
        base_required = runner.required_hits_for_target(target)
        base_bypass_min = runner.bypass_min_hits_for_target(target, base_required)
        if fired:
            fired_steps.append([round(t, 2), round(float(conf), 2)])
            same = group >= 0 and hit_group == group
            if not same:
                hit_count, bypass_consec, hit_required, hit_bypass_min = 0, 0, 0, 0
            required = max(base_required, hit_required) if same and hit_required else base_required
            bypass_min = max(base_bypass_min, hit_bypass_min) if same and hit_bypass_min else base_bypass_min
            hit_group, hit_required, hit_bypass_min = group, required, bypass_min
            hit_count += 1
            bypass_consec = bypass_consec + 1 if runner.bypass is not None and conf >= runner.bypass else 0
            bypassed = runner.bypass is not None and bypass_consec >= bypass_min
            if (bypassed or hit_count >= required) and step - last_detect >= runner.cooldown_windows:
                detections.append(round(t, 2))
                last_detect = step
                hit_count, hit_group, hit_required, hit_bypass_min, bypass_consec = 0, -1, 0, 0, 0
                stream[start:end] = 0.0
                stream[start:end, blank] = 1.0
        elif runner.hit_mode == "consecutive":
            hit_count, hit_group, hit_required, hit_bypass_min = 0, -1, 0, 0
            bypass_consec = 0
        else:
            bypass_consec = 0
    print(json.dumps({"detections": detections, "fired": fired_steps}))


if __name__ == "__main__":
    main()
