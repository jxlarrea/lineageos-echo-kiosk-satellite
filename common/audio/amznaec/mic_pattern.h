/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/*
 * The Echo Dot's speaker removal (mic_beam) for the Echo Shows, combined with
 * a linear echo canceller.
 *
 * The speaker never moves relative to the microphones, so its sound, the
 * distortion a linear canceller cannot model included, always arrives with
 * the same pattern across them. For each frequency this learns that pattern
 * from the FPGA's copy of the speaker signal and removes microphone 0's
 * projection onto it. A voice comes from elsewhere and keeps most of itself.
 *
 * The Shows' microphones sit close together, so at low frequencies a voice
 * and the speaker look alike across them and the removal would take the
 * voice too. Below the split the output is the linear canceller's, which
 * keeps the voice whole. Above it the output is the pattern removal, raised
 * by a fixed curve that puts the voice back at the linear canceller's level.
 * The removal fades in while the speaker plays and out after it stops.
 *
 * Measured on an Echo Show 5 (2nd gen) over music: 12.8 dB of voice above
 * the music with the linear canceller alone, 15.7 dB combined, about 10 dB
 * more from 1.2 kHz up.
 */

struct MicPattern;

/* mics: 2 to 4. split_hz: where the output turns from the linear canceller
 * to the pattern removal, crossfaded from a fifth below to a fifth above. */
MicPattern* mic_pattern_create(int mics, int split_hz);
void mic_pattern_free(MicPattern* p);

/*
 * One block of n samples at 16 kHz: the linear canceller's output for
 * microphone 0, each microphone as captured and the speaker reference. Writes
 * n output samples, 32 ms behind the input.
 */
void mic_pattern_process(MicPattern* p, const float* linear, const float* const* mics,
                         const float* ref, int n, float* out);

/* How far the removal is faded in, 0 to 1. */
float mic_pattern_mix(const MicPattern* p);
