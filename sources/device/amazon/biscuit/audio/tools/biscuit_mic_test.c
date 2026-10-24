/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Reads the Echo Dot's microphone array directly and prints the level of
 * each channel: microphones 0 to 6, an unused channel 7 and the codec's copy
 * of the speaker signal on 8. A microphone far below the others is weak.
 *
 *   biscuit_mic_test [seconds] [file.wav]
 *
 * With a file it also saves the capture as a 9-channel 24-bit WAV. Stop
 * audioserver first if the audio HAL holds the microphones.
 */

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tinyalsa/asoundlib.h>

#include "../biscuit_pcm.h"

#define RATE 16000
#define CHANNELS 9
#define BYTES 3
#define PERIOD_SIZE 256
#define PERIOD_COUNT 4

static int32_t s24_3le(const unsigned char* p) {
    int32_t v = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16));
    if (v & 0x800000) v |= ~0xffffff;
    return v;
}

static void put16(FILE* f, uint16_t v) {
    fputc(v & 0xff, f);
    fputc(v >> 8, f);
}

static void put32(FILE* f, uint32_t v) {
    put16(f, v & 0xffff);
    put16(f, v >> 16);
}

static void write_wav_header(FILE* f, uint32_t frames) {
    uint32_t data = frames * CHANNELS * BYTES;

    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + data);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, CHANNELS);
    put32(f, RATE);
    put32(f, RATE * CHANNELS * BYTES);
    put16(f, CHANNELS * BYTES);
    put16(f, BYTES * 8);
    fwrite("data", 1, 4, f);
    put32(f, data);
}

int main(int argc, char** argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 1;
    const char* path = argc > 2 ? argv[2] : NULL;
    struct pcm_config config = {
        .channels = CHANNELS,
        .rate = RATE,
        .period_size = PERIOD_SIZE,
        .period_count = PERIOD_COUNT,
        .format = PCM_FORMAT_S24_3LE,
    };
    int device = biscuit_find_pcm(BISCUIT_MIC_PCM_NAME, true);
    size_t period_bytes = PERIOD_SIZE * CHANNELS * BYTES;
    double sum[CHANNELS] = {0};
    int32_t peak[CHANNELS] = {0};
    uint32_t frames_total = 0;
    int periods = seconds * RATE / PERIOD_SIZE;
    unsigned char* buf;
    struct pcm* pcm;
    FILE* wav = NULL;

    if (device < 0) {
        fprintf(stderr, "no \"%s\" PCM in /proc/asound/pcm\n", BISCUIT_MIC_PCM_NAME);
        return 1;
    }
    pcm = pcm_open(BISCUIT_CARD, device, PCM_IN, &config);
    if (!pcm || !pcm_is_ready(pcm)) {
        fprintf(stderr, "pcm_open(%d) failed: %s\n", device, pcm ? pcm_get_error(pcm) : "no memory");
        return 1;
    }
    buf = malloc(period_bytes);
    if (!buf) return 1;
    if (path) {
        wav = fopen(path, "wb");
        if (!wav) {
            fprintf(stderr, "cannot write %s: %s\n", path, strerror(errno));
            return 1;
        }
        write_wav_header(wav, 0);
    }

    for (int p = 0; p < periods; p++) {
        if (pcm_read(pcm, buf, period_bytes) != 0) {
            fprintf(stderr, "pcm_read failed: %s\n", pcm_get_error(pcm));
            return 1;
        }
        for (int f = 0; f < PERIOD_SIZE; f++) {
            for (int ch = 0; ch < CHANNELS; ch++) {
                int32_t v = s24_3le(buf + (f * CHANNELS + ch) * BYTES);
                int32_t a = v < 0 ? -v : v;
                if (a > peak[ch]) peak[ch] = a;
                sum[ch] += (double)v * (double)v;
            }
        }
        if (wav) fwrite(buf, 1, period_bytes, wav);
        frames_total += PERIOD_SIZE;
    }

    printf("PCM %d, %u frames\n", device, frames_total);
    for (int ch = 0; ch < CHANNELS; ch++) {
        double rms = sqrt(sum[ch] / frames_total);
        printf("ch%d rms=%.0f (%.1f dBFS) peak=%d\n", ch, rms,
               rms > 0 ? 20.0 * log10(rms / 8388608.0) : -INFINITY, peak[ch]);
    }

    if (wav) {
        rewind(wav);
        write_wav_header(wav, frames_total);
        fclose(wav);
    }
    free(buf);
    pcm_close(pcm);
    return 0;
}
