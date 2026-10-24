/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BISCUIT_MIC_BEAM_H
#define BISCUIT_MIC_BEAM_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/*
 * Reads the Echo Dot's microphone array directly and removes the speaker
 * from it. See mic_beam.c.
 */

/* What has been learned about the speaker. Outlives each capture session. */
struct mic_beam_model;

/* One capture session, holding the microphone PCM open. */
struct mic_beam;

struct mic_beam_model* mic_beam_model_create(void);
void mic_beam_model_free(struct mic_beam_model* model);

/* Opens the microphone PCM, with out_mic (0 to 6) as the microphone the
 * output comes from. NULL when it cannot, such as when it is busy. */
struct mic_beam* mic_beam_open(struct mic_beam_model* model, int out_mic);

/* Fills out with frames of 16 kHz mono. Blocks like a PCM read. Returns
 * frames or a negative errno. */
ssize_t mic_beam_read(struct mic_beam* beam, int16_t* out, size_t frames);

void mic_beam_close(struct mic_beam* beam);

#endif
