/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Reads an Echo Show's microphone stream directly and prints the level of
 * each channel. The FPGA behind the TLV320AIC3101 codec sends the
 * microphones and a copy of the speaker signal in one stream: 4 channels on
 * the Echo Show 5 (2nd gen) and 6 on the Echo Show 8 (1st gen). A
 * microphone far below the others is weak.
 *
 *   echo_mic_test [seconds] [file.wav]
 *
 * With a file it also saves the capture as a 24-bit WAV with every channel.
 * The audio HAL only holds the stream while something records, so stop
 * whatever listens first, such as Kiosk Satellite's wake word.
 */

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tinyalsa/asoundlib.h>

#define CARD 0
#define PCM_NAME "TLV320AIC3101 Capture"
#define RATE 16000
#define BYTES 3
#define PERIOD_COUNT 4
#define MAX_CHANNELS 9

// The stream's shapes, as the kernel's amzn-mt-spi-pcm driver builds them:
// channels and the frames in each SPI transfer, a header frame included.
static const struct {
    unsigned int channels;
    unsigned int period_size;
} kShapes[] = {
    {4, 257},
    {6, 257},
    {9, 256},
};

// The device number of the PCM whose name starts with PCM_NAME, from lines
// such as "00-22: TLV320AIC3101 Capture tlv320aic3101-codec-22 : ...".
static int find_pcm(void) {
    FILE* f = fopen("/proc/asound/pcm", "r");
    char line[256];
    int device = -1;

    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        int card, dev;
        char* name = strchr(line, ':');
        if (!name || sscanf(line, "%d-%d", &card, &dev) != 2 || card != CARD) continue;
        name++;
        while (*name == ' ') name++;
        if (strncmp(name, PCM_NAME, strlen(PCM_NAME)) == 0) {
            device = dev;
            break;
        }
    }
    fclose(f);
    return device;
}

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

static void write_wav_header(FILE* f, unsigned int channels, uint32_t frames) {
    uint32_t data = frames * channels * BYTES;

    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + data);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, channels);
    put32(f, RATE);
    put32(f, RATE * channels * BYTES);
    put16(f, channels * BYTES);
    put16(f, BYTES * 8);
    fwrite("data", 1, 4, f);
    put32(f, data);
}

int main(int argc, char** argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 1;
    const char* path = argc > 2 ? argv[2] : NULL;
    int device = find_pcm();
    struct pcm* pcm = NULL;
    struct pcm_config config = {
        .rate = RATE,
        .period_count = PERIOD_COUNT,
        .format = PCM_FORMAT_S24_3LE,
    };
    double sum[MAX_CHANNELS] = {0};
    int32_t peak[MAX_CHANNELS] = {0};
    uint32_t frames_total = 0;
    unsigned char* buf;
    size_t period_bytes;
    unsigned int channels;
    int periods;
    FILE* wav = NULL;

    if (device < 0) {
        fprintf(stderr, "no \"%s\" PCM in /proc/asound/pcm\n", PCM_NAME);
        return 1;
    }
    // The driver takes exactly one shape, so try each until it opens.
    for (size_t i = 0; i < sizeof(kShapes) / sizeof(kShapes[0]); i++) {
        config.channels = kShapes[i].channels;
        config.period_size = kShapes[i].period_size;
        pcm = pcm_open(CARD, device, PCM_IN, &config);
        if (pcm && pcm_is_ready(pcm)) break;
        if (pcm) pcm_close(pcm);
        pcm = NULL;
    }
    if (!pcm) {
        fprintf(stderr, "cannot open PCM %d with 4, 6 or 9 channels. If the audio HAL holds it, stop "
                        "whatever records and try again.\n", device);
        return 1;
    }
    channels = config.channels;
    period_bytes = config.period_size * channels * BYTES;
    periods = seconds * RATE / (int)config.period_size;
    buf = malloc(period_bytes);
    if (!buf) return 1;
    if (path) {
        wav = fopen(path, "wb");
        if (!wav) {
            fprintf(stderr, "cannot write %s: %s\n", path, strerror(errno));
            return 1;
        }
        write_wav_header(wav, channels, 0);
    }

    for (int p = 0; p < periods; p++) {
        if (pcm_read(pcm, buf, period_bytes) != 0) {
            fprintf(stderr, "pcm_read failed: %s\n", pcm_get_error(pcm));
            return 1;
        }
        for (unsigned int f = 0; f < config.period_size; f++) {
            for (unsigned int ch = 0; ch < channels; ch++) {
                int32_t v = s24_3le(buf + (f * channels + ch) * BYTES);
                int32_t a = v < 0 ? -v : v;
                if (a > peak[ch]) peak[ch] = a;
                sum[ch] += (double)v * (double)v;
            }
        }
        if (wav) fwrite(buf, 1, period_bytes, wav);
        frames_total += config.period_size;
    }

    printf("PCM %d, %u channels, %u frames\n", device, channels, frames_total);
    for (unsigned int ch = 0; ch < channels; ch++) {
        double rms = sqrt(sum[ch] / frames_total);
        printf("ch%u rms=%.0f (%.1f dBFS) peak=%d\n", ch, rms,
               rms > 0 ? 20.0 * log10(rms / 8388608.0) : -INFINITY, peak[ch]);
    }

    if (wav) {
        rewind(wav);
        write_wav_header(wav, channels, frames_total);
        fclose(wav);
    }
    free(buf);
    pcm_close(pcm);
    return 0;
}
