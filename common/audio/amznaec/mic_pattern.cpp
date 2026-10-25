/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mic_pattern.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include <vector>

namespace {

constexpr int kRate = 16000;
constexpr int kN = 512;
constexpr int kHop = 256;
constexpr int kBins = kN / 2 + 1;
constexpr int kMaxMics = 4;
/* The linear canceller's output, the microphones and the reference. */
constexpr int kMaxSignals = kMaxMics + 2;

/* How long the learned pattern remembers, in seconds of speaker output. */
constexpr float kLearnSeconds = 2.0f;
/* Speaker output learned from before the removal starts: about 2.4 s. */
constexpr int kTrainedHops = 150;
/* Mean square of the reference: learn above about -60 dBFS, remove above
 * about -50 dBFS. */
constexpr float kRefLearn = 1e-6f;
constexpr float kRefActive = 1e-5f;
/* Per hop of 16 ms: in within about 60 ms, out over about 1 s. */
constexpr float kMixAttack = 0.25f;
constexpr float kMixRelease = 0.015f;
/* The pattern changes slowly, so it is worked out again from the
 * cross-spectra every few hops of learning. */
constexpr int kPatternEvery = 8;

/* How much the pattern removal lowers a voice, by frequency, measured on an
 * Echo Show 5 (2nd gen) over music with a person a couple of meters in front:
 * the boost that puts it back at the linear canceller's level. The two
 * microphones are close, so the low frequencies lose the most. */
const struct {
    float hz, db;
} kBoost[] = {
    {800, 19.4f}, {1000, 17.6f}, {1250, 15.6f}, {1500, 14.7f}, {2000, 11.9f},
    {2500, 11.2f}, {4000, 11.3f}, {5000, 6.1f}, {6000, 5.1f}, {8000, 5.0f},
};

float window[kN];
float twiddle_re[kN / 2];
float twiddle_im[kN / 2];
uint16_t bit_reverse[kN];
pthread_once_t tables_once = PTHREAD_ONCE_INIT;

void make_tables() {
    int bits = 0;
    while ((1 << bits) < kN) bits++;
    for (int i = 0; i < kN; i++) {
        int r = 0;
        for (int b = 0; b < bits; b++) {
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        }
        bit_reverse[i] = (uint16_t)r;
        /* Square root of a periodic Hann window, on analysis and synthesis:
         * the two multiply to a Hann window, which sums to one at 50%
         * overlap. */
        window[i] = sqrtf(0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / kN));
    }
    for (int i = 0; i < kN / 2; i++) {
        twiddle_re[i] = cosf(2.0f * (float)M_PI * i / kN);
        twiddle_im[i] = -sinf(2.0f * (float)M_PI * i / kN);
    }
}

