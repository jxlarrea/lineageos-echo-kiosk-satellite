/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The Echo Dot's primary audio HAL. It loads the Fire OS 6 HAL
 * (audio.primary_amazon.mt8163), which drives the codecs, and adds what the
 * Dot needs around it, as the CM14 image did:
 *
 *  - Android 11 structures in front of the Fire OS HAL's Android 7.1 ones
 *    (fireos_audio.h).
 *  - The speaker route and amplifier, which the Fire OS HAL leaves to
 *    Amazon's audio processor. That processor never runs here.
 *  - Optional speaker tuning: a high-pass filter and a peak limiter.
 *  - The voice capture from the microphone array with the speaker removed
 *    (mic_beam.c).
 */

#define LOG_TAG "audio.biscuit"

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cutils/properties.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <log/log.h>
#include <tinyalsa/asoundlib.h>

#include "biscuit_pcm.h"
#include "fireos_audio.h"
#include "mic_beam.h"

_Static_assert(offsetof(struct fireos_port_config, ext) == offsetof(struct audio_port_config, flags),
               "fireos_port_config must match audio_port_config up to flags");

struct biscuit_device {
    struct audio_hw_device device;
    struct fireos_device* fireos;
    bool mic_muted;
    struct mic_beam_model* beam_model;
    /* The Fire OS HAL has opened the microphones once, see in_read(). */
    bool fireos_capture_ready;
};

static struct fireos_device* fireos_dev(const struct audio_hw_device* dev) {
    return ((const struct biscuit_device*)dev)->fireos;
}

static void to_fireos_config(const struct audio_config* config, struct fireos_config* f) {
    const audio_offload_info_t* info = &config->offload_info;

    memset(f, 0, sizeof(*f));
    f->sample_rate = config->sample_rate;
    f->channel_mask = config->channel_mask;
    f->format = config->format;
    f->frame_count = config->frame_count;
    f->offload_info.version = AUDIO_MAKE_OFFLOAD_INFO_VERSION(0, 1);
    f->offload_info.size = sizeof(f->offload_info);
    f->offload_info.sample_rate = info->sample_rate;
    f->offload_info.channel_mask = info->channel_mask;
    f->offload_info.format = info->format;
    f->offload_info.stream_type = info->stream_type;
    f->offload_info.bit_rate = info->bit_rate;
    f->offload_info.duration_us = info->duration_us;
    f->offload_info.has_video = info->has_video;
    f->offload_info.is_streaming = info->is_streaming;
    f->offload_info.bit_width = info->bit_width;
    f->offload_info.offload_buffer_size = info->offload_buffer_size;
    f->offload_info.usage = info->usage;
}

/* The Fire OS HAL suggests a config here when it rejects one. */
static void from_fireos_config(const struct fireos_config* f, struct audio_config* config) {
    config->sample_rate = f->sample_rate;
    config->channel_mask = f->channel_mask;
    config->format = f->format;
    config->frame_count = f->frame_count;
}

static void to_fireos_port_config(const struct audio_port_config* config,
                                  struct fireos_port_config* f) {
    memcpy(f, config, offsetof(struct fireos_port_config, ext));
    memcpy(&f->ext, &config->ext, sizeof(f->ext));
}

/* Mixer controls ------------------------------------------------------- */

static int set_ctl_enum(struct mixer* mixer, const char* name, const char* value, int fallback) {
    struct mixer_ctl* ctl = mixer_get_ctl_by_name(mixer, name);
    unsigned int i;
    int ret;

    if (!ctl) {
        ALOGW("missing mixer control: %s", name);
        return -ENOENT;
    }
    ret = mixer_ctl_set_enum_by_string(ctl, value);
    if (ret == 0) return 0;
    for (i = 0; i < mixer_ctl_get_num_values(ctl); i++) {
        ret = mixer_ctl_set_value(ctl, i, fallback);
        if (ret != 0) ALOGW("failed mixer control %s[%u]=%d", name, i, fallback);
    }
    return ret;
}

static int set_ctl_values(struct mixer* mixer, const char* name, int left, int right) {
    struct mixer_ctl* ctl = mixer_get_ctl_by_name(mixer, name);
    unsigned int n;
    int ret = 0;

    if (!ctl) {
        ALOGW("missing mixer control: %s", name);
        return -ENOENT;
    }
    n = mixer_ctl_get_num_values(ctl);
    if (n > 0) ret = mixer_ctl_set_value(ctl, 0, left);
    if (n > 1) ret |= mixer_ctl_set_value(ctl, 1, right);
    if (ret != 0) ALOGW("failed mixer control %s=%d,%d", name, left, right);
    return ret;
}

/*
 * The TLV320DAC3203's headphone outputs drive the speaker amplifier. Fire OS
 * sets their driver to 0 dB, which played too quietly here, and +6 dB
 * distorted even at moderate volume, so the Dot plays at +3 dB, chosen by
 * ear. persist.biscuit.spk_gain_db changes it, from -6 to 29 dB, within a
 * second while something plays.
 */
#define SPK_GAIN_DEFAULT_DB 3
#define SPK_GAIN_MIN_DB (-6)
#define SPK_GAIN_MAX_DB 29

