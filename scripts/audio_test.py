#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
"""The standard wake word and voice test for an Echo. Run it through
scripts/audio-test.sh, which sets up its environment. .internal/docs/audio-testing.md
describes the procedure and what each number means.

capture records the corpus once per Echo: the wake word in a quiet room,
said by a person or, with --mouth, played by a second Echo standing where a
person would from vsWakeWord's held-out test voices, the test track through the speaker and that person's voice
through the speaker in place of the assistant. Each is the raw microphone
stream, before any processing, from the dump in the image's microphone
processing.

run mixes those recordings digitally (sound adds at the microphones, so a
voice recorded in a quiet room plus music recorded in silence is the voice
over that music), replays every mix through the image's processing on the
Echo with its offline tool and scores what comes out with vsWakeWord, Kiosk
Satellite's own wake word engine (scripts/vsww_score.py, with the
vs-wake-word repository's environment and the model's own runtime gates), or
with the okay_nabu microWakeWord model when that repository is not there.

calibrate writes the speaker null calibration for an Echo Show model from the
music recording of a corpus (common/audio/amznaec/speaker_null.h).

live checks what only the running Echo can show: that Android hands an app
the processing's output untouched, that the level comes back after a loud
sound, that the HAL reads the microphones on time and that Wi-Fi stays put.
"""

import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys
import threading
import time

import numpy as np

RATE = 16000
DEV_DIR = "/data/local/tmp/audio-test"
KS_PACKAGE = "me.jxl.kiosk_satellite"

DEVICES = {
    "crown": {"name": "Echo Show 8 (1st gen)", "kind": "show", "channels": 6, "mics": 4},
    "cronos": {"name": "Echo Show 5 (2nd gen)", "kind": "show", "channels": 4, "mics": 2},
    "biscuit": {"name": "Echo Dot (2nd gen)", "kind": "dot", "channels": 9, "mics": 7},
}

# The minimum every release must reach, from the Echo Show 8 tuning of
# 2026-10-26 (.internal/docs/audio-testing.md). "min" fails below, "max" fails above.
MVP = {
    "quiet.wake_rate": ("min", 0.9),
    "quiet.false_triggers": ("max", 0),
    "quiet.floor_cut_db": ("max", 12.0),
    "quiet.weak_speech_loss_db": ("max", 3.0),
    "music.wake_rate": ("min", 0.8),
    "music.loud_wake_rate": ("min", 0.6),
    "music.false_triggers": ("max", 0),
    "music_only.false_triggers": ("max", 0),
    "assistant.self_triggers": ("max", 0),
    "assistant.leak_frames": ("max", 0.10),
    "barge_in.wake_rate": ("min", 0.5),
    "live.path_correlation": ("min", 0.99),
    "live.level_change_after_loud_db": ("max", 3.0),
    "live.hal_read_gap_ms": ("max", 100.0),
    "live.wifi_reassociations": ("max", 0),
}

MUSIC_LEAD_S = 10.0     # music alone before the first wake word, for the canceller to adapt
ASSISTANT_LEAD_S = 5.0  # the same for the assistant's voice
PROMPT_EVERY_S = 3.5
LEAK_MARGIN_DB = 25.0   # leaked assistant voice within this of the user's voice counts


# --- adb ---------------------------------------------------------------------

class Echo:
    def __init__(self, serial):
        self.serial = serial

    def sh(self, cmd, check=True, timeout=600):
        r = subprocess.run(["adb", "-s", self.serial, "shell", cmd], capture_output=True,
                           text=True, timeout=timeout)
        if check and r.returncode != 0:
            raise SystemExit(f"ERROR: on the Echo: {cmd}\n{r.stdout}{r.stderr}")
        return r.stdout.strip()

    def push(self, local, remote):
        subprocess.run(["adb", "-s", self.serial, "push", local, remote], check=True,
                       capture_output=True)

    def pull(self, remote, local):
        subprocess.run(["adb", "-s", self.serial, "pull", remote, local], check=True,
                       capture_output=True)

    def prop(self, name):
        return self.sh(f"getprop {name}", check=False)


def connect(args):
    echo = Echo(args.serial)
    if echo.sh("id -u", check=False) != "0":
        subprocess.run(["adb", "-s", args.serial, "root"], capture_output=True)
        subprocess.run(["adb", "-s", args.serial, "wait-for-device"], capture_output=True)
        time.sleep(2)
        if echo.sh("id -u", check=False) != "0":
            raise SystemExit("ERROR: the test needs adb root on the Echo")
    device = echo.prop("ro.product.device")
    if device not in DEVICES:
        raise SystemExit(f"ERROR: {device} is not an Echo this test knows")
    return echo, device, DEVICES[device]


def find_tool(args, device, name):
    out = os.path.join(args.workspace, "lineage-18.1", "out", "target", "product", device)
    for sub in ("system/bin", "system/vendor/bin", "vendor/bin"):
        path = os.path.join(out, sub, name)
        if os.path.exists(path):
            return path
    raise SystemExit(f"ERROR: build {name} first: BUILD_TARGET=\"{name}\" make {device}")


