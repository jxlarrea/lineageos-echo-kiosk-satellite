/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/*
 * A fixed null on the speakers across the microphones, ahead of the echo
 * canceller.
 *
 * The speakers never move relative to the microphones, so for each frequency
 * everything they play, the distortion an echo canceller cannot model
 * included, reaches the microphones from the same few directions. With two
 * speakers and four microphones there are two such directions and two more
 * for a voice. A calibration measured once on a Show of that model gives
 * them, and this removes them from microphone 0.
 *
 * The directions are not learned while running: learning them from the
 * microphones also learns a voice that talks over the music, and learning
 * them from the speaker signal misses the distortion. The removal fades in
 * while the speaker plays and out after it stops, so in a quiet room
 * microphone 0 passes through unchanged.
 *
 * On the audio test's mixes (scripts/audio-test.sh) it looked better on an
 * Echo Show 8 (1st gen) at 50% volume: 9 of 10 and 17 of 20 wake words over
 * music in two rooms instead of 8 of 10 and 16 of 20, and the assistant's
 * voice back within 25 dB of the user's 0% and 4% of the time instead of
 * 12% and 95%. A live session replayed through it disagreed: a wake word
 * said over the assistant's voice scored 0.40 and 0.04 with the null and
 * 0.86 without it, and the hardest wake word over music 0.49 instead of
 * 0.90. Off on every Show until that is understood.
 *
 * The calibration is a text file: a first line
 *   amznaec-null 1 <mics> <bins> <rank>
 * then one line per bin, microphone and direction: "<bin> <mic> <k> <re> <im>",
 * for a 320 point transform at 16 kHz (bins = 161). scripts/audio-test.sh
 * calibrate writes it from a recording of music.
 */

struct SpeakerNull;

/* NULL when the file cannot be read or does not match mics. */
SpeakerNull* speaker_null_load(const char* path, int mics);
void speaker_null_free(SpeakerNull* n);

/*
 * One block of 160 samples: each microphone as captured and the speaker
 * reference. Writes 160 samples of microphone 0 with the speakers removed,
 * 10 ms behind the input.
 */
void speaker_null_process(SpeakerNull* n, const float* const* mics, const float* ref, float* out);

/* How far the removal is faded in, 0 to 1. */
float speaker_null_mix(const SpeakerNull* n);
