/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Symbols the Echo Dot's Fire OS 6 audio libraries need from Android 7.1
 * that Android 11 no longer has. scripts/extract-biscuit-blobs.sh adds this
 * library to the blobs that use them.
 */

#include <stdint.h>
#include <time.h>

#include <tinyalsa/asoundlib.h>

namespace android {

/* libutils dropped LinearTransform with the common time service. The Fire
 * OS HAL and libcommon_time_client still reduce fractions with it. */
struct LinearTransform {
    template <class T>
    static void reduce(T* N, T* D);
};

template <>
void LinearTransform::reduce<uint64_t>(uint64_t* N, uint64_t* D) {
    uint64_t a, b;

    if (!N || !D || !*D) return;
    if (*N == 0) {
        *D = 1;
        return;
    }
    /* Euclid's algorithm for the greatest common divisor. */
    a = *N > *D ? *N : *D;
    b = *N > *D ? *D : *N;
    while (a % b) {
        uint64_t r = a % b;
        a = b;
        b = r;
    }
    *N /= b;
    *D /= b;
}

}  // namespace android

extern "C" {

/* Amazon's TinyALSA had this. The current hardware timestamp served the HAL
 * well on the CM14 image. */
int pcm_get_trigger_tstamp(struct pcm* pcm, struct timespec* tstamp) {
    unsigned int avail;

    return pcm_get_htimestamp(pcm, &avail, tstamp);
}

/* libacehal_log flushes the log through a liblog call that is gone. */
void android_logger_flush(void) {}

/*
 * Amazon's audio processor. The Fire OS HAL runs playback and its own
 * captures through libasp pipelines, which it skips when it gets none. The
 * CM14 image never had them, and the wake word worked over music at 100%.
 * With them the speaker played quieter through Amazon's dynamics. This
 * library comes before libasp in the HAL's libraries, so this is the
 * asp_create_pipeline the HAL calls.
 */
void* asp_create_pipeline(void) {
    return nullptr;
}

}
