/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * echo_audio_harness: records the microphone through Android the way an app
 * does (AUDIO_SOURCE_MIC, 16 kHz mono, 16 bit, as Kiosk Satellite) while it
 * plays noise through the speaker at the media volume, and prints the
 * recorded level every half second. It shows what an app gets from the audio
 * HAL before, during and after loud playback, without the app.
 *
 * Not in the image. Build it with BUILD_TARGET=echo_audio_harness, push it to
 * /data/local/tmp and run it as root while nothing else records:
 *
 *   echo_audio_harness [quiet_s] [play_s] [after_s] [noise_dbfs] [uid package]
 *
 * Defaults: 10 s quiet, 20 s of noise at -12 dBFS, 30 s quiet. Android
 * silences every recording but the top app's, so to record next to an app
 * that holds the microphone, pass that app's uid and package: the harness
 * then records as the app, and both get the same stream. With
 * ECHO_HARNESS_SAVE=<file> set, the recording is also saved as raw 16 kHz
 * mono 16 bit samples. ECHO_HARNESS_SOURCE=<n> records from another
 * audio_source_t than AUDIO_SOURCE_MIC (1). ECHO_HARNESS_PARAMS=<kv pairs>
 * hands the audio HAL those parameters first, as AudioManager.setParameters.
 * ECHO_HARNESS_PLAY=<file> plays that file (raw 48 kHz stereo 16 bit,
 * looped) in place of the noise. ECHO_HARNESS_PLAY_ONLY=1 only plays, for an
 * Echo standing in for a person's voice.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <thread>
#include <vector>

#include <binder/ProcessState.h>
#include <media/AudioRecord.h>
#include <media/AudioSystem.h>
#include <media/AudioTrack.h>
#include <utils/String16.h>

using namespace android;

static double now_s() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static std::atomic<int> g_phase{0};  // 0 quiet before, 1 playing, 2 quiet after
static const char* kPhase[] = {"quiet", "PLAY", "after"};

static void play(double seconds, double dbfs) {
    const uint32_t rate = 48000;
    sp<AudioTrack> track = new AudioTrack(AUDIO_STREAM_MUSIC, rate, AUDIO_FORMAT_PCM_16_BIT,
                                          AUDIO_CHANNEL_OUT_STEREO, 0, AUDIO_OUTPUT_FLAG_NONE);
    if (track->initCheck() != NO_ERROR) {
        fprintf(stderr, "cannot create the AudioTrack: %d\n", track->initCheck());
        return;
    }
    track->start();
    const double amp = 32767.0 * pow(10.0, dbfs / 20.0);
    std::vector<int16_t> buf(rate / 50 * 2);  // 20 ms, stereo
    FILE* file = nullptr;
    if (const char* path = getenv("ECHO_HARNESS_PLAY")) {
        file = fopen(path, "rb");
        if (!file) perror(path);
    }
    // Noise shaped a little toward lower frequencies, with a slow level
    // swing, so it is closer to music than flat white noise.
    double lp = 0;
    uint32_t seed = 12345;
    const double end = now_s() + seconds;
    double t = 0;
    while (now_s() < end) {
        if (file) {
            size_t got = fread(buf.data(), sizeof(int16_t), buf.size(), file);
            if (got < buf.size()) {
                rewind(file);
                fread(buf.data() + got, sizeof(int16_t), buf.size() - got, file);
            }
            track->write(buf.data(), buf.size() * sizeof(int16_t));
            continue;
        }
        for (size_t i = 0; i < buf.size(); i += 2) {
            seed = seed * 1664525u + 1013904223u;
            double white = ((int32_t)seed) / 2147483648.0;
            lp = 0.85 * lp + 0.15 * white;
            double swing = 0.6 + 0.4 * sin(2 * M_PI * 0.5 * t);
            int16_t s = (int16_t)(amp * swing * (lp * 2.5));
            buf[i] = buf[i + 1] = s;
            t += 1.0 / rate;
        }
        track->write(buf.data(), buf.size() * sizeof(int16_t));
    }
    track->stop();
    if (file) fclose(file);
}

int main(int argc, char** argv) {
    double quiet = argc > 1 ? atof(argv[1]) : 10;
    double playing = argc > 2 ? atof(argv[2]) : 20;
    double after = argc > 3 ? atof(argv[3]) : 30;
    double dbfs = argc > 4 ? atof(argv[4]) : -12;
    const char* package = "com.android.shell";
    if (const char* params = getenv("ECHO_HARNESS_PARAMS")) {
        status_t rc = AudioSystem::setParameters(String8(params));
        fprintf(stderr, "set %s: %d\n", params, rc);
    }
    if (argc > 6) {
        uid_t uid = (uid_t)atoi(argv[5]);
        if (setresgid(uid, uid, uid) != 0 || setresuid(uid, uid, uid) != 0) {
            perror("cannot switch to that uid");
            return 1;
        }
        package = argv[6];
    }

    ProcessState::self()->startThreadPool();

    if (getenv("ECHO_HARNESS_PLAY_ONLY")) {
        usleep((useconds_t)(quiet * 1e6));
        play(playing, dbfs);
        return 0;
    }

    const char* source = getenv("ECHO_HARNESS_SOURCE");
    sp<AudioRecord> record = new AudioRecord(source ? (audio_source_t)atoi(source) : AUDIO_SOURCE_MIC, 16000, AUDIO_FORMAT_PCM_16_BIT,
                                             AUDIO_CHANNEL_IN_MONO, String16(package));
    if (record->initCheck() != NO_ERROR) {
        fprintf(stderr, "cannot create the AudioRecord: %d\n", record->initCheck());
        return 1;
    }
    if (record->start() != NO_ERROR) {
        fprintf(stderr, "cannot start the AudioRecord\n");
        return 1;
    }

    std::thread player([&] {
        usleep((useconds_t)(quiet * 1e6));
        g_phase = 1;
        play(playing, dbfs);
        g_phase = 2;
    });

    FILE* save = nullptr;
    if (const char* path = getenv("ECHO_HARNESS_SAVE")) {
        save = fopen(path, "wb");
        if (!save) perror(path);
    }

    const double start = now_s();
    const double total = quiet + playing + after;
    std::vector<int16_t> buf(8000);  // half a second
    printf("time   phase  rms dBFS  peak dBFS\n");
    while (now_s() - start < total) {
        size_t got = 0;
        while (got < buf.size()) {
            ssize_t n = record->read(buf.data() + got, (buf.size() - got) * sizeof(int16_t));
            if (n <= 0) {
                fprintf(stderr, "read failed: %zd\n", n);
                break;
            }
            got += n / sizeof(int16_t);
        }
        if (save) fwrite(buf.data(), sizeof(int16_t), got, save);
        double sum = 0, peak = 0;
        for (size_t i = 0; i < got; i++) {
            double v = buf[i] / 32768.0;
            sum += v * v;
            if (fabs(v) > peak) peak = fabs(v);
        }
        double rms = got ? sqrt(sum / got) : 0;
        printf("%5.1f  %-5s  %8.1f  %9.1f\n", now_s() - start, kPhase[g_phase.load()],
               rms > 0 ? 20 * log10(rms) : -120.0, peak > 0 ? 20 * log10(peak) : -120.0);
        fflush(stdout);
    }
    player.join();
    record->stop();
    if (save) fclose(save);
    return 0;
}
