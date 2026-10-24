/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Plays a tone or records through Android's audio stack, the way an app
 * does, to test the whole path down to the Fire OS HAL.
 *
 *   biscuit_audio_test play [seconds] [hz] [level 0-1]
 *   biscuit_audio_test record [seconds] [source]
 *
 * record prints the level of what it got. Source 6 is VOICE_RECOGNITION,
 * the capture Kiosk Satellite's wake word uses, which reads the microphone
 * array with the speaker removed. Source 1 is MIC.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <media/AudioRecord.h>
#include <media/AudioTrack.h>
#include <system/audio.h>
#include <utils/Errors.h>
#include <utils/String16.h>
#include <utils/StrongPointer.h>

using android::AudioRecord;
using android::AudioTrack;
using android::sp;
using android::status_t;
using android::String16;

static int play(int seconds, double hz, double level) {
    const uint32_t rate = 48000;
    size_t min_frames = 0;
    status_t ret = AudioTrack::getMinFrameCount(&min_frames, AUDIO_STREAM_MUSIC, rate);

    if (ret != android::NO_ERROR) {
        fprintf(stderr, "getMinFrameCount failed: %d\n", ret);
        return 1;
    }
    sp<AudioTrack> track = new AudioTrack(AUDIO_STREAM_MUSIC, rate, AUDIO_FORMAT_PCM_16_BIT,
                                          AUDIO_CHANNEL_OUT_STEREO, min_frames * 2,
                                          AUDIO_OUTPUT_FLAG_NONE, nullptr, nullptr, 0,
                                          AUDIO_SESSION_ALLOCATE, AudioTrack::TRANSFER_SYNC);
    if ((ret = track->initCheck()) != android::NO_ERROR) {
        fprintf(stderr, "AudioTrack init failed: %d\n", ret);
        return 1;
    }
    if ((ret = track->start()) != android::NO_ERROR) {
        fprintf(stderr, "AudioTrack start failed: %d\n", ret);
        return 1;
    }

    const size_t frames = 1024;
    int16_t buf[frames * 2];
    const int total = rate * seconds;
    int written = 0;

    while (written < total) {
        size_t todo = total - written < (int)frames ? total - written : frames;
        for (size_t i = 0; i < todo; i++) {
            double s = sin(2.0 * M_PI * hz * (written + i) / rate) * level * 32767.0;
            buf[i * 2] = buf[i * 2 + 1] = (int16_t)s;
        }
        ssize_t n = track->write(buf, todo * 2 * sizeof(int16_t), true);
        if (n < 0) {
            fprintf(stderr, "AudioTrack write failed: %zd\n", n);
            return 1;
        }
        written += n / (2 * sizeof(int16_t));
    }
    usleep(300000);
    track->stop();
    printf("played %d frames at %.0f Hz\n", written, hz);
    return 0;
}

static int record(int seconds, audio_source_t source) {
    const uint32_t rate = 16000;
    size_t min_frames = 0;
    status_t ret = AudioRecord::getMinFrameCount(&min_frames, rate, AUDIO_FORMAT_PCM_16_BIT,
                                                 AUDIO_CHANNEL_IN_MONO);

    if (ret != android::NO_ERROR) {
        fprintf(stderr, "getMinFrameCount failed: %d\n", ret);
        return 1;
    }
    sp<AudioRecord> rec = new AudioRecord(source, rate, AUDIO_FORMAT_PCM_16_BIT,
                                          AUDIO_CHANNEL_IN_MONO, String16("com.android.shell"),
                                          min_frames * 2, nullptr, nullptr, 0,
                                          AUDIO_SESSION_ALLOCATE, AudioRecord::TRANSFER_SYNC);
    if ((ret = rec->initCheck()) != android::NO_ERROR) {
        fprintf(stderr, "AudioRecord init failed: %d\n", ret);
        return 1;
    }
    if ((ret = rec->start()) != android::NO_ERROR) {
        fprintf(stderr, "AudioRecord start failed: %d\n", ret);
        return 1;
    }

    int16_t buf[1024];
    const int total = rate * seconds;
    int got = 0;
    int peak = 0;
    double sum = 0;

    while (got < total) {
        ssize_t n = rec->read(buf, sizeof(buf));
        if (n < 0) {
            fprintf(stderr, "AudioRecord read failed: %zd\n", n);
            return 1;
        }
        for (ssize_t i = 0; i < n / (ssize_t)sizeof(int16_t); i++) {
            int a = abs(buf[i]);
            if (a > peak) peak = a;
            sum += (double)buf[i] * buf[i];
        }
        got += n / sizeof(int16_t);
    }
    rec->stop();
    double rms = sqrt(sum / got);
    printf("recorded %d samples from source %d: rms=%.0f (%.1f dBFS) peak=%d\n", got,
           (int)source, rms, rms > 0 ? 20.0 * log10(rms / 32768.0) : -INFINITY, peak);
    return 0;
}

int main(int argc, char** argv) {
    int ret;

    if (argc > 1 && !strcmp(argv[1], "play")) {
        ret = play(argc > 2 ? atoi(argv[2]) : 2, argc > 3 ? atof(argv[3]) : 880.0,
                   argc > 4 ? atof(argv[4]) : 0.35);
    } else if (argc > 1 && !strcmp(argv[1], "record")) {
        ret = record(argc > 2 ? atoi(argv[2]) : 3,
                     (audio_source_t)(argc > 3 ? atoi(argv[3]) : AUDIO_SOURCE_VOICE_RECOGNITION));
    } else {
        fprintf(stderr, "usage: %s play [seconds] [hz] [level] | record [seconds] [source]\n",
                argv[0]);
        return 2;
    }
    fflush(stdout);
    /* Skip the AudioTrack and AudioRecord destructors, which tear down the
     * binder threads of a short-lived shell tool badly. */
    _exit(ret);
}
