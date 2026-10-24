/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BISCUIT_PCM_H
#define BISCUIT_PCM_H

#include <stdbool.h>

#define BISCUIT_CARD 0

/* The microphone array: the TLV320AIC3101 ADCs read over SPI. */
#define BISCUIT_MIC_PCM_NAME "TLV320AIC3101 Capture"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Finds a PCM device on card 0 by its stream name in /proc/asound/pcm. The
 * numbers change between kernels: the microphone array is device 24 on the
 * Dot's 3.18 kernel and 22 on 4.9. Returns the device or -1.
 */
int biscuit_find_pcm(const char* name, bool capture);

#ifdef __cplusplus
}
#endif

#endif