def install_tools(args, echo, device, profile):
    tools = ["echo_audio_harness"]
    tools.append("amznaec_offline" if profile["kind"] == "show" else "mic_beam_offline")
    echo.sh(f"mkdir -p {DEV_DIR}")
    for tool in tools:
        echo.push(find_tool(args, device, tool), f"{DEV_DIR}/{tool}")
    echo.sh(f"chmod 755 {DEV_DIR}/*; chmod 777 {DEV_DIR}")


def ks_uid(echo):
    out = echo.sh(f"dumpsys package {KS_PACKAGE} | grep -m1 userId=", check=False)
    m = re.search(r"userId=(\d+)", out)
    if not m:
        raise SystemExit("ERROR: Kiosk Satellite is not installed")
    return int(m.group(1))


def mic_in_use(echo):
    out = echo.sh("dumpsys media.audio_flinger | grep -A3 '^Input thread'", check=False)
    return "Standby: no" in out


def wait_for_free_mic(echo):
    if not mic_in_use(echo):
        return
    print("Turn off Voice Satellite in Kiosk Satellite to free the microphones. Waiting...")
    while mic_in_use(echo):
        time.sleep(1)
    time.sleep(1)


def dump_prop(profile):
    return "persist.vendor.amznaec.dump" if profile["kind"] == "show" else "persist.biscuit.mic_beam_dump"


def volume(echo):
    out = echo.sh("dumpsys audio | grep -A8 '^- STREAM_MUSIC:'", check=False)
    cur = re.search(r"Current:.*?2 \(speaker\): (\d+)", out)
    top = re.search(r"Max: (\d+)", out)
    return (int(cur.group(1)) if cur else None, int(top.group(1)) if top else 15)


def set_volume(echo, index):
    echo.sh(f"cmd media_session volume --stream 3 --set {index} >/dev/null 2>&1", check=False)


def record(echo, profile, uid, name, seconds, play=None, play_after=1.0):
    """Records the raw microphone stream through the dump while the harness
    holds the microphones as Kiosk Satellite would, playing a file if asked.
    Returns the device path of the dump."""
    prefix = f"{DEV_DIR}/{name}"
    echo.sh(f"rm -f {prefix}.in.raw {prefix}.out.raw; setprop {dump_prop(profile)} {prefix}")
    try:
        env = f"ECHO_HARNESS_PLAY={play} " if play else ""
        if play:
            timing = f"{play_after} {seconds} 1"
        else:
            timing = f"{seconds} 0 0"
        echo.sh(f"{env}{DEV_DIR}/echo_audio_harness {timing} -12 {uid} {KS_PACKAGE} >/dev/null 2>&1",
                timeout=seconds + 120)
    finally:
        echo.sh(f"setprop {dump_prop(profile)} ''", check=False)
    return f"{prefix}.in.raw"


# --- audio -------------------------------------------------------------------

def read_s24(path, channels):
    b = np.fromfile(path, dtype=np.uint8)
    n = len(b) // (3 * channels)
    b = b[: n * 3 * channels].reshape(n, channels, 3).astype(np.int32)
    v = b[:, :, 0] | (b[:, :, 1] << 8) | (b[:, :, 2] << 16)
    v = np.where(v >= 1 << 23, v - (1 << 24), v)
    return v / float(1 << 23)


def write_s24(path, x):
    v = np.clip(np.round(x * (1 << 23)), -(1 << 23), (1 << 23) - 1).astype(np.int32)
    b = np.stack([v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF], axis=-1).astype(np.uint8)
    b.tofile(path)


def db(x):
    return 10 * np.log10(np.mean(np.square(x)) + 1e-20)


def frames_db(x, size=320):
    n = len(x) // size
    f = x[: n * size].reshape(n, size)
    return 10 * np.log10(np.mean(np.square(f), axis=1) + 1e-20)


def ref_channels(profile):
    if profile["kind"] == "show":
        return [profile["mics"], profile["mics"] + 1]
    return [8]


def ref_signal(x, profile):
    return np.mean(x[:, ref_channels(profile)], axis=1)


def find_utterances(mic, expected):
    """Finds the spoken wake words in a quiet recording, as (start, end) in
    seconds."""
    e = frames_db(mic)
    floor = np.percentile(e, 20)
    speech = e > floor + 12
    segments, start, last = [], None, -100
    for i, s in enumerate(speech):
        if s:
            if start is None or i - last > 15:  # gaps under 300 ms join
                if start is not None:
                    segments.append((start, last))
                start = i
            last = i
    if start is not None:
        segments.append((start, last))
    segments = [(a * 0.02, (b + 1) * 0.02) for a, b in segments if (b - a + 1) >= 12]
    if len(segments) != expected:
        print(f"WARNING: found {len(segments)} wake words in the recording, expected {expected}")
    return segments


# --- capture -----------------------------------------------------------------

