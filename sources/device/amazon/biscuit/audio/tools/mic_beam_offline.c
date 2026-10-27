/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Runs the microphone array processing (mic_beam.c) on a recording instead
 * of the live microphones, to compare changes on identical audio. It feeds
 * mic_beam's PCM reads from a file in the array stream's shape (S24_3LE,
 * 16 kHz, 9 channels), such as a persist.biscuit.mic_beam_dump .in.raw
 * file, and writes the output as 16 kHz mono 16 bit samples.
 *
 *   mic_beam_offline <in.raw> <out.raw> [output microphone]
 *
 * Each run starts from an untrained model, as after a boot. Not in the
 * image.
 */

#include <stdio.h>
#include <stdlib.h>

#include <tinyalsa/asoundlib.h>

#include "mic_beam.h"

static FILE* input;

/* mic_beam.c's PCM calls land here instead of in libtinyalsa. */
struct pcm* pcm_open(unsigned int card, unsigned int device, unsigned int flags,
                     struct pcm_config* config) {
    (void)card;
    (void)device;
    (void)flags;
    (void)config;
    return (struct pcm*)&input;
}

int pcm_is_ready(struct pcm* pcm) {
    (void)pcm;
    return 1;
}

const char* pcm_get_error(struct pcm* pcm) {
    (void)pcm;
    return "end of the recording";
}

int pcm_read(struct pcm* pcm, void* data, unsigned int count) {
    (void)pcm;
    return fread(data, 1, count, input) == count ? 0 : -1;
}

int pcm_close(struct pcm* pcm) {
    (void)pcm;
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <in.raw> <out.raw> [output microphone]\n", argv[0]);
        return 1;
    }
    input = fopen(argv[1], "rb");
    FILE* output = fopen(argv[2], "wb");
    if (!input || !output) {
        perror("cannot open the files");
        return 1;
    }
    struct mic_beam_model* model = mic_beam_model_create();
    struct mic_beam* beam = mic_beam_open(model, argc > 3 ? atoi(argv[3]) : 0);
    if (!beam) {
        fprintf(stderr, "mic_beam_open failed\n");
        return 1;
    }
    int16_t buf[256];
    while (mic_beam_read(beam, buf, 256) == 256) fwrite(buf, sizeof(int16_t), 256, output);
    mic_beam_close(beam);
    mic_beam_model_free(model);
    fclose(output);
    fclose(input);
    return 0;
}