static void apply_speaker_route(void) {
    struct mixer* mixer = mixer_open(BISCUIT_CARD);

    if (!mixer) {
        ALOGE("mixer_open(%d) failed", BISCUIT_CARD);
        return;
    }
    set_ctl_enum(mixer, "HPL Output Mixer L_DAC Switch", "On", 1);
    set_ctl_enum(mixer, "HPR Output Mixer R_DAC Switch", "On", 1);
    set_ctl_enum(mixer, "Audio_DacMux_Setting", "Off", 0);
    set_ctl_enum(mixer, "Right Channel Only", "On", 1);
    set_ctl_enum(mixer, "MFP Gpio Mute", "Off", 0);
    mixer_close(mixer);
}

static int speaker_gain_db(void) {
    int gain = property_get_int32("persist.biscuit.spk_gain_db", SPK_GAIN_DEFAULT_DB);

    if (gain < SPK_GAIN_MIN_DB) gain = SPK_GAIN_MIN_DB;
    if (gain > SPK_GAIN_MAX_DB) gain = SPK_GAIN_MAX_DB;
    return gain;
}

/* After the first write: opening the PCM applies Fire OS's speaker path,
 * which sets the driver back to 0 dB. */
static void apply_speaker_gain(int gain) {
    struct mixer* mixer = mixer_open(BISCUIT_CARD);

    if (!mixer) {
        ALOGE("mixer_open(%d) failed", BISCUIT_CARD);
        return;
    }
    /* The control counts from -6 dB in 1 dB steps. */
    set_ctl_values(mixer, "HP Driver Gain Volume", gain - SPK_GAIN_MIN_DB, gain - SPK_GAIN_MIN_DB);
    mixer_close(mixer);
}

static void set_speaker_amp(bool on) {
    struct mixer* mixer = mixer_open(BISCUIT_CARD);

    if (!mixer) {
        ALOGE("mixer_open(%d) failed", BISCUIT_CARD);
        return;
    }
    if (on) {
        set_ctl_enum(mixer, "Audio_Amp_R_Switch", "On", 1);
        set_ctl_enum(mixer, "Audio_Amp_L_Switch", "On", 1);
        set_ctl_enum(mixer, "Ext_Speaker_Amp_Switch", "On", 1);
    } else {
        set_ctl_enum(mixer, "Ext_Speaker_Amp_Switch", "Off", 0);
        set_ctl_enum(mixer, "Audio_Amp_R_Switch", "Off", 0);
        set_ctl_enum(mixer, "Audio_Amp_L_Switch", "Off", 0);
    }
    mixer_close(mixer);
}

/* Speaker tuning --------------------------------------------------------- */

/*
 * Fire OS shapes the signal for the small driver in Amazon's audio
 * processor, so the speaker gets the full range at full level. Both tools
 * here are off by default: in listening tests on CM14 the wake word did
 * slightly worse over loud music with either on, and the high frequencies
 * sounded harsher once the bass was gone.
 *
 * persist.biscuit.spk_hpf_hz sets a high-pass cutoff in Hz that removes bass
 * the driver cannot reproduce. persist.biscuit.spk_limiter 1 turns on a peak
 * limiter, which reduces distortion, but its gain changes faster than Kiosk
 * Satellite's echo canceller can follow.
 *
 * Both are read again whenever playback resumes from standby.
 */
#define SPK_MAX_CHANNELS 2
#define SPK_HPF_SECTIONS 2
#define SPK_DEFAULT_HPF_HZ 0
#define SPK_LIMIT_CEILING 0.891f /* -1 dBFS */
#define SPK_LIMIT_RELEASE_MS 100.0f

struct biquad {
    float b0, b1, b2, a1, a2;
    float z1[SPK_MAX_CHANNELS];
    float z2[SPK_MAX_CHANNELS];
};

struct speaker_tuning {
    bool hpf;
    bool limiter;
    struct biquad sections[SPK_HPF_SECTIONS];
    float gain;
    float release;
    int16_t* scratch;
    size_t scratch_bytes;
};

struct biscuit_stream_out {
    struct audio_stream_out stream;
    struct fireos_stream_out* fireos;
    bool active;
    bool amp_on;
    uint32_t rate;
    int channels;
    bool pcm16;
    struct speaker_tuning tuning;
    /* persist.biscuit.audio_levels: the level Android plays at, about once
     * a second, to compare with the speaker reference mic_beam.c logs. */
    bool log_levels;
    double level_power;
    size_t level_frames;
    int gain_db;
    size_t gain_check_frames;
};

static void biquad_highpass(struct biquad* bq, float rate, float hz, float q) {
    float w0 = 2.0f * (float)M_PI * hz / rate;
    float cosw = cosf(w0);
    float alpha = sinf(w0) / (2.0f * q);
    float a0 = 1.0f + alpha;

    bq->b0 = (1.0f + cosw) / 2.0f / a0;
    bq->b1 = -(1.0f + cosw) / a0;
    bq->b2 = bq->b0;
    bq->a1 = -2.0f * cosw / a0;
    bq->a2 = (1.0f - alpha) / a0;
    memset(bq->z1, 0, sizeof(bq->z1));
    memset(bq->z2, 0, sizeof(bq->z2));
}