def capture(args):
    echo, device, profile = connect(args)
    corpus = os.path.join(args.workspace, "audio-test", device, "corpus")
    os.makedirs(corpus, exist_ok=True)
    meta_path = os.path.join(corpus, "meta.json")
    meta = json.load(open(meta_path)) if os.path.exists(meta_path) else {}
    install_tools(args, echo, device, profile)
    uid = ks_uid(echo)
    wait_for_free_mic(echo)
    parts = args.only.split(",") if args.only else ["voice", "music", "assistant"]

    old_volume, top = volume(echo)
    index = args.volume if args.volume is not None else int(
        echo.prop("ro.config.media_vol_default") or round(top * 0.7))
    meta.update({
        "device": device, "name": profile["name"], "channels": profile["channels"],
        "build": echo.prop("ro.build.date"), "volume_index": index, "volume_max": top,
    })
    try:
        if "voice" in parts:
            if args.mouth:
                capture_mouth(args, echo, profile, uid, corpus, meta)
            else:
                capture_voice(echo, profile, uid, corpus, meta, args.utterances)
        set_volume(echo, index)
        if "music" in parts:
            capture_music(args, echo, profile, uid, corpus, meta)
        if "assistant" in parts:
            capture_assistant(echo, profile, uid, corpus, meta)
    finally:
        if old_volume is not None:
            set_volume(echo, old_volume)
    meta["captured"] = datetime.datetime.now().isoformat(timespec="seconds")
    json.dump(meta, open(meta_path, "w"), indent=2)
    print(f"Corpus saved in {corpus}. You can turn Voice Satellite back on.")


def capture_voice(echo, profile, uid, corpus, meta, count):
    seconds = 4 + count * PROMPT_EVERY_S + 3
    print(f"\nStand where you usually talk to the Echo. When prompted, say \"Okay Nabu\" "
          f"in your normal voice, {count} times.")
    input("Press Enter when ready...")
    done = threading.Event()

    def run():
        record(echo, profile, uid, "voice", seconds)
        done.set()

    threading.Thread(target=run, daemon=True).start()
    time.sleep(4)
    for i in range(count):
        print(f"  ({i + 1}/{count}) Say \"Okay Nabu\" now", flush=True)
        time.sleep(PROMPT_EVERY_S)
    print("  Stay quiet...")
    done.wait()
    echo.pull(f"{DEV_DIR}/voice.in.raw", os.path.join(corpus, "voice.raw"))
    x = read_s24(os.path.join(corpus, "voice.raw"), profile["channels"])
    meta["utterances"] = find_utterances(x[:, 0], count)
    print(f"  Recorded {len(meta['utterances'])} wake words.")


def mouth_clips(voices):
    """The held-out test voices: the clean set, then five of each language."""
    sets = [("tts-positive-heldout-clean", 20)] + [
        (f"tts-positive-{lang}-heldout", 5) for lang in ("de", "es", "fr", "pt")]
    clips = []
    for name, count in sets:
        folder = os.path.join(voices, name)
        files = sorted(os.path.join(dp, f) for dp, _, fs in os.walk(folder) for f in fs if f.endswith(".wav"))
        if len(files) < count:
            raise SystemExit(f"ERROR: {folder} has {len(files)} clips, the test needs {count}")
        clips += files[:count]
    return clips


def to_48k_stereo(x16):
    spec = np.fft.rfft(x16)
    up = np.fft.irfft(np.concatenate([spec, np.zeros(len(spec) * 2 - 2)]), len(x16) * 3) * 3
    s = (np.clip(up, -1, 1) * 32767).astype(np.int16)
    return np.stack([s, s], 1).reshape(-1)


def read_wav16(path):
    import wave
    with wave.open(path, "rb") as w:
        if w.getframerate() != RATE or w.getsampwidth() != 2:
            raise SystemExit(f"ERROR: {path} is not 16 kHz 16 bit")
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768
        return x.reshape(-1, w.getnchannels())[:, 0]


def mouth_program(files):
    """The clips one after the other, PROMPT_EVERY_S apart after 4 s, and
    where each one's speech is, in seconds."""
    n = int((4 + len(files) * PROMPT_EVERY_S + 3) * RATE)
    x, spans = np.zeros(n), []
    for i, f in enumerate(files):
        c = read_wav16(f)
        c = c / (np.max(np.abs(c)) + 1e-9) * 0.5
        a = int((4 + i * PROMPT_EVERY_S) * RATE)
        x[a:a + len(c)] += c[: n - a]
        e = frames_db(c)
        speech = np.nonzero(e > np.max(e) - 30)[0]
        spans.append(((a + speech[0] * 320) / RATE, (a + (speech[-1] + 1) * 320) / RATE))
    return x, spans


def mouth_utterances(mic, files):
    """Where each clip landed in the recording: the program's spans, moved by
    the lag that lines the program up with what the microphone heard."""
    x, spans = mouth_program(files)
    env = lambda v: np.repeat(np.sqrt(np.maximum(10 ** (frames_db(v) / 10), 1e-12)), 1)
    a, b = env(x), env(mic)
    n = 1 << int(np.ceil(np.log2(len(a) + len(b))))
    xc = np.fft.irfft(np.fft.rfft(b - b.mean(), n) * np.conj(np.fft.rfft(a - a.mean(), n)), n)[: len(b)]
    lag = int(np.argmax(xc)) * 320 / RATE
    return [(round(s + lag, 2), round(e + lag, 2)) for s, e in spans]


