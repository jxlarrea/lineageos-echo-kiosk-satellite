/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "biscuit_pcm.h"

#include <stdio.h>
#include <string.h>

/* A line reads "00-22: TLV320AIC3101 Capture tlv320aic3101-codec-22 :  : capture 1". */
int biscuit_find_pcm(const char* name, bool capture) {
    FILE* f = fopen("/proc/asound/pcm", "re");
    char line[256];
    int found = -1;

    if (!f) return -1;
    while (found < 0 && fgets(line, sizeof(line), f)) {
        int card, device;
        char* id;

        if (sscanf(line, "%d-%d:", &card, &device) != 2 || card != BISCUIT_CARD) continue;
        id = strchr(line, ':');
        if (!id) continue;
        id++;
        while (*id == ' ') id++;
        if (strncmp(id, name, strlen(name)) != 0 || id[strlen(name)] != ' ') continue;
        if (strstr(line, capture ? ": capture" : ": playback")) found = device;
    }
    fclose(f);
    return found;
}