static void speaker_tuning_load(struct biscuit_stream_out* out) {
    struct speaker_tuning* t = &out->tuning;
    /* The Q of each section of a 4th order Butterworth filter. */
    static const float q[SPK_HPF_SECTIONS] = {0.5412f, 1.3066f};
    int hz = property_get_int32("persist.biscuit.spk_hpf_hz", SPK_DEFAULT_HPF_HZ);
    int i;

    t->hpf = false;
    t->limiter = false;
    if (!out->pcm16 || out->channels < 1 || out->channels > SPK_MAX_CHANNELS || out->rate == 0) {
        return;
    }
    if (hz > 0 && hz < (int)out->rate / 4) {
        for (i = 0; i < SPK_HPF_SECTIONS; i++) {
            biquad_highpass(&t->sections[i], (float)out->rate, (float)hz, q[i]);
        }
        t->hpf = true;
    }
    t->limiter = property_get_bool("persist.biscuit.spk_limiter", false);
    t->gain = 1.0f;
    t->release = 1.0f - expf(-1.0f / (SPK_LIMIT_RELEASE_MS / 1000.0f * out->rate));
}

/* Returns the buffer to write: the tuned copy, or the input untouched. */
static const void* speaker_tuning_process(struct biscuit_stream_out* out, const void* buffer,
                                          size_t bytes) {
    struct speaker_tuning* t = &out->tuning;
    const int16_t* in = buffer;
    size_t frames;
    size_t f;
    int c, s;

    if (!t->hpf && !t->limiter) return buffer;
    if (bytes > t->scratch_bytes) {
        int16_t* grown = realloc(t->scratch, bytes);
        if (!grown) return buffer;
        t->scratch = grown;
        t->scratch_bytes = bytes;
    }
    frames = bytes / (sizeof(int16_t) * out->channels);
    for (f = 0; f < frames; f++) {
        float v[SPK_MAX_CHANNELS];
        float peak = 0.0f;

        for (c = 0; c < out->channels; c++) {
            float x = in[f * out->channels + c] / 32768.0f;
            if (t->hpf) {
                for (s = 0; s < SPK_HPF_SECTIONS; s++) {
                    struct biquad* bq = &t->sections[s];
                    float y = bq->b0 * x + bq->z1[c];
                    bq->z1[c] = bq->b1 * x - bq->a1 * y + bq->z2[c];
                    bq->z2[c] = bq->b2 * x - bq->a2 * y;
                    x = y;
                }
            }
            v[c] = x;
            if (fabsf(x) > peak) peak = fabsf(x);
        }
        if (t->limiter) {
            /* Gain drops at once on a peak and recovers over the release. */
            float target = peak > SPK_LIMIT_CEILING ? SPK_LIMIT_CEILING / peak : 1.0f;
            if (target < t->gain) {
                t->gain = target;
            } else {
                t->gain += (1.0f - t->gain) * t->release;
            }
        }
        for (c = 0; c < out->channels; c++) {
            float y = v[c] * (t->limiter ? t->gain : 1.0f) * 32768.0f;
            if (y > 32767.0f) y = 32767.0f;
            if (y < -32768.0f) y = -32768.0f;
            t->scratch[f * out->channels + c] = (int16_t)lrintf(y);
        }
    }
    return t->scratch;
}

/* Output streams --------------------------------------------------------- */

#define OUT(s) ((struct biscuit_stream_out*)(s))
#define OUT_C(s) ((const struct biscuit_stream_out*)(s))
#define FIREOS_OUT_COMMON(s) (&OUT(s)->fireos->common)

static uint32_t out_get_sample_rate(const struct audio_stream* stream) {
    struct audio_stream* c = &OUT_C(stream)->fireos->common;
    return c->get_sample_rate(c);
}

static int out_set_sample_rate(struct audio_stream* stream, uint32_t rate) {
    return FIREOS_OUT_COMMON(stream)->set_sample_rate(FIREOS_OUT_COMMON(stream), rate);
}

static size_t out_get_buffer_size(const struct audio_stream* stream) {
    struct audio_stream* c = &OUT_C(stream)->fireos->common;
    return c->get_buffer_size(c);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream* stream) {
    struct audio_stream* c = &OUT_C(stream)->fireos->common;
    return c->get_channels(c);
}

static audio_format_t out_get_format(const struct audio_stream* stream) {
    struct audio_stream* c = &OUT_C(stream)->fireos->common;
    return c->get_format(c);
}

static int out_set_format(struct audio_stream* stream, audio_format_t format) {
    return FIREOS_OUT_COMMON(stream)->set_format(FIREOS_OUT_COMMON(stream), format);
}

static int out_standby(struct audio_stream* stream) {
    struct biscuit_stream_out* out = OUT(stream);
    int ret = 0;

    /* The amplifier goes off before the codec, so it never plays the DAC
     * powering down. */
    if (out->amp_on) {
        set_speaker_amp(false);
        out->amp_on = false;
    }
    if (out->fireos->common.standby) ret = out->fireos->common.standby(&out->fireos->common);
    out->active = false;
    return ret;
}