def mouth_play(mouth, pcm, seconds):
    mouth.sh(f"ECHO_HARNESS_PLAY_ONLY=1 ECHO_HARNESS_PLAY={pcm} {DEV_DIR}/echo_audio_harness 0 {seconds} 0 "
             ">/dev/null 2>&1", timeout=seconds + 60)


def capture_mouth(args, echo, profile, uid, corpus, meta):
    """The wake word from vsWakeWord's held-out voices, played by a second
    Echo standing where a person would, at a person's level."""
    mouth = Echo(args.mouth)
    if mouth.sh("id -u", check=False) != "0":
        subprocess.run(["adb", "-s", args.mouth, "root"], capture_output=True)
        time.sleep(3)
        subprocess.run(["adb", "connect", args.mouth], capture_output=True)
    mouth.sh(f"mkdir -p {DEV_DIR}")
    mouth.push(find_tool(args, echo.prop("ro.product.device"), "echo_audio_harness"), f"{DEV_DIR}/echo_audio_harness")
    mouth.sh(f"chmod 755 {DEV_DIR}/echo_audio_harness")
    clips = mouth_clips(args.voices)
    work = os.path.join(corpus, "..", "work")
    os.makedirs(work, exist_ok=True)

    def program(files, path):
        x, spans = mouth_program(files)
        to_48k_stereo(x).tofile(path)
        return len(x) / RATE

    def level_at(volume):
        set_volume(mouth, volume)
        seconds = program(clips[:3], os.path.join(work, "mouth-probe.s16"))
        mouth.push(os.path.join(work, "mouth-probe.s16"), f"{DEV_DIR}/mouth-probe.s16")
        t = threading.Thread(target=lambda: record(echo, profile, uid, "probe", seconds + 2))
        t.start()
        time.sleep(1.5)
        mouth_play(mouth, f"{DEV_DIR}/mouth-probe.s16", seconds)
        t.join()
        echo.pull(f"{DEV_DIR}/probe.in.raw", os.path.join(work, "probe.raw"))
        return float(np.percentile(frames_db(read_s24(os.path.join(work, "probe.raw"), profile["channels"])[:, 0]), 99))

    old, _ = volume(mouth)
    try:
        if args.mouth_volume is not None:
            index = args.mouth_volume
        else:
            # A person's level at the Echo: the person recorded on this Echo,
            # or a voice across a room (p99 of 20 ms frames at microphone 0).
            target = args.voice_level
            lo, hi = level_at(6), level_at(12)
            slope = (hi - lo) / 6 if hi > lo else 2.0
            index = int(np.clip(round(6 + (target - lo) / slope), 1, 15))
            print(f"  The mouth arrives at {lo:.1f} dBFS at volume 6 and {hi:.1f} at 12. "
                  f"Using {index} for {target:.1f}.")
        set_volume(mouth, index)
        seconds = program(clips, os.path.join(work, "mouth.s16"))
        mouth.push(os.path.join(work, "mouth.s16"), f"{DEV_DIR}/mouth.s16")
        print(f"\nPlaying {len(clips)} wake words from the second Echo ({seconds:.0f} s). Stay quiet...")
        t = threading.Thread(target=lambda: record(echo, profile, uid, "voice", seconds + 2))
        t.start()
        time.sleep(1.5)
        mouth_play(mouth, f"{DEV_DIR}/mouth.s16", seconds)
        t.join()
    finally:
        if old is not None:
            set_volume(mouth, old)
    echo.pull(f"{DEV_DIR}/voice.in.raw", os.path.join(corpus, "voice.raw"))
    x = read_s24(os.path.join(corpus, "voice.raw"), profile["channels"])
    meta["utterances"] = mouth_utterances(x[:, 0], clips)
    meta["voices"] = {"mouth": args.mouth, "mouth_volume": index,
                      "clips": [os.path.relpath(c, args.voices) for c in clips]}
    print(f"  Recorded {len(meta['utterances'])} wake words.")


def capture_music(args, echo, profile, uid, corpus, meta):
    track = args.music
    if not os.path.exists(track):
        raise SystemExit(f"ERROR: no test track at {track} (see .internal/docs/audio-testing.md)")
    pcm = os.path.join(corpus, "track.s16")
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", str(args.music_start), "-to",
                    str(args.music_end), "-i", track, "-ac", "2", "-ar", "48000", "-f", "s16le",
                    pcm], check=True)
    seconds = args.music_end - args.music_start
    print(f"\nPlaying the test track for {seconds:.0f} s. Stay quiet...")
    echo.push(pcm, f"{DEV_DIR}/track.s16")
    record(echo, profile, uid, "music", seconds, play=f"{DEV_DIR}/track.s16")
    echo.pull(f"{DEV_DIR}/music.in.raw", os.path.join(corpus, "music.raw"))
    meta["track"] = {"file": os.path.basename(track),
                     "sha256": hashlib.sha256(open(track, "rb").read()).hexdigest(),
                     "start": args.music_start, "end": args.music_end}


