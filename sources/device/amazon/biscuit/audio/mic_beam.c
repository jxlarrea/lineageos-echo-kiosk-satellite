/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * The speaker sits a few centimeters below the Echo Dot's seven microphones.
 * At high volume its sound reaches them about 20 dB above a voice across the
 * room, and much of it is distortion, which an echo canceller cannot model.
 *
 * The speaker never moves relative to the microphones, so its sound, the
 * distortion included, always arrives with the same pattern across them.
 * This removes that pattern. For each frequency it learns how the speaker
 * reaches each microphone from the codec's own copy of the speaker signal,
 * channel 8 of the microphone PCM, which shares the microphones' clock. A
 * voice is not in that signal, so talking over music cannot disturb what is
 * learned. The output is one microphone, microphone 0 (the one Amazon's HAL
 * hands Android) unless the caller picks another, minus its projection onto
 * the speaker's pattern.
 *
 * The removal fades in while the speaker plays and fades out after it stops,
 * so in a quiet room the output is that microphone unchanged: the removal
 * also takes some of a voice's low end with it.
 */

#define LOG_TAG "audio.biscuit_mic_beam"

#include "mic_beam.h"
#include "biscuit_pcm.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <cutils/properties.h>
#include <log/log.h>
#include <tinyalsa/asoundlib.h>

#define BEAM_RATE 16000
#define BEAM_CHANNELS 9
#define BEAM_MICS 7
#define BEAM_REF_CHANNEL 8
#define BEAM_N 512
#define BEAM_HOP 256
#define BEAM_BINS (BEAM_N / 2 + 1)

/* How long the learned pattern remembers, in seconds of speaker output. */
#define BEAM_LEARN_SECONDS 2.0f
/* Speaker output learned from before the removal starts: about 2.4 s. */
#define BEAM_TRAINED_HOPS 150
/* Mean square of the reference: learn above about -60 dBFS, remove above
 * about -50 dBFS. */
#define BEAM_REF_LEARN 1e-6f
#define BEAM_REF_ACTIVE 1e-5f
/* Per hop of 16 ms: in within about 60 ms, out over about 1 s. */
#define BEAM_MIX_ATTACK 0.25f
#define BEAM_MIX_RELEASE 0.015f

/* The speaker's pattern changes slowly, so it is worked out again from the
 * cross-spectra every few hops of learning, not on every hop. */
#define BEAM_PATTERN_EVERY 8

struct mic_beam_model {
    float cross_re[BEAM_BINS][BEAM_MICS];
    float cross_im[BEAM_BINS][BEAM_MICS];
    int trained;
    int since_pattern;
    /* The microphone the output comes from. */
    int out_mic;
    /* How the speaker reaches each microphone relative to the output
     * microphone, and one over its squared length; 0 where it is not
     * known. */
    float d_re[BEAM_BINS][BEAM_MICS];
    float d_im[BEAM_BINS][BEAM_MICS];
    float inv_dd[BEAM_BINS];
};

struct mic_beam {
    struct pcm* pcm;
    struct mic_beam_model* model;
    uint8_t raw[BEAM_HOP * BEAM_CHANNELS * 3];
    /* The last BEAM_N samples of each microphone, then of the reference. */
    float hist[BEAM_MICS + 1][BEAM_N];
    /* Their spectra, up to half the sample rate. */
    float re[BEAM_MICS + 1][BEAM_BINS];
    float im[BEAM_MICS + 1][BEAM_BINS];
    /* One complex transform at a time. */
    float work_re[BEAM_N];
    float work_im[BEAM_N];
    float ola[BEAM_N];
    int16_t out[BEAM_HOP];
    size_t out_pos;
    float mix;
    /* persist.biscuit.audio_levels: the levels logged about once a second. */
    bool log_levels;
    float level_ref;
    float level_mic;
    float level_out;
    int level_hops;
};

static float window[BEAM_N];
static float twiddle_re[BEAM_N / 2];
static float twiddle_im[BEAM_N / 2];
static uint16_t bit_reverse[BEAM_N];
static pthread_once_t tables_once = PTHREAD_ONCE_INIT;