static int out_dump(const struct audio_stream* stream, int fd) {
    struct audio_stream* c = &OUT_C(stream)->fireos->common;
    return c->dump ? c->dump(c, fd) : 0;
}

static audio_devices_t out_get_device(const struct audio_stream* stream) {
    struct audio_stream* c = &OUT_C(stream)->fireos->common;
    return c->get_device(c);
}

static int out_set_device(struct audio_stream* stream, audio_devices_t device) {
    return FIREOS_OUT_COMMON(stream)->set_device(FIREOS_OUT_COMMON(stream), device);
}

static int out_set_parameters(struct audio_stream* stream, const char* kv_pairs) {
    return FIREOS_OUT_COMMON(stream)->set_parameters(FIREOS_OUT_COMMON(stream), kv_pairs);
}

static char* out_get_parameters(const struct audio_stream* stream, const char* keys) {
    struct audio_stream* c = &OUT_C(stream)->fireos->common;
    return c->get_parameters(c, keys);
}

static int out_add_audio_effect(const struct audio_stream* stream, effect_handle_t effect) {
    struct audio_stream* c = &OUT_C(stream)->fireos->common;
    return c->add_audio_effect(c, effect);
}

static int out_remove_audio_effect(const struct audio_stream* stream, effect_handle_t effect) {
    struct audio_stream* c = &OUT_C(stream)->fireos->common;
    return c->remove_audio_effect(c, effect);
}

static uint32_t out_get_latency(const struct audio_stream_out* stream) {
    return OUT_C(stream)->fireos->get_latency(OUT_C(stream)->fireos);
}

static int out_set_volume(struct audio_stream_out* stream, float left, float right) {
    return OUT(stream)->fireos->set_volume(OUT(stream)->fireos, left, right);
}

static ssize_t out_write(struct audio_stream_out* stream, const void* buffer, size_t bytes) {
    struct biscuit_stream_out* out = OUT(stream);
    ssize_t ret;

    if (!out->active) {
        apply_speaker_route();
        speaker_tuning_load(out);
        out->log_levels = property_get_bool("persist.biscuit.audio_levels", false);
        out->active = true;
    }
    if (out->log_levels && out->pcm16 && out->channels > 0) {
        const int16_t* pcm = buffer;
        size_t i, samples = bytes / sizeof(int16_t);

        for (i = 0; i < samples; i++) out->level_power += (double)pcm[i] * pcm[i];
        out->level_frames += samples / out->channels;
        if (out->level_frames >= out->rate) {
            ALOGI("levels: playback %.1f dBFS",
                  10.0 * log10(out->level_power / (out->level_frames * out->channels) /
                               (32768.0 * 32768.0) + 1e-12));
            out->level_power = 0;
            out->level_frames = 0;
        }
    }
    /* The first write after standby opens the PCM, which starts I2S and
     * powers up the codec. The amplifier only comes on once that write is
     * done: a codec powering up into a live amplifier crackles. */
    ret = out->fireos->write(out->fireos, speaker_tuning_process(out, buffer, bytes), bytes);
    if (!out->amp_on) {
        out->gain_db = speaker_gain_db();
        apply_speaker_gain(out->gain_db);
        set_speaker_amp(true);
        out->amp_on = true;
        out->gain_check_frames = 0;
    } else if (out->rate && out->channels > 0) {
        /* A new persist.biscuit.spk_gain_db applies within a second. */
        out->gain_check_frames += bytes / audio_bytes_per_frame(out->channels, AUDIO_FORMAT_PCM_16_BIT);
        if (out->gain_check_frames >= out->rate) {
            int gain = speaker_gain_db();

            out->gain_check_frames = 0;
            if (gain != out->gain_db) {
                out->gain_db = gain;
                apply_speaker_gain(gain);
            }
        }
    }
    return ret;
}

static int out_get_render_position(const struct audio_stream_out* stream, uint32_t* dsp_frames) {
    return OUT_C(stream)->fireos->get_render_position(OUT_C(stream)->fireos, dsp_frames);
}

static int out_get_next_write_timestamp(const struct audio_stream_out* stream,
                                        int64_t* timestamp) {
    return OUT_C(stream)->fireos->get_next_write_timestamp(OUT_C(stream)->fireos, timestamp);
}

static int out_set_callback(struct audio_stream_out* stream, stream_callback_t callback,
                            void* cookie) {
    return OUT(stream)->fireos->set_callback(OUT(stream)->fireos, callback, cookie);
}

static int out_pause(struct audio_stream_out* stream) {
    return OUT(stream)->fireos->pause(OUT(stream)->fireos);
}

static int out_resume(struct audio_stream_out* stream) {
    return OUT(stream)->fireos->resume(OUT(stream)->fireos);
}

static int out_drain(struct audio_stream_out* stream, audio_drain_type_t type) {
    return OUT(stream)->fireos->drain(OUT(stream)->fireos, type);
}

static int out_flush(struct audio_stream_out* stream) {
    return OUT(stream)->fireos->flush(OUT(stream)->fireos);
}

static int out_get_presentation_position(const struct audio_stream_out* stream, uint64_t* frames,
                                         struct timespec* timestamp) {
    return OUT_C(stream)->fireos->get_presentation_position(OUT_C(stream)->fireos, frames,
                                                            timestamp);
}