def capture_assistant(echo, profile, uid, corpus, meta):
    # The person's own wake words, back to back, stand in for the assistant.
    x = read_s24(os.path.join(corpus, "voice.raw"), profile["channels"])[:, 0]
    x = x - np.mean(x)
    e = frames_db(x)
    keep = np.concatenate([x[i * 320:(i + 1) * 320] for i in np.nonzero(e > np.percentile(e, 20) + 6)[0]])
    keep = np.tile(keep, int(np.ceil(30 * RATE / len(keep))))[: 30 * RATE]
    keep = keep / np.max(np.abs(keep)) * 0.7
    spec = np.fft.rfft(keep)
    up = np.fft.irfft(np.concatenate([spec, np.zeros(len(spec) * 2 - 2)]), len(keep) * 3) * 3
    s = (np.clip(up, -1, 1) * 32767).astype(np.int16)
    pcm = os.path.join(corpus, "assistant.s16")
    np.stack([s, s], 1).reshape(-1).tofile(pcm)
    print("\nPlaying your voice in place of the assistant for 30 s. Stay quiet...")
    echo.push(pcm, f"{DEV_DIR}/assistant.s16")
    record(echo, profile, uid, "assistant", 30, play=f"{DEV_DIR}/assistant.s16")
    echo.pull(f"{DEV_DIR}/assistant.in.raw", os.path.join(corpus, "assistant.raw"))


# --- run ---------------------------------------------------------------------

def onset(x, profile):
    e = frames_db(ref_signal(x, profile))
    active = np.nonzero(e > -60)[0]
    return int(active[0]) * 320 if len(active) else 0


def build_scenarios(corpus, profile, meta, work):
    ch = profile["channels"]
    voice = read_s24(os.path.join(corpus, "voice.raw"), ch)
    music = read_s24(os.path.join(corpus, "music.raw"), ch)
    assistant = read_s24(os.path.join(corpus, "assistant.raw"), ch)
    utt = [tuple(u) for u in meta["utterances"]]
    music = music[onset(music, profile):]
    assistant = assistant[onset(assistant, profile):]

    def over(far, lead):
        n = int(lead * RATE) + len(voice)
        far = np.tile(far, (int(np.ceil(n / len(far))), 1))[:n]
        mix = far.copy()
        mix[int(lead * RATE):, : profile["mics"]] += voice[:, : profile["mics"]]
        return mix, [(a + lead, b + lead) for a, b in utt]

    scenarios = {}
    scenarios["quiet"] = (voice, utt)
    scenarios["music"] = over(music, MUSIC_LEAD_S)
    scenarios["music_only"] = (music, [])
    scenarios["assistant"] = (assistant, [])
    scenarios["barge_in"] = over(assistant, ASSISTANT_LEAD_S)
    for name, (x, _) in scenarios.items():
        write_s24(os.path.join(work, f"{name}.raw"), x)
    return scenarios


def process(echo, profile, work, name, settings):
    remote_in, remote_out = f"{DEV_DIR}/{name}.raw", f"{DEV_DIR}/{name}.out"
    echo.push(os.path.join(work, f"{name}.raw"), remote_in)
    if profile["kind"] == "show":
        sets = " ".join(settings)
        echo.sh(f"AMZNAEC_OFFLINE_MONO=1 LD_LIBRARY_PATH=/vendor/lib {DEV_DIR}/amznaec_offline {remote_in} {remote_out} "
                f"{profile['channels']} {sets}")
    else:
        out_mic = next((s.split("=", 1)[1] for s in settings if s.startswith("out_mic=")), "0")
        echo.sh(f"{DEV_DIR}/mic_beam_offline {remote_in} {remote_out} {out_mic}")
    local = os.path.join(work, f"{name}.out")
    echo.pull(remote_out, local)
    echo.sh(f"rm -f {remote_in} {remote_out}", check=False)
    if profile["kind"] == "show":
        # The shim delays its output by one 10 ms block.
        return read_s24(local, 1)[160:, 0]
    return np.fromfile(local, dtype=np.int16).astype(np.float64) / 32768


# Which wake word engine scores, set in main(): vsWakeWord, the engine Kiosk
# Satellite runs, or microWakeWord, which is far more forgiving of what the
# echo canceller leaves and so is only a fallback.
SCORER = {"kind": "mww"}


def wake_events(y):
    """Detection times, and the engine's score over time."""
    if SCORER["kind"] == "vsww":
        return vsww_events(y)
    from pymicro_wakeword import MicroWakeWord, MicroWakeWordFeatures, Model
    mww = MicroWakeWord.from_builtin(Model.OKAY_NABU)
    features = MicroWakeWordFeatures()
    pcm = (np.clip(y, -1, 1 - 1 / 32768) * 32767).astype(np.int16).tobytes()
    times, probs = [], []
    for k in range(0, len(pcm), 320):
        for f in features.process_streaming(pcm[k:k + 320]):
            times.append((k + 320) / 2 / RATE)
            probs.append(mww.process_streaming_prob(f) or 0.0)
    times, probs = np.array(times), np.array(probs)
    events, last = [], -10.0
    for t, p in zip(times, probs):
        if p > mww.probability_cutoff and t - last > 1.5:
            events.append(t)
            last = t
    return events, times, probs