static void make_tables(void) {
    int bits = 0;
    int i, b;

    while ((1 << bits) < BEAM_N) bits++;
    for (i = 0; i < BEAM_N; i++) {
        int r = 0;
        for (b = 0; b < bits; b++) {
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        }
        bit_reverse[i] = (uint16_t)r;
        /* Square root of a periodic Hann window, on analysis and synthesis:
         * the two multiply to a Hann window, which sums to one at 50%
         * overlap. */
        window[i] = sqrtf(0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / BEAM_N));
    }
    for (i = 0; i < BEAM_N / 2; i++) {
        twiddle_re[i] = cosf(2.0f * (float)M_PI * i / BEAM_N);
        twiddle_im[i] = -sinf(2.0f * (float)M_PI * i / BEAM_N);
    }
}

static void fft(float* re, float* im, bool inverse) {
    int i, size, start, k;

    for (i = 0; i < BEAM_N; i++) {
        int j = bit_reverse[i];
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (size = 2; size <= BEAM_N; size <<= 1) {
        int half = size / 2;
        int step = BEAM_N / size;
        for (start = 0; start < BEAM_N; start += size) {
            for (k = 0; k < half; k++) {
                float wr = twiddle_re[k * step];
                float wi = inverse ? -twiddle_im[k * step] : twiddle_im[k * step];
                int a = start + k;
                int b = a + half;
                float tr = re[b] * wr - im[b] * wi;
                float ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}

/*
 * The spectra of every microphone and the reference. The signals are real, so
 * each transform carries two of them, one as the real part and one as the
 * imaginary part, and their spectra are separated afterwards: four transforms
 * for eight channels.
 */
static void transform_all(struct mic_beam* beam) {
    float* zr = beam->work_re;
    float* zi = beam->work_im;
    int c, n, k;

    for (c = 0; c <= BEAM_MICS; c += 2) {
        const float* a = beam->hist[c];
        const float* b = beam->hist[c + 1];
        for (n = 0; n < BEAM_N; n++) {
            zr[n] = a[n] * window[n];
            zi[n] = b[n] * window[n];
        }
        fft(zr, zi, false);
        for (k = 0; k < BEAM_BINS; k++) {
            int m = (BEAM_N - k) & (BEAM_N - 1);
            beam->re[c][k] = 0.5f * (zr[k] + zr[m]);
            beam->im[c][k] = 0.5f * (zi[k] - zi[m]);
            beam->re[c + 1][k] = 0.5f * (zi[k] + zi[m]);
            beam->im[c + 1][k] = -0.5f * (zr[k] - zr[m]);
        }
    }
}

static void update_pattern(struct mic_beam_model* model) {
    const int out = model->out_mic;
    int k, c;

    for (k = 0; k < BEAM_BINS; k++) {
        float cor = model->cross_re[k][out];
        float coi = model->cross_im[k][out];
        float no = cor * cor + coi * coi;
        float dd = 0.0f;

        model->inv_dd[k] = 0.0f;
        if (!(no > 1e-24f)) continue;
        for (c = 0; c < BEAM_MICS; c++) {
            /* d = cross_c / cross_out */
            float cr = model->cross_re[k][c];
            float ci = model->cross_im[k][c];
            float dr = (cr * cor + ci * coi) / no;
            float di = (ci * cor - cr * coi) / no;
            model->d_re[k][c] = dr;
            model->d_im[k][c] = di;
            dd += dr * dr + di * di;
        }
        if (dd >= 1.0f && dd < 1e30f) model->inv_dd[k] = 1.0f / dd;
    }
}

static float decode_s24(const uint8_t* p) {
    int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
    if (v & 0x800000) v -= 1 << 24;
    return v / 8388608.0f;
}

static void beam_hop(struct mic_beam* beam) {
    struct mic_beam_model* model = beam->model;
    float frame[BEAM_N];
    float ref_power = 0.0f;
    float target;
    int c, n, k;

    for (c = 0; c <= BEAM_MICS; c++) {
        int channel = c < BEAM_MICS ? c : BEAM_REF_CHANNEL;
        float* h = beam->hist[c];
        memmove(h, h + BEAM_HOP, (BEAM_N - BEAM_HOP) * sizeof(float));
        for (n = 0; n < BEAM_HOP; n++) {
            h[BEAM_N - BEAM_HOP + n] =
                    decode_s24(&beam->raw[(n * BEAM_CHANNELS + channel) * 3]);
        }
    }
    for (n = BEAM_N - BEAM_HOP; n < BEAM_N; n++) {
        float v = beam->hist[BEAM_MICS][n];
        ref_power += v * v;
    }
    ref_power /= BEAM_HOP;
    if (beam->log_levels) {
        float mic_power = 0.0f;
        for (n = BEAM_N - BEAM_HOP; n < BEAM_N; n++) {
            float v = beam->hist[model->out_mic][n];
            mic_power += v * v;
        }
        beam->level_ref += ref_power;
        beam->level_mic += mic_power / BEAM_HOP;
    }

    /* The removal can only start while the reference plays, which is when
     * learning transforms them anyway. */
    if (ref_power > BEAM_REF_LEARN || beam->mix >= 0.001f) {
        transform_all(beam);
    }

    if (ref_power > BEAM_REF_LEARN) {
        const float keep = expf(-(float)BEAM_HOP / (BEAM_RATE * BEAM_LEARN_SECONDS));
        for (k = 0; k < BEAM_BINS; k++) {
            float rr = beam->re[BEAM_MICS][k];
            float ri = beam->im[BEAM_MICS][k];
            for (c = 0; c < BEAM_MICS; c++) {
                float xr = beam->re[c][k];
                float xi = beam->im[c][k];
                /* The microphone times the conjugate of the reference. */
                model->cross_re[k][c] = keep * model->cross_re[k][c] +
                        (1.0f - keep) * (xr * rr + xi * ri);
                model->cross_im[k][c] = keep * model->cross_im[k][c] +
                        (1.0f - keep) * (xi * rr - xr * ri);
            }
        }
        if (model->trained < BEAM_TRAINED_HOPS) model->trained++;
        if (++model->since_pattern >= BEAM_PATTERN_EVERY) {
            model->since_pattern = 0;
            update_pattern(model);
        }
    }

    target = model->trained >= BEAM_TRAINED_HOPS && ref_power > BEAM_REF_ACTIVE ? 1.0f : 0.0f;
    beam->mix += (target - beam->mix) *
            (target > beam->mix ? BEAM_MIX_ATTACK : BEAM_MIX_RELEASE);

    if (beam->mix < 0.001f) {
        /* The output microphone as it is, through the same windows. */
        for (n = 0; n < BEAM_N; n++) {
            frame[n] = beam->hist[model->out_mic][n] * window[n] * window[n];
        }
    } else {
        float* yr = beam->work_re;
        float* yi = beam->work_im;

        for (k = 0; k < BEAM_BINS; k++) {
            float x0r = beam->re[model->out_mic][k];
            float x0i = beam->im[model->out_mic][k];
            float scale = beam->mix * model->inv_dd[k];
            float pr = 0.0f, pi = 0.0f;

            if (scale == 0.0f) {
                yr[k] = x0r;
                yi[k] = x0i;
                continue;
            }
            for (c = 0; c < BEAM_MICS; c++) {
                float dr = model->d_re[k][c];
                float di = model->d_im[k][c];
                float xr = beam->re[c][k];
                float xi = beam->im[c][k];
                /* conj(d) times x */
                pr += dr * xr + di * xi;
                pi += dr * xi - di * xr;
            }
            yr[k] = x0r - scale * pr;
            yi[k] = x0i - scale * pi;
        }
        for (k = 1; k < BEAM_N / 2; k++) {
            yr[BEAM_N - k] = yr[k];
            yi[BEAM_N - k] = -yi[k];
        }
        yi[0] = 0.0f;
        yi[BEAM_N / 2] = 0.0f;
        fft(yr, yi, true);
        for (n = 0; n < BEAM_N; n++) {
            frame[n] = yr[n] / BEAM_N * window[n];
        }
    }

    for (n = 0; n < BEAM_N; n++) beam->ola[n] += frame[n];
    for (n = 0; n < BEAM_HOP; n++) {
        float y = beam->ola[n] * 32768.0f;
        if (y > 32767.0f) y = 32767.0f;
        if (y < -32768.0f) y = -32768.0f;
        beam->out[n] = (int16_t)lrintf(y);
    }
    memmove(beam->ola, beam->ola + BEAM_HOP, (BEAM_N - BEAM_HOP) * sizeof(float));
    memset(beam->ola + BEAM_N - BEAM_HOP, 0, BEAM_HOP * sizeof(float));
    beam->out_pos = 0;

    if (beam->log_levels) {
        float out_power = 0.0f;
        for (n = 0; n < BEAM_HOP; n++) {
            float y = beam->out[n] / 32768.0f;
            out_power += y * y;
        }
        beam->level_out += out_power / BEAM_HOP;
        if (++beam->level_hops == BEAM_RATE / BEAM_HOP) {
            ALOGI("levels: speaker reference %.1f dBFS, microphone %d %.1f dBFS, output %.1f dBFS, "
                  "removal %.2f",
                  10.0f * log10f(beam->level_ref / beam->level_hops + 1e-12f), model->out_mic,
                  10.0f * log10f(beam->level_mic / beam->level_hops + 1e-12f),
                  10.0f * log10f(beam->level_out / beam->level_hops + 1e-12f), beam->mix);
            beam->level_ref = beam->level_mic = beam->level_out = 0.0f;
            beam->level_hops = 0;
        }
    }
}

struct mic_beam_model* mic_beam_model_create(void) {
    pthread_once(&tables_once, make_tables);
    return calloc(1, sizeof(struct mic_beam_model));
}

void mic_beam_model_free(struct mic_beam_model* model) {
    free(model);
}

struct mic_beam* mic_beam_open(struct mic_beam_model* model, int out_mic) {
    struct pcm_config config = {
        .channels = BEAM_CHANNELS,
        .rate = BEAM_RATE,
        .period_size = BEAM_HOP,
        .period_count = 4,
        .format = PCM_FORMAT_S24_3LE,
    };
    struct mic_beam* beam;
    int device;

    if (!model) return NULL;
    beam = calloc(1, sizeof(*beam));
    if (!beam) return NULL;
    device = biscuit_find_pcm(BISCUIT_MIC_PCM_NAME, true);
    if (device < 0) {
        ALOGW("there is no \"%s\" PCM", BISCUIT_MIC_PCM_NAME);
        free(beam);
        return NULL;
    }
    beam->pcm = pcm_open(BISCUIT_CARD, device, PCM_IN, &config);
    if (!beam->pcm || !pcm_is_ready(beam->pcm)) {
        ALOGW("cannot open the microphone PCM: %s",
              beam->pcm ? pcm_get_error(beam->pcm) : "no memory");
        if (beam->pcm) pcm_close(beam->pcm);
        free(beam);
        return NULL;
    }
    if (out_mic < 0 || out_mic >= BEAM_MICS) {
        ALOGW("there is no microphone %d, using microphone 0", out_mic);
        out_mic = 0;
    }
    /* What is learned does not depend on the output microphone, only the
     * pattern measured against it, so a new pick keeps the training. The
     * PCM is open, so no other session is updating the model. */
    if (model->out_mic != out_mic) {
        model->out_mic = out_mic;
        update_pattern(model);
    }
    ALOGI("output microphone %d", out_mic);
    beam->model = model;
    beam->out_pos = BEAM_HOP;
    beam->log_levels = property_get_bool("persist.biscuit.audio_levels", false);
    return beam;
}

ssize_t mic_beam_read(struct mic_beam* beam, int16_t* out, size_t frames) {
    size_t done = 0;

    while (done < frames) {
        size_t n;
        if (beam->out_pos >= BEAM_HOP) {
            if (pcm_read(beam->pcm, beam->raw, sizeof(beam->raw)) != 0) {
                ALOGE("microphone PCM read failed: %s", pcm_get_error(beam->pcm));
                return -EIO;
            }
            beam_hop(beam);
        }
        n = BEAM_HOP - beam->out_pos;
        if (n > frames - done) n = frames - done;
        memcpy(out + done, beam->out + beam->out_pos, n * sizeof(int16_t));
        beam->out_pos += n;
        done += n;
    }
    return (ssize_t)done;
}

void mic_beam_close(struct mic_beam* beam) {
    if (!beam) return;
    pcm_close(beam->pcm);
    free(beam);
}