static int adev_open_output_stream(struct audio_hw_device* dev, audio_io_handle_t handle,
                                   audio_devices_t devices, audio_output_flags_t flags,
                                   struct audio_config* config,
                                   struct audio_stream_out** stream_out, const char* address) {
    struct fireos_device* fireos = fireos_dev(dev);
    struct fireos_stream_out* f = NULL;
    struct fireos_config fconfig;
    struct biscuit_stream_out* out;
    struct audio_stream_out* s;
    int ret;

    to_fireos_config(config, &fconfig);
    ret = fireos->open_output_stream(fireos, handle, devices, flags, &fconfig, &f, address);
    from_fireos_config(&fconfig, config);
    if (ret != 0) return ret;
    if (!f) return -ENODEV;

    out = calloc(1, sizeof(*out));
    if (!out) {
        fireos->close_output_stream(fireos, f);
        return -ENOMEM;
    }
    out->fireos = f;
    out->rate = f->common.get_sample_rate(&f->common);
    out->channels = audio_channel_count_from_out_mask(f->common.get_channels(&f->common));
    out->pcm16 = f->common.get_format(&f->common) == AUDIO_FORMAT_PCM_16_BIT;

    /* Only what the Fire OS stream has. The methods Android 11 added stay
     * NULL, which tells Android the stream does not have them. */
    s = &out->stream;
    s->common.get_sample_rate = out_get_sample_rate;
    s->common.set_sample_rate = out_set_sample_rate;
    s->common.get_buffer_size = out_get_buffer_size;
    s->common.get_channels = out_get_channels;
    s->common.get_format = out_get_format;
    s->common.set_format = out_set_format;
    s->common.standby = out_standby;
    s->common.dump = out_dump;
    s->common.get_device = out_get_device;
    s->common.set_device = out_set_device;
    s->common.set_parameters = out_set_parameters;
    s->common.get_parameters = out_get_parameters;
    s->common.add_audio_effect = out_add_audio_effect;
    s->common.remove_audio_effect = out_remove_audio_effect;
    s->get_latency = out_get_latency;
    s->set_volume = f->set_volume ? out_set_volume : NULL;
    s->write = out_write;
    s->get_render_position = f->get_render_position ? out_get_render_position : NULL;
    s->get_next_write_timestamp = f->get_next_write_timestamp ? out_get_next_write_timestamp : NULL;
    s->set_callback = f->set_callback ? out_set_callback : NULL;
    s->pause = f->pause ? out_pause : NULL;
    s->resume = f->resume ? out_resume : NULL;
    s->drain = f->drain ? out_drain : NULL;
    s->flush = f->flush ? out_flush : NULL;
    s->get_presentation_position =
            f->get_presentation_position ? out_get_presentation_position : NULL;

    *stream_out = s;
    return 0;
}

static void adev_close_output_stream(struct audio_hw_device* dev, struct audio_stream_out* stream) {
    struct biscuit_stream_out* out = OUT(stream);

    out_standby(&out->stream.common);
    fireos_dev(dev)->close_output_stream(fireos_dev(dev), out->fireos);
    free(out->tuning.scratch);
    free(out);
}

/* Input streams ---------------------------------------------------------- */

/*
 * The voice capture Android opens, 16 kHz mono from the built-in
 * microphones, reads the microphone array itself through mic_beam.c, which
 * removes the speaker from it. It hands Android microphone 0 unless
 * persist.biscuit.mic_beam_out picks another (0 to 6), for a Dot whose
 * microphone 0 is weak. Every other capture, and this one when
 * persist.biscuit.mic_beam is 0 or the array cannot be opened, goes to the
 * Fire OS HAL, which hands Android microphone 0 unprocessed.
 */
struct biscuit_stream_in {
    struct audio_stream_in stream;
    struct fireos_stream_in* fireos;
    struct biscuit_device* adev;
    bool beam_capable;
    bool beam_off;
    struct mic_beam* beam;
    uint64_t beam_frames;
};

#define IN(s) ((struct biscuit_stream_in*)(s))
#define IN_C(s) ((const struct biscuit_stream_in*)(s))
#define FIREOS_IN_COMMON(s) (&IN(s)->fireos->common)

static void in_close_beam(struct biscuit_stream_in* in) {
    if (in->beam) {
        mic_beam_close(in->beam);
        in->beam = NULL;
    }
}

static uint32_t in_get_sample_rate(const struct audio_stream* stream) {
    struct audio_stream* c = &IN_C(stream)->fireos->common;
    return c->get_sample_rate(c);
}

static int in_set_sample_rate(struct audio_stream* stream, uint32_t rate) {
    return FIREOS_IN_COMMON(stream)->set_sample_rate(FIREOS_IN_COMMON(stream), rate);
}

static size_t in_get_buffer_size(const struct audio_stream* stream) {
    struct audio_stream* c = &IN_C(stream)->fireos->common;
    return c->get_buffer_size(c);
}

static audio_channel_mask_t in_get_channels(const struct audio_stream* stream) {
    struct audio_stream* c = &IN_C(stream)->fireos->common;
    return c->get_channels(c);
}