def vsww_events(y):
    import tempfile
    with tempfile.NamedTemporaryFile(suffix=".s16", delete=False) as f:
        path = f.name
    try:
        (np.clip(y, -1, 1 - 1 / 32768) * 32767).astype(np.int16).tofile(path)
        out = subprocess.run([SCORER["python"], SCORER["script"], SCORER["repo"], SCORER["model"], path],
                             capture_output=True, text=True, check=True).stdout
    finally:
        os.unlink(path)
    result = json.loads(out.strip().splitlines()[-1])
    fired = np.array(result["fired"], dtype=float).reshape(-1, 2)
    return result["detections"], fired[:, 0], fired[:, 1]


def score_wake(y, utt):
    events, times, probs = wake_events(y)
    hit, peaks = [], []
    for a, b in utt:
        w = (times >= a - 0.3) & (times <= b + 1.5)
        peaks.append(float(probs[w].max()) if w.any() else 0.0)
        hit.append(any(a - 0.3 <= e <= b + 1.5 for e in events))
    false = sum(1 for e in events if not any(a - 0.3 <= e <= b + 1.5 for a, b in utt))
    return hit, peaks, false


def run(args):
    echo, device, profile = connect(args)
    base = os.path.join(args.workspace, "audio-test", device)
    corpus = os.path.join(base, "corpus")
    meta_path = os.path.join(corpus, "meta.json")
    if not os.path.exists(meta_path):
        raise SystemExit(f"ERROR: no corpus for {device} yet: scripts/audio-test.sh capture")
    meta = json.load(open(meta_path))
    install_tools(args, echo, device, profile)
    work = os.path.join(base, "work")
    os.makedirs(work, exist_ok=True)
    scenarios = build_scenarios(corpus, profile, meta, work)
    ch0 = {name: x[:, 0] for name, (x, _) in scenarios.items()}
    out = {}
    for name in scenarios:
        print(f"Processing {name}...", flush=True)
        out[name] = process(echo, profile, work, name, args.set)

    r = {}
    # Quiet: the wake words, and how much of the room and of each word survives.
    hit, peaks, false = score_wake(out["quiet"], scenarios["quiet"][1])
    r["quiet.wake_rate"] = float(np.mean(hit))
    r["quiet.false_triggers"] = false
    x, y = ch0["quiet"], out["quiet"][: len(ch0["quiet"])]
    ex, ey = frames_db(x), frames_db(y)
    n = min(len(ex), len(ey))
    ex, ey = ex[:n], ey[:n]
    peak = np.percentile(ex, 99)
    noise, strong = ex < np.percentile(ex, 20) + 2, ex > peak - 10
    weak = (ex <= peak - 10) & (ex > peak - 25)
    gain = float(np.mean(ey[strong] - ex[strong]))
    r["quiet.floor_cut_db"] = float(gain - np.mean(ey[noise] - ex[noise]))
    r["quiet.weak_speech_loss_db"] = float(gain - np.mean(ey[weak] - ex[weak]))
    voice_level = float(np.median(ey[strong]))

    # Music: every wake word, the ones in the loud passages on their own.
    music_x, utt = scenarios["music"]
    hit, peaks, false = score_wake(out["music"], utt)
    # Loud by what the microphones hear of the music: Amazon's playback
    # levels the loopback, so it barely moves between verse and chorus.
    echo_only = scenarios["music_only"][0][:, 0]
    echo_only = np.tile(echo_only, int(np.ceil(len(music_x) / len(echo_only))))[: len(music_x)]
    overall = db(echo_only[int(MUSIC_LEAD_S * RATE):])
    loud = [db(echo_only[int(a * RATE):int(b * RATE)]) > overall + 2 for a, b in utt]
    r["music.wake_rate"] = float(np.mean(hit))
    r["music.loud_wake_rate"] = float(np.mean([h for h, l in zip(hit, loud) if l])) if any(loud) else None
    r["music.loud_wake_words"] = int(sum(loud))
    r["music.mean_peak"] = float(np.mean(peaks))
    r["music.weakest_peak"] = float(np.min(peaks))
    r["music.false_triggers"] = false
    r["music_only.false_triggers"] = score_wake(out["music_only"], [])[2]

    # The assistant's voice: what leaks back toward the user's voice level.
    a_x = scenarios["assistant"][0]
    y = out["assistant"][: len(a_x)]
    far = frames_db(ref_signal(a_x, profile)) > -60
    ey = frames_db(y)
    n = min(len(far), len(ey))
    far, ey = far[:n], ey[:n]
    r["assistant.leak_frames"] = float(np.mean(ey[far] > voice_level - LEAK_MARGIN_DB))
    r["assistant.echo_removed_db"] = float(db(a_x[:, 0]) - (db(y) - gain))
    r["assistant.self_triggers"] = score_wake(out["assistant"], [])[2]

    # Barge-in: the wake words said over the assistant's voice.
    hit, peaks, false = score_wake(out["barge_in"], scenarios["barge_in"][1])
    r["barge_in.wake_rate"] = float(np.mean(hit))
    r["barge_in.false_triggers"] = false

    report(args, base, device, profile, meta, r, "run")


# --- calibrate ---------------------------------------------------------------

