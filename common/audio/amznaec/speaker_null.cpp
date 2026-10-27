/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "speaker_null.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "kiss_fftr.h"

namespace {

constexpr int kN = 320;
constexpr int kHop = 160;
constexpr int kBins = kN / 2 + 1;
constexpr int kMaxMics = 4;
constexpr int kMaxRank = 3;
/* Mean square of the reference above about -50 dBFS counts as playing. */
constexpr float kRefActive = 1e-5f;
/* Per block of 10 ms: in within about 40 ms, out over about 1 s. */
constexpr float kMixAttack = 0.35f;
constexpr float kMixRelease = 0.01f;

}  // namespace

struct SpeakerNull {
    int mics = 0;
    int rank = 0;
    /* The directions, [bin][mic][k]. */
    float e_re[kBins][kMaxMics][kMaxRank] = {};
    float e_im[kBins][kMaxMics][kMaxRank] = {};
    kiss_fftr_cfg fwd = nullptr;
    kiss_fftr_cfg inv = nullptr;
    float window[kN];
    float hist[kMaxMics][kN] = {};
    float ola[kHop] = {};
    float mix = 0;
};

SpeakerNull* speaker_null_load(const char* path, int mics) {
    FILE* f = fopen(path, "r");
    if (!f) return nullptr;
    int version, file_mics, bins, rank;
    SpeakerNull* n = new SpeakerNull();
    bool ok = fscanf(f, "amznaec-null %d %d %d %d", &version, &file_mics, &bins, &rank) == 4 &&
              version == 1 && file_mics == mics && mics <= kMaxMics && bins == kBins &&
              rank >= 1 && rank <= kMaxRank;
    int lines = 0;
    while (ok) {
        int b, m, k;
        float re, im;
        int got = fscanf(f, "%d %d %d %f %f", &b, &m, &k, &re, &im);
        if (got == EOF) break;
        if (got != 5 || b < 0 || b >= kBins || m < 0 || m >= mics || k < 0 || k >= rank) {
            ok = false;
            break;
        }
        n->e_re[b][m][k] = re;
        n->e_im[b][m][k] = im;
        lines++;
    }
    fclose(f);
    if (!ok || lines != kBins * mics * rank) {
        delete n;
        return nullptr;
    }
    n->mics = mics;
    n->rank = rank;
    n->fwd = kiss_fftr_alloc(kN, 0, nullptr, nullptr);
    n->inv = kiss_fftr_alloc(kN, 1, nullptr, nullptr);
    /* Square root of a periodic Hann window, on analysis and synthesis, so
     * overlapping by half adds back to the input. */
    for (int i = 0; i < kN; i++) n->window[i] = sqrtf(0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / kN));
    return n;
}

void speaker_null_free(SpeakerNull* n) {
    if (!n) return;
    free(n->fwd);
    free(n->inv);
    delete n;
}

void speaker_null_process(SpeakerNull* n, const float* const* mics, const float* ref, float* out) {
    float power = 0;
    for (int i = 0; i < kHop; i++) power += ref[i] * ref[i];
    bool active = power / kHop > kRefActive;
    n->mix += (active ? kMixAttack : kMixRelease) * ((active ? 1.0f : 0.0f) - n->mix);

    kiss_fft_cpx spec[kMaxMics][kBins];
    float frame[kN];
    for (int m = 0; m < n->mics; m++) {
        memmove(n->hist[m], n->hist[m] + kHop, (kN - kHop) * sizeof(float));
        memcpy(n->hist[m] + kN - kHop, mics[m], kHop * sizeof(float));
        for (int i = 0; i < kN; i++) frame[i] = n->hist[m][i] * n->window[i];
        kiss_fftr(n->fwd, frame, spec[m]);
    }

    kiss_fft_cpx y[kBins];
    for (int b = 0; b < kBins; b++) {
        y[b] = spec[0][b];
        if (n->mix < 1e-3f) continue;
        /* microphone 0's part along the speakers' directions: e0 * (E^H x) */
        float p_re = 0, p_im = 0;
        for (int k = 0; k < n->rank; k++) {
            float c_re = 0, c_im = 0;
            for (int m = 0; m < n->mics; m++) {
                float er = n->e_re[b][m][k], ei = n->e_im[b][m][k];
                c_re += er * spec[m][b].r + ei * spec[m][b].i;
                c_im += er * spec[m][b].i - ei * spec[m][b].r;
            }
            float er = n->e_re[b][0][k], ei = n->e_im[b][0][k];
            p_re += er * c_re - ei * c_im;
            p_im += er * c_im + ei * c_re;
        }
        y[b].r -= n->mix * p_re;
        y[b].i -= n->mix * p_im;
    }
    kiss_fftri(n->inv, y, frame);
    for (int i = 0; i < kHop; i++) {
        out[i] = n->ola[i] + frame[i] * n->window[i] / kN;
        n->ola[i] = frame[kHop + i] * n->window[kHop + i] / kN;
    }
}

float speaker_null_mix(const SpeakerNull* n) {
    return n ? n->mix : 0.0f;
}