static audio_format_t in_get_format(const struct audio_stream* stream) {
    struct audio_stream* c = &IN_C(stream)->fireos->common;
    return c->get_format(c);
}

static int in_set_format(struct audio_stream* stream, audio_format_t format) {
    return FIREOS_IN_COMMON(stream)->set_format(FIREOS_IN_COMMON(stream), format);
}

static int in_standby(struct audio_stream* stream) {
    struct biscuit_stream_in* in = IN(stream);

    in_close_beam(in);
    in->beam_off = false;
    return in->fireos->common.standby(&in->fireos->common);
}

static int in_dump(const struct audio_stream* stream, int fd) {
    struct audio_stream* c = &IN_C(stream)->fireos->common;
    return c->dump ? c->dump(c, fd) : 0;
}

static audio_devices_t in_get_device(const struct audio_stream* stream) {
    struct audio_stream* c = &IN_C(stream)->fireos->common;
    return c->get_device(c);
}

static int in_set_device(struct audio_stream* stream, audio_devices_t device) {
    return FIREOS_IN_COMMON(stream)->set_device(FIREOS_IN_COMMON(stream), device);
}

static int in_set_parameters(struct audio_stream* stream, const char* kv_pairs) {
    return FIREOS_IN_COMMON(stream)->set_parameters(FIREOS_IN_COMMON(stream), kv_pairs);
}

static char* in_get_parameters(const struct audio_stream* stream, const char* keys) {
    struct audio_stream* c = &IN_C(stream)->fireos->common;
    return c->get_parameters(c, keys);
}

static int in_add_audio_effect(const struct audio_stream* stream, effect_handle_t effect) {
    struct audio_stream* c = &IN_C(stream)->fireos->common;
    return c->add_audio_effect(c, effect);
}

static int in_remove_audio_effect(const struct audio_stream* stream, effect_handle_t effect) {
    struct audio_stream* c = &IN_C(stream)->fireos->common;
    return c->remove_audio_effect(c, effect);
}

static int in_set_gain(struct audio_stream_in* stream, float gain) {
    return IN(stream)->fireos->set_gain(IN(stream)->fireos, gain);
}

static ssize_t in_read(struct audio_stream_in* stream, void* buffer, size_t bytes) {
    struct biscuit_stream_in* in = IN(stream);

    if (in->beam_capable && !in->beam && !in->beam_off) {
        if (property_get_bool("persist.biscuit.mic_beam", true)) {
            if (!in->adev->fireos_capture_ready) {
                /* The Fire OS HAL applies its mixer defaults, the microphone
                 * inputs and preamp among them, when it first opens a
                 * stream. Until then the ADCs deliver silence, so it opens
                 * the microphones once, for one buffer, before the array is
                 * read. */
                in->fireos->read(in->fireos, buffer, bytes);
                in->fireos->common.standby(&in->fireos->common);
                in->adev->fireos_capture_ready = true;
            }
            in->beam = mic_beam_open(in->adev->beam_model,
                                     property_get_int32("persist.biscuit.mic_beam_out", 0));
            if (in->beam) {
                ALOGI("reading the microphone array: the speaker is removed while it plays");
            }
        }
        /* Off, or the array would not open: the Fire OS HAL until standby. */
        if (!in->beam) in->beam_off = true;
    }
    if (in->beam) {
        ssize_t frames = mic_beam_read(in->beam, buffer, bytes / sizeof(int16_t));
        if (frames < 0) {
            in_close_beam(in);
            in->beam_off = true;
            memset(buffer, 0, bytes);
            return bytes;
        }
        if (in->adev->mic_muted) memset(buffer, 0, bytes);
        in->beam_frames += frames;
        return bytes;
    }
    return in->fireos->read(in->fireos, buffer, bytes);
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in* stream) {
    struct biscuit_stream_in* in = IN(stream);

    if (in->beam) return 0;
    return in->fireos->get_input_frames_lost(in->fireos);
}

static int in_get_capture_position(const struct audio_stream_in* stream, int64_t* frames,
                                   int64_t* time) {
    const struct biscuit_stream_in* in = IN_C(stream);

    if (in->beam) {
        struct timespec now;

        if (!frames || !time) return -EINVAL;
        clock_gettime(CLOCK_MONOTONIC, &now);
        *frames = (int64_t)in->beam_frames;
        *time = now.tv_sec * 1000000000LL + now.tv_nsec;
        return 0;
    }
    if (!in->fireos->get_capture_position) return -ENOSYS;
    return in->fireos->get_capture_position(in->fireos, frames, time);
}