def calibrate(args):
    """The speakers' directions across the microphones for each frequency:
    the strongest eigenvectors of the microphones' covariance while only the
    music plays, for the same 320 point transform as speaker_null.cpp."""
    profile = DEVICES[args.device]
    if profile["kind"] != "show":
        raise SystemExit("ERROR: the speaker null is for the Echo Shows")
    corpus = args.corpus or os.path.join(args.workspace, "audio-test", args.device, "corpus")
    x = read_s24(os.path.join(corpus, "music.raw"), profile["channels"])
    x = x[onset(x, profile):, : profile["mics"]]
    n, hop = 320, 160
    window = np.sqrt(0.5 - 0.5 * np.cos(2 * np.pi * np.arange(n) / n))
    frames = (len(x) - n) // hop
    spec = np.stack([np.fft.rfft(x[i * hop:i * hop + n] * window[:, None], axis=0) for i in range(frames)])
    cov = np.einsum("fbm,fbn->bmn", spec, spec.conj()) / frames
    lines = [f"amznaec-null 1 {profile['mics']} {cov.shape[0]} {args.rank}"]
    for b in range(cov.shape[0]):
        e = np.linalg.eigh(cov[b])[1][:, -args.rank:]
        for m in range(profile["mics"]):
            for k in range(args.rank):
                lines.append(f"{b} {m} {k} {e[m, k].real:.7f} {e[m, k].imag:.7f}")
    out = args.out or os.path.join(args.workspace, "audio-test", args.device, "speaker-null.txt")
    open(out, "w").write("\n".join(lines) + "\n")
    print(f"Wrote {out} from {corpus}")


# --- live --------------------------------------------------------------------

def live(args):
    echo, device, profile = connect(args)
    base = os.path.join(args.workspace, "audio-test", device)
    install_tools(args, echo, device, profile)
    uid = ks_uid(echo)
    wait_for_free_mic(echo)
    r = {}
    old_log = echo.prop("persist.vendor.amznaec.log")
    if profile["kind"] == "show":
        echo.sh("setprop persist.vendor.amznaec.log 1")
    start = echo.sh("date +'%m-%d %H:%M:%S.000'")
    try:
        # Path: what Android hands an app against what the processing put out.
        prefix = f"{DEV_DIR}/path"
        echo.sh(f"setprop {dump_prop(profile)} {prefix}")
        try:
            echo.sh(f"ECHO_HARNESS_SAVE={prefix}.app.raw {DEV_DIR}/echo_audio_harness 8 0 0 -12 "
                    f"{uid} {KS_PACKAGE} >/dev/null 2>&1")
        finally:
            echo.sh(f"setprop {dump_prop(profile)} ''", check=False)
        work = os.path.join(base, "work")
        os.makedirs(work, exist_ok=True)
        for f in ("path.app.raw", "path.out.raw"):
            echo.pull(f"{DEV_DIR}/{f}", os.path.join(work, f))
        app = np.fromfile(os.path.join(work, "path.app.raw"), dtype=np.int16) / 32768.0
        if profile["kind"] == "show":
            proc = read_s24(os.path.join(work, "path.out.raw"), profile["channels"])[:, 0]
        else:
            proc = np.fromfile(os.path.join(work, "path.out.raw"), dtype=np.int16) / 32768.0
        seg = app[2 * RATE:6 * RATE]
        n = 1 << int(np.ceil(np.log2(len(proc) + len(seg))))
        xc = np.fft.irfft(np.fft.rfft(proc, n) * np.conj(np.fft.rfft(seg, n)), n)[: len(proc) - len(seg)]
        k = int(np.argmax(np.abs(xc)))
        w = proc[k:k + len(seg)]
        r["live.path_correlation"] = float(np.dot(w, seg) / np.sqrt(np.dot(w, w) * np.dot(seg, seg) + 1e-30))

        # Level: the quiet room before and after loud noise through the speaker.
        echo.sh(f"ECHO_HARNESS_SAVE={DEV_DIR}/level.raw {DEV_DIR}/echo_audio_harness 8 8 20 -12 "
                f"{uid} {KS_PACKAGE} >/dev/null 2>&1")
        echo.pull(f"{DEV_DIR}/level.raw", os.path.join(work, "level.raw"))
        lv = np.fromfile(os.path.join(work, "level.raw"), dtype=np.int16) / 32768.0
        before, after = db(lv[2 * RATE:8 * RATE]), db(lv[26 * RATE:36 * RATE])
        r["live.level_before_db"], r["live.level_after_db"] = float(before), float(after)
        r["live.level_change_after_loud_db"] = float(abs(after - before))

        # Wi-Fi: no reassociation while connected.
        print(f"Watching Wi-Fi for {args.wifi_seconds} s...", flush=True)
        time.sleep(args.wifi_seconds)
        log = echo.sh(f"logcat -d -b all -T '{start}'", check=False)
        r["live.wifi_reassociations"] = len(re.findall(r"CTRL-EVENT-(?:CONNECTED|DISCONNECTED)", log))
        gaps = [float(g) for g in re.findall(r"longest read gap (\d+) ms", log)]
        r["live.hal_read_gap_ms"] = max(gaps) if gaps else None
    finally:
        if profile["kind"] == "show":
            echo.sh(f"setprop persist.vendor.amznaec.log '{old_log}'", check=False)
    report(args, base, device, profile, {}, r, "live")


# --- report ------------------------------------------------------------------