void fft(float* re, float* im, bool inverse) {
    for (int i = 0; i < kN; i++) {
        int j = bit_reverse[i];
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int size = 2; size <= kN; size <<= 1) {
        int half = size / 2;
        int step = kN / size;
        for (int start = 0; start < kN; start += size) {
            for (int k = 0; k < half; k++) {
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

float boost_db(float hz) {
    const int n = sizeof(kBoost) / sizeof(kBoost[0]);
    if (hz <= kBoost[0].hz) return kBoost[0].db;
    for (int i = 1; i < n; i++) {
        if (hz <= kBoost[i].hz) {
            float t = (hz - kBoost[i - 1].hz) / (kBoost[i].hz - kBoost[i - 1].hz);
            return kBoost[i - 1].db + t * (kBoost[i].db - kBoost[i - 1].db);
        }
    }
    return kBoost[n - 1].db;
}

}  // namespace

struct MicPattern {
    int mics;
    int signals;            /* linear, mics, reference */
    /* The last kN samples of each signal and their spectra. */
    float hist[kMaxSignals][kN];
    float re[kMaxSignals][kBins];
    float im[kMaxSignals][kBins];
    /* Each microphone times the conjugate of the reference, averaged. */
    float cross_re[kBins][kMaxMics];
    float cross_im[kBins][kMaxMics];
    /* How the speaker reaches each microphone relative to microphone 0, and
     * one over its squared length; 0 where it is not known. */
    float d_re[kBins][kMaxMics];
    float d_im[kBins][kMaxMics];
    float inv_dd[kBins];
    /* Per bin: how much of the pattern removal goes into the output, and its
     * boost. */
    float weight[kBins];
    float gain[kBins];
    int trained;
    int since_pattern;
    float mix;
    float work_re[kN];
    float work_im[kN];
    float ola[kN];
    /* Input waiting for a full hop, and output waiting to be taken. */
    float pending[kMaxSignals][kHop];
    int pending_n;
    std::vector<float> out;
};

namespace {

/* The spectra of every signal, two per transform: one as the real part and
 * one as the imaginary part, separated afterwards. */
void transform_all(MicPattern* p) {
    float* zr = p->work_re;
    float* zi = p->work_im;
    for (int c = 0; c < p->signals; c += 2) {
        const float* a = p->hist[c];
        const float* b = c + 1 < p->signals ? p->hist[c + 1] : nullptr;
        for (int n = 0; n < kN; n++) {
            zr[n] = a[n] * window[n];
            zi[n] = b ? b[n] * window[n] : 0.0f;
        }
        fft(zr, zi, false);
        for (int k = 0; k < kBins; k++) {
            int m = (kN - k) & (kN - 1);
            p->re[c][k] = 0.5f * (zr[k] + zr[m]);
            p->im[c][k] = 0.5f * (zi[k] - zi[m]);
            if (b) {
                p->re[c + 1][k] = 0.5f * (zi[k] + zi[m]);
                p->im[c + 1][k] = -0.5f * (zr[k] - zr[m]);
            }
        }
    }
}

void update_pattern(MicPattern* p) {
    for (int k = 0; k < kBins; k++) {
        float cor = p->cross_re[k][0];
        float coi = p->cross_im[k][0];
        float no = cor * cor + coi * coi;
        float dd = 0.0f;
        p->inv_dd[k] = 0.0f;
        if (!(no > 1e-24f)) continue;
        for (int c = 0; c < p->mics; c++) {
            /* d = cross_c / cross_0 */
            float cr = p->cross_re[k][c];
            float ci = p->cross_im[k][c];
            float dr = (cr * cor + ci * coi) / no;
            float di = (ci * cor - cr * coi) / no;
            p->d_re[k][c] = dr;
            p->d_im[k][c] = di;
            dd += dr * dr + di * di;
        }
        if (dd >= 1.0f && dd < 1e30f) p->inv_dd[k] = 1.0f / dd;
    }
}

void hop(MicPattern* p) {
    const int lin = 0, ref = p->signals - 1;
    float ref_power = 0.0f;

    for (int c = 0; c < p->signals; c++) {
        memmove(p->hist[c], p->hist[c] + kHop, (kN - kHop) * sizeof(float));
        memcpy(p->hist[c] + kN - kHop, p->pending[c], kHop * sizeof(float));
    }
    for (int n = kN - kHop; n < kN; n++) ref_power += p->hist[ref][n] * p->hist[ref][n];
    ref_power /= kHop;

    transform_all(p);

    if (ref_power > kRefLearn) {
        const float keep = expf(-(float)kHop / (kRate * kLearnSeconds));
        for (int k = 0; k < kBins; k++) {
            float rr = p->re[ref][k], ri = p->im[ref][k];
            for (int c = 0; c < p->mics; c++) {
                float xr = p->re[1 + c][k], xi = p->im[1 + c][k];
                p->cross_re[k][c] = keep * p->cross_re[k][c] + (1.0f - keep) * (xr * rr + xi * ri);
                p->cross_im[k][c] = keep * p->cross_im[k][c] + (1.0f - keep) * (xi * rr - xr * ri);
            }
        }
        if (p->trained < kTrainedHops) p->trained++;
        if (++p->since_pattern >= kPatternEvery) {
            p->since_pattern = 0;
            update_pattern(p);
        }
    }

    float target = p->trained >= kTrainedHops && ref_power > kRefActive ? 1.0f : 0.0f;
    p->mix += (target - p->mix) * (target > p->mix ? kMixAttack : kMixRelease);

    float* yr = p->work_re;
    float* yi = p->work_im;
    for (int k = 0; k < kBins; k++) {
        float sr = p->re[lin][k], si = p->im[lin][k];
        float w = p->mix * p->weight[k];
        yr[k] = sr;
        yi[k] = si;
        if (w < 0.001f || p->inv_dd[k] == 0.0f) continue;
        float pr = 0.0f, pi = 0.0f;
        for (int c = 0; c < p->mics; c++) {
            float dr = p->d_re[k][c], di = p->d_im[k][c];
            float xr = p->re[1 + c][k], xi = p->im[1 + c][k];
            /* conj(d) times x */
            pr += dr * xr + di * xi;
            pi += dr * xi - di * xr;
        }
        float br = (p->re[1][k] - p->inv_dd[k] * pr) * p->gain[k];
        float bi = (p->im[1][k] - p->inv_dd[k] * pi) * p->gain[k];
        yr[k] = sr + w * (br - sr);
        yi[k] = si + w * (bi - si);
    }
    for (int k = 1; k < kN / 2; k++) {
        yr[kN - k] = yr[k];
        yi[kN - k] = -yi[k];
    }
    yi[0] = 0.0f;
    yi[kN / 2] = 0.0f;
    fft(yr, yi, true);
    for (int n = 0; n < kN; n++) p->ola[n] += yr[n] / kN * window[n];
    p->out.insert(p->out.end(), p->ola, p->ola + kHop);
    memmove(p->ola, p->ola + kHop, (kN - kHop) * sizeof(float));
    memset(p->ola + kN - kHop, 0, kHop * sizeof(float));
}

}  // namespace

MicPattern* mic_pattern_create(int mics, int split_hz) {
    if (mics < 2 || mics > kMaxMics) return nullptr;
    pthread_once(&tables_once, make_tables);
    MicPattern* p = new MicPattern();
    memset(p->hist, 0, sizeof(p->hist));
    memset(p->cross_re, 0, sizeof(p->cross_re));
    memset(p->cross_im, 0, sizeof(p->cross_im));
    memset(p->d_re, 0, sizeof(p->d_re));
    memset(p->d_im, 0, sizeof(p->d_im));
    memset(p->inv_dd, 0, sizeof(p->inv_dd));
    memset(p->ola, 0, sizeof(p->ola));
    p->mics = mics;
    p->signals = mics + 2;
    p->trained = 0;
    p->since_pattern = 0;
    p->mix = 0.0f;
    p->pending_n = 0;
    /* Crossfaded from a fifth below the split to a fifth above it. */
    const float lo = split_hz * 0.8f, hi = split_hz * 1.2f;
    for (int k = 0; k < kBins; k++) {
        float hz = (float)k * kRate / kN;
        p->weight[k] = hz <= lo ? 0.0f : hz >= hi ? 1.0f : (hz - lo) / (hi - lo);
        p->gain[k] = powf(10.0f, boost_db(hz) / 20.0f);
    }
    /* A hop of silence ahead, so a block of output is always ready. */
    p->out.assign(kHop, 0.0f);
    return p;
}

void mic_pattern_free(MicPattern* p) {
    delete p;
}

void mic_pattern_process(MicPattern* p, const float* linear, const float* const* mics,
                         const float* ref, int n, float* out) {
    for (int i = 0; i < n; i++) {
        p->pending[0][p->pending_n] = linear[i];
        for (int c = 0; c < p->mics; c++) p->pending[1 + c][p->pending_n] = mics[c][i];
        p->pending[p->signals - 1][p->pending_n] = ref[i];
        if (++p->pending_n == kHop) {
            hop(p);
            p->pending_n = 0;
        }
    }
    /* By construction at least n samples are ready: a hop of silence ahead
     * and every hop adds as many as it took in. */
    int ready = (int)p->out.size() < n ? (int)p->out.size() : n;
    memcpy(out, p->out.data(), ready * sizeof(float));
    for (int i = ready; i < n; i++) out[i] = 0.0f;
    p->out.erase(p->out.begin(), p->out.begin() + ready);
}

float mic_pattern_mix(const MicPattern* p) {
    return p->mix;
}