static int adev_open_input_stream(struct audio_hw_device* dev, audio_io_handle_t handle,
                                  audio_devices_t devices, struct audio_config* config,
                                  struct audio_stream_in** stream_in, audio_input_flags_t flags,
                                  const char* address, audio_source_t source) {
    struct biscuit_device* adev = (struct biscuit_device*)dev;
    struct fireos_stream_in* f = NULL;
    struct fireos_config fconfig;
    struct biscuit_stream_in* in;
    struct audio_stream_in* s;
    int ret;

    to_fireos_config(config, &fconfig);
    ret = adev->fireos->open_input_stream(adev->fireos, handle, devices, &fconfig, &f, flags,
                                          address, source);
    from_fireos_config(&fconfig, config);
    if (ret != 0) return ret;
    if (!f) return -ENODEV;

    in = calloc(1, sizeof(*in));
    if (!in) {
        adev->fireos->close_input_stream(adev->fireos, f);
        return -ENOMEM;
    }
    in->fireos = f;
    in->adev = adev;

    s = &in->stream;
    s->common.get_sample_rate = in_get_sample_rate;
    s->common.set_sample_rate = in_set_sample_rate;
    s->common.get_buffer_size = in_get_buffer_size;
    s->common.get_channels = in_get_channels;
    s->common.get_format = in_get_format;
    s->common.set_format = in_set_format;
    s->common.standby = in_standby;
    s->common.dump = in_dump;
    s->common.get_device = in_get_device;
    s->common.set_device = in_set_device;
    s->common.set_parameters = in_set_parameters;
    s->common.get_parameters = in_get_parameters;
    s->common.add_audio_effect = in_add_audio_effect;
    s->common.remove_audio_effect = in_remove_audio_effect;
    s->set_gain = f->set_gain ? in_set_gain : NULL;
    s->read = in_read;
    s->get_input_frames_lost = in_get_input_frames_lost;
    s->get_capture_position = in_get_capture_position;

    in->beam_capable =
            adev->beam_model && f->common.get_sample_rate(&f->common) == 16000 &&
            audio_channel_count_from_in_mask(f->common.get_channels(&f->common)) == 1 &&
            f->common.get_format(&f->common) == AUDIO_FORMAT_PCM_16_BIT &&
            (devices & AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN) != 0;

    *stream_in = s;
    return 0;
}

static void adev_close_input_stream(struct audio_hw_device* dev, struct audio_stream_in* stream) {
    struct biscuit_stream_in* in = IN(stream);

    in_close_beam(in);
    fireos_dev(dev)->close_input_stream(fireos_dev(dev), in->fireos);
    free(in);
}

/* The device ------------------------------------------------------------- */

static uint32_t adev_get_supported_devices(const struct audio_hw_device* dev) {
    return fireos_dev(dev)->get_supported_devices(fireos_dev(dev));
}

static int adev_init_check(const struct audio_hw_device* dev) {
    return fireos_dev(dev)->init_check(fireos_dev(dev));
}

static int adev_set_voice_volume(struct audio_hw_device* dev, float volume) {
    return fireos_dev(dev)->set_voice_volume(fireos_dev(dev), volume);
}

static int adev_set_master_volume(struct audio_hw_device* dev, float volume) {
    return fireos_dev(dev)->set_master_volume(fireos_dev(dev), volume);
}

static int adev_get_master_volume(struct audio_hw_device* dev, float* volume) {
    return fireos_dev(dev)->get_master_volume(fireos_dev(dev), volume);
}

static int adev_set_mode(struct audio_hw_device* dev, audio_mode_t mode) {
    return fireos_dev(dev)->set_mode(fireos_dev(dev), mode);
}

/* The Fire OS HAL mutes the microphones with GPIO 87, then tells Amazon's
 * audio processor, which is not running, so it reports a failure. */
static int adev_set_mic_mute(struct audio_hw_device* dev, bool state) {
    struct biscuit_device* adev = (struct biscuit_device*)dev;
    int ret = adev->fireos->set_mic_mute(adev->fireos, state);

    if (ret != 0) ALOGV("Fire OS set_mic_mute(%d) returned %d", state, ret);
    adev->mic_muted = state;
    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device* dev, bool* state) {
    if (!state) return -EINVAL;
    *state = ((const struct biscuit_device*)dev)->mic_muted;
    return 0;
}

static int adev_set_parameters(struct audio_hw_device* dev, const char* kv_pairs) {
    return fireos_dev(dev)->set_parameters(fireos_dev(dev), kv_pairs);
}

static char* adev_get_parameters(const struct audio_hw_device* dev, const char* keys) {
    return fireos_dev(dev)->get_parameters(fireos_dev(dev), keys);
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device* dev,
                                         const struct audio_config* config) {
    struct fireos_config fconfig;

    to_fireos_config(config, &fconfig);
    return fireos_dev(dev)->get_input_buffer_size(fireos_dev(dev), &fconfig);
}

static int adev_dump(const struct audio_hw_device* dev, int fd) {
    return fireos_dev(dev)->dump ? fireos_dev(dev)->dump(fireos_dev(dev), fd) : 0;
}

static int adev_set_master_mute(struct audio_hw_device* dev, bool mute) {
    return fireos_dev(dev)->set_master_mute(fireos_dev(dev), mute);
}

static int adev_get_master_mute(struct audio_hw_device* dev, bool* mute) {
    return fireos_dev(dev)->get_master_mute(fireos_dev(dev), mute);
}