def report(args, base, device, profile, meta, r, kind):
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    label = f"-{args.label}" if args.label else ""
    reports = os.path.join(base, "reports")
    os.makedirs(reports, exist_ok=True)
    rows, failed = [], 0
    for key, value in r.items():
        rule = MVP.get(key)
        verdict = ""
        if rule and value is not None:
            ok = value >= rule[1] if rule[0] == "min" else value <= rule[1]
            verdict = "pass" if ok else "FAIL"
            failed += 0 if ok else 1
        shown = "n/a" if value is None else (f"{value:.2f}" if isinstance(value, float) else str(value))
        limit = f"{'at least' if rule[0] == 'min' else 'at most'} {rule[1]}" if rule else ""
        rows.append(f"| {key} | {shown} | {limit} | {verdict} |")
    title = f"{profile['name']} audio test ({kind}{label})"
    lines = [f"# {title}", "", f"- Date: {stamp}", f"- Build: {meta.get('build', '')}",
             f"- Wake word engine: {SCORER.get('name', 'microWakeWord okay_nabu')}",
             f"- Settings: {' '.join(args.set) if getattr(args, 'set', None) else 'image defaults'}"]
    if meta.get("track"):
        lines.append(f"- Track: {meta['track']['file']} {meta['track']['start']}-{meta['track']['end']} s, "
                     f"volume {meta['volume_index']}/{meta['volume_max']}")
    lines += ["", "| Metric | Value | Minimum | Result |", "|---|---|---|---|"] + rows
    lines += ["", f"**{'PASS' if failed == 0 else f'FAIL ({failed})'}**"]
    text = "\n".join(lines) + "\n"
    path = os.path.join(reports, f"{stamp}-{kind}{label}")
    open(path + ".md", "w").write(text)
    json.dump({"device": device, "kind": kind, "label": args.label, "settings": getattr(args, "set", []),
               "results": r}, open(path + ".json", "w"), indent=2)
    print("\n" + text + f"\nSaved {path}.md")
    sys.exit(1 if failed else 0)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--repo", required=True)
    p.add_argument("--workspace", required=True)
    p.add_argument("--label", default="")
    p.add_argument("--scorer", choices=["auto", "vsww", "mww"], default="auto",
                   help="vsWakeWord (VSWW_REPO, default ~/repos/vs-wake-word) or microWakeWord")
    sub = p.add_subparsers(dest="command", required=True)
    c = sub.add_parser("calibrate")
    c.add_argument("--device", required=True, choices=[d for d, p in DEVICES.items() if p["kind"] == "show"])
    c.add_argument("--corpus", help="corpus folder (default: the device's)")
    c.add_argument("--rank", type=int, default=2, help="directions to remove, one per speaker")
    c.add_argument("--out")
    for name in ("capture", "run", "live"):
        s = sub.add_parser(name)
        s.add_argument("--serial", default=os.environ.get("ANDROID_SERIAL"), required="ANDROID_SERIAL" not in os.environ)
        s.add_argument("--label", default="")
        if name == "capture":
            s.add_argument("--only", help="voice, music or assistant, comma separated")
            s.add_argument("--utterances", type=int, default=20)
            s.add_argument("--volume", type=int, help="media volume index (default: the image's default)")
            s.add_argument("--music", default=None)
            s.add_argument("--mouth", help="serial of a second Echo that plays the wake words in place of a person")
            s.add_argument("--voices", default=os.environ.get(
                "AUDIO_TEST_VOICES", "/mnt/training_data/data/benchmarks/tts/ok_nabu"),
                help="vsWakeWord's benchmark sets, for --mouth")
            s.add_argument("--mouth-volume", type=int, help="the second Echo's media volume (default: matched)")
            s.add_argument("--voice-level", type=float, default=-36.0,
                           help="dBFS at microphone 0 the mouth is matched to (a voice across a room)")
            s.add_argument("--music-start", type=float, default=40)
            s.add_argument("--music-end", type=float, default=90)
        if name == "run":
            s.add_argument("--set", nargs="*", default=[],
                           help="processing settings to try, such as engine=3 aec_level=1 (Shows) or out_mic=2 (Dot)")
        if name == "live":
            s.add_argument("--wifi-seconds", type=int, default=120)
    args = p.parse_args()
    repo = os.environ.get("VSWW_REPO", os.path.expanduser("~/repos/vs-wake-word"))
    model = os.environ.get("VSWW_MODEL", os.path.join(repo, "production", "models", "ok_nabu"))
    python = os.path.join(repo, ".venv", "bin", "python")
    if args.scorer == "vsww" or (args.scorer == "auto" and os.path.exists(python) and os.path.isdir(model)):
        if not os.path.exists(python):
            raise SystemExit(f"ERROR: no vs-wake-word environment at {python}")
        SCORER.update(kind="vsww", repo=repo, model=model, python=python,
                      script=os.path.join(args.repo, "scripts", "vsww_score.py"),
                      name=f"vsWakeWord {os.path.basename(model)} ({model})")
    if args.command == "capture" and not args.music:
        args.music = os.path.join(args.repo, ".internal", "track.mp3")
    {"capture": capture, "run": run, "live": live, "calibrate": calibrate}[args.command](args)


if __name__ == "__main__":
    main()