static int adev_create_audio_patch(struct audio_hw_device* dev, unsigned int num_sources,
                                   const struct audio_port_config* sources, unsigned int num_sinks,
                                   const struct audio_port_config* sinks,
                                   audio_patch_handle_t* handle) {
    struct fireos_port_config fsources[AUDIO_PATCH_PORTS_MAX];
    struct fireos_port_config fsinks[AUDIO_PATCH_PORTS_MAX];
    unsigned int i;

    if (num_sources > AUDIO_PATCH_PORTS_MAX || num_sinks > AUDIO_PATCH_PORTS_MAX) return -EINVAL;
    for (i = 0; i < num_sources; i++) to_fireos_port_config(&sources[i], &fsources[i]);
    for (i = 0; i < num_sinks; i++) to_fireos_port_config(&sinks[i], &fsinks[i]);
    return fireos_dev(dev)->create_audio_patch(fireos_dev(dev), num_sources, fsources, num_sinks,
                                               fsinks, handle);
}

static int adev_release_audio_patch(struct audio_hw_device* dev, audio_patch_handle_t handle) {
    return fireos_dev(dev)->release_audio_patch(fireos_dev(dev), handle);
}

/* Android calls these without checking for them, and the Fire OS HAL knows
 * an older struct audio_port. */
static int adev_get_audio_port(struct audio_hw_device* dev __unused,
                               struct audio_port* port __unused) {
    return -ENOSYS;
}

static int adev_set_audio_port_config(struct audio_hw_device* dev __unused,
                                      const struct audio_port_config* config __unused) {
    return -ENOSYS;
}

static int adev_close(struct hw_device_t* device) {
    struct biscuit_device* adev = (struct biscuit_device*)device;
    int ret = 0;

    set_speaker_amp(false);
    if (adev->fireos->common.close) ret = adev->fireos->common.close(&adev->fireos->common);
    mic_beam_model_free(adev->beam_model);
    free(adev);
    return ret;
}

static int adev_open(const struct hw_module_t* module, const char* name,
                     struct hw_device_t** device) {
    struct biscuit_device* adev;
    const struct hw_module_t* fireos_module;
    struct fireos_device* f;
    struct audio_hw_device* d;
    int ret;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0) return -EINVAL;

    ret = hw_get_module_by_class(AUDIO_HARDWARE_MODULE_ID, "primary_amazon", &fireos_module);
    if (ret != 0) {
        ALOGE("cannot load the Fire OS audio HAL: %d", ret);
        return ret;
    }
    if (!fireos_module->methods || !fireos_module->methods->open) return -ENOSYS;

    adev = calloc(1, sizeof(*adev));
    if (!adev) return -ENOMEM;
    ret = fireos_module->methods->open(fireos_module, name, (struct hw_device_t**)&adev->fireos);
    if (ret != 0 || !adev->fireos) {
        ALOGE("cannot open the Fire OS audio HAL: %d", ret);
        free(adev);
        return ret ? ret : -ENODEV;
    }
    f = adev->fireos;
    ALOGI("Fire OS audio HAL version %#x", f->common.version);

    d = &adev->device;
    d->common.tag = HARDWARE_DEVICE_TAG;
    /* From 3.0 Android routes with audio patches and calls them unchecked. */
    d->common.version = f->common.version;
    if (d->common.version >= AUDIO_DEVICE_API_VERSION_3_0 &&
        (!f->create_audio_patch || !f->release_audio_patch)) {
        d->common.version = AUDIO_DEVICE_API_VERSION_2_0;
    }
    d->common.module = (struct hw_module_t*)module;
    d->common.close = adev_close;
    d->get_supported_devices = f->get_supported_devices ? adev_get_supported_devices : NULL;
    d->init_check = adev_init_check;
    d->set_voice_volume = adev_set_voice_volume;
    d->set_master_volume = f->set_master_volume ? adev_set_master_volume : NULL;
    d->get_master_volume = f->get_master_volume ? adev_get_master_volume : NULL;
    d->set_mode = adev_set_mode;
    d->set_mic_mute = adev_set_mic_mute;
    d->get_mic_mute = adev_get_mic_mute;
    d->set_parameters = adev_set_parameters;
    d->get_parameters = adev_get_parameters;
    d->get_input_buffer_size = adev_get_input_buffer_size;
    d->open_output_stream = adev_open_output_stream;
    d->close_output_stream = adev_close_output_stream;
    d->open_input_stream = adev_open_input_stream;
    d->close_input_stream = adev_close_input_stream;
    d->dump = adev_dump;
    d->set_master_mute = f->set_master_mute ? adev_set_master_mute : NULL;
    d->get_master_mute = f->get_master_mute ? adev_get_master_mute : NULL;
    d->create_audio_patch = f->create_audio_patch ? adev_create_audio_patch : NULL;
    d->release_audio_patch = f->release_audio_patch ? adev_release_audio_patch : NULL;
    d->get_audio_port = adev_get_audio_port;
    d->set_audio_port_config = adev_set_audio_port_config;

    adev->beam_model = mic_beam_model_create();
    /* Nothing plays yet, so the amplifier starts off. */
    set_speaker_amp(false);
    *device = &d->common;
    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
        .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
        .common =
                {
                        .tag = HARDWARE_MODULE_TAG,
                        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
                        .hal_api_version = HARDWARE_HAL_API_VERSION,
                        .id = AUDIO_HARDWARE_MODULE_ID,
                        .name = "Echo Dot audio HAL",
                        .author = "The LineageOS Project",
                        .methods = &hal_module_methods,
                },
};
