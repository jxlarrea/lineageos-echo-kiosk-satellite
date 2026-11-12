/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The Echo Dot (3rd gen)'s primary audio HAL. It drives the kernel's sound
 * card directly, with none of Fire OS's audio libraries:
 *
 *  - The speaker: the DL1 playback PCM, which the MT8167 sends over I2S to
 *    the TAS2770 amplifier.
 *  - The microphones: the TDM capture PCM, where two TLV320AIC3101
 *    converters deliver the four microphones in the first four of eight
 *    32-bit channels at 16 kHz.
 *
 * Android gets one microphone as captured, with no processing, as on the
 * other Echos: Kiosk Satellite's own echo canceller then sees the speaker's
 * echo at a fixed delay. persist.donut.mic picks the microphone, 0 to 3.
 * A stereo capture gets that microphone and the next one.
 */

#define LOG_TAG "audio.donut"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <cutils/properties.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <log/log.h>
#include <tinyalsa/asoundlib.h>

#define CARD 0
#define PCM_SPEAKER 6 /* DL1_Playback */
#define PCM_MICS 1 /* TDM_Capture */

#define OUT_RATE 48000
#define OUT_CHANNELS 2
#define OUT_PERIOD 1024
#define OUT_PERIODS 4

#define IN_RATE 16000
#define IN_CHANNELS 8 /* what the TDM link carries */
#define IN_MICS 4 /* the channels with a microphone */
#define IN_PERIOD 320 /* 20 ms */
#define IN_PERIODS 4

/*
 * The mixer state Fire OS 6 has while it plays and listens (its
 * mixer_paths.xml, read back from a running Dot): the I2S route to the
 * amplifier, the converters' differential inputs, their 12 dB preamp
 * (24 half dB steps) and their mutes off.
 */
struct mixer_setting {
    const char* name;
    const char* text; /* an enum value, or NULL */
    int value;
};

static const struct mixer_setting kMixerSettings[] = {
        {"Playback State Device Select", NULL, 6},
        {"O03 I05 Switch", NULL, 1},
        {"O04 I06 Switch", NULL, 1},
        {"O05 I05 Switch", NULL, 1},
        {"O06 I06 Switch", NULL, 1},
        {"O09 I03 Switch", NULL, 1},
        {"O10 I04 Switch", NULL, 1},
        {"I2S O03_O04 Switch", NULL, 1},
        {"INT ADDA O03_O04 Switch", NULL, 1},
        {"MRG BT O02 Switch", NULL, 1},
        {"DAIBT Mux", "MRG", 0},
        {"Left PGA Mux", "OPEN", 0},
        {"Right PGA Mux", "OPEN", 0},
        {"ADC_A MICPGA Volume Ctrl", NULL, 24},
        {"ADC_B MICPGA Volume Ctrl", NULL, 24},
        {"ADC_A DIF1_L Input Gain", NULL, 0},
        {"ADC_A DIF1_R Input Gain", NULL, 0},
        {"ADC_B DIF1_L Input Gain", NULL, 0},
        {"ADC_B DIF1_R Input Gain", NULL, 0},
        {"ADC_A Left Ip Select ADC_A DIF1_L switch", NULL, 1},
        {"ADC_A Right Ip Select ADC_A DIF1_R switch", NULL, 1},
        {"ADC_B Left Ip Select ADC_B DIF1_L switch", NULL, 1},
        {"ADC_B Right Ip Select ADC_B DIF1_R switch", NULL, 1},
        {"ADC_A Left Mute", NULL, 0},
        {"ADC_A Right Mute", NULL, 0},
        {"ADC_B Left Mute", NULL, 0},
        {"ADC_B Right Mute", NULL, 0},
};

static void apply_mixer_settings(void) {
    struct mixer* mixer = mixer_open(CARD);
    size_t i;

    if (!mixer) {
        ALOGE("cannot open the mixer of card %d", CARD);
        return;
    }
    for (i = 0; i < sizeof(kMixerSettings) / sizeof(kMixerSettings[0]); i++) {
        const struct mixer_setting* s = &kMixerSettings[i];
        struct mixer_ctl* ctl = mixer_get_ctl_by_name(mixer, s->name);
        unsigned int n;
        int ret = 0;

        if (!ctl) {
            ALOGW("no mixer control \"%s\"", s->name);
            continue;
        }
        if (s->text) {
            ret = mixer_ctl_set_enum_by_string(ctl, s->text);
        } else {
            for (n = 0; n < mixer_ctl_get_num_values(ctl) && ret == 0; n++) {
                ret = mixer_ctl_set_value(ctl, n, s->value);
            }
        }
        if (ret != 0) ALOGW("cannot set \"%s\": %d", s->name, ret);
    }
    mixer_close(mixer);
}

struct donut_device {
    struct audio_hw_device device;
    pthread_mutex_t lock;
    bool mic_muted;
};

/* The speaker ------------------------------------------------------------ */

struct donut_stream_out {
    struct audio_stream_out stream;
    pthread_mutex_t lock;
    struct pcm* pcm;
    uint64_t frames_written;
};

#define OUT(s) ((struct donut_stream_out*)(s))

static const struct pcm_config kOutConfig = {
        .channels = OUT_CHANNELS,
        .rate = OUT_RATE,
        .period_size = OUT_PERIOD,
        .period_count = OUT_PERIODS,
        .format = PCM_FORMAT_S16_LE,
        .start_threshold = OUT_PERIOD * 2,
};

static uint32_t out_get_sample_rate(const struct audio_stream* stream __unused) {
    return OUT_RATE;
}

static int out_set_sample_rate(struct audio_stream* stream __unused, uint32_t rate __unused) {
    return -ENOSYS;
}

static size_t out_get_buffer_size(const struct audio_stream* stream __unused) {
    return OUT_PERIOD * OUT_CHANNELS * sizeof(int16_t);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream* stream __unused) {
    return AUDIO_CHANNEL_OUT_STEREO;
}

static audio_format_t out_get_format(const struct audio_stream* stream __unused) {
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream* stream __unused, audio_format_t format __unused) {
    return -ENOSYS;
}

static int out_standby(struct audio_stream* stream) {
    struct donut_stream_out* out = OUT(stream);

    pthread_mutex_lock(&out->lock);
    if (out->pcm) {
        pcm_close(out->pcm);
        out->pcm = NULL;
    }
    pthread_mutex_unlock(&out->lock);
    return 0;
}

static int out_dump(const struct audio_stream* stream __unused, int fd __unused) {
    return 0;
}

static audio_devices_t out_get_device(const struct audio_stream* stream __unused) {
    return AUDIO_DEVICE_OUT_SPEAKER;
}

static int out_set_device(struct audio_stream* stream __unused, audio_devices_t device __unused) {
    return 0;
}

static int out_set_parameters(struct audio_stream* stream __unused, const char* kv __unused) {
    return 0;
}

static char* out_get_parameters(const struct audio_stream* stream __unused,
                                const char* keys __unused) {
    return strdup("");
}

static int out_add_audio_effect(const struct audio_stream* stream __unused,
                                effect_handle_t effect __unused) {
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream* stream __unused,
                                   effect_handle_t effect __unused) {
    return 0;
}

static uint32_t out_get_latency(const struct audio_stream_out* stream __unused) {
    return OUT_PERIOD * OUT_PERIODS * 1000 / OUT_RATE;
}

static ssize_t out_write(struct audio_stream_out* stream, const void* buffer, size_t bytes) {
    struct donut_stream_out* out = OUT(stream);
    size_t frames = bytes / (OUT_CHANNELS * sizeof(int16_t));
    int ret = 0;

    pthread_mutex_lock(&out->lock);
    if (!out->pcm) {
        /* tinyalsa writes the period it got back into the config. */
        struct pcm_config config = kOutConfig;
        out->pcm = pcm_open(CARD, PCM_SPEAKER, PCM_OUT | PCM_MONOTONIC, &config);
        if (!out->pcm || !pcm_is_ready(out->pcm)) {
            ALOGE("cannot open the speaker: %s", out->pcm ? pcm_get_error(out->pcm) : "no memory");
            if (out->pcm) pcm_close(out->pcm);
            out->pcm = NULL;
            ret = -ENODEV;
        }
    }
    if (out->pcm) {
        ret = pcm_write(out->pcm, buffer, bytes);
        if (ret != 0) ALOGW("speaker write failed: %s", pcm_get_error(out->pcm));
    }
    if (ret == 0) out->frames_written += frames;
    pthread_mutex_unlock(&out->lock);

    /* Keep AudioFlinger's pace when the card does not take the audio. */
    if (ret != 0) usleep(frames * 1000000ULL / OUT_RATE);
    return bytes;
}

static int out_get_render_position(const struct audio_stream_out* stream __unused,
                                   uint32_t* dsp_frames __unused) {
    return -ENOSYS;
}

static int out_get_presentation_position(const struct audio_stream_out* stream, uint64_t* frames,
                                         struct timespec* timestamp) {
    struct donut_stream_out* out = (struct donut_stream_out*)stream;
    unsigned int avail;
    int ret = -ENODATA;

    pthread_mutex_lock(&out->lock);
    if (out->pcm && pcm_get_htimestamp(out->pcm, &avail, timestamp) == 0) {
        int64_t queued = (int64_t)pcm_get_buffer_size(out->pcm) - avail;
        int64_t played = (int64_t)out->frames_written - queued;

        if (played >= 0) {
            *frames = played;
            ret = 0;
        }
    }
    pthread_mutex_unlock(&out->lock);
    return ret;
}

static int adev_open_output_stream(struct audio_hw_device* dev __unused,
                                   audio_io_handle_t handle __unused,
                                   audio_devices_t devices __unused,
                                   audio_output_flags_t flags __unused,
                                   struct audio_config* config,
                                   struct audio_stream_out** stream_out,
                                   const char* address __unused) {
    struct donut_stream_out* out = calloc(1, sizeof(*out));
    struct audio_stream_out* s;

    if (!out) return -ENOMEM;
    pthread_mutex_init(&out->lock, NULL);
    config->sample_rate = OUT_RATE;
    config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    config->format = AUDIO_FORMAT_PCM_16_BIT;

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
    s->write = out_write;
    s->get_render_position = out_get_render_position;
    s->get_presentation_position = out_get_presentation_position;
    *stream_out = s;
    return 0;
}

static void adev_close_output_stream(struct audio_hw_device* dev __unused,
                                     struct audio_stream_out* stream) {
    out_standby(&stream->common);
    free(stream);
}

/* The microphones -------------------------------------------------------- */

struct donut_stream_in {
    struct audio_stream_in stream;
    struct donut_device* adev;
    pthread_mutex_t lock;
    struct pcm* pcm;
    unsigned int channels; /* what Android reads: 1 or 2 */
    unsigned int mic;
    int32_t* raw;
    size_t raw_frames;
    uint64_t frames_read;
};

#define IN(s) ((struct donut_stream_in*)(s))

static const struct pcm_config kInConfig = {
        .channels = IN_CHANNELS,
        .rate = IN_RATE,
        .period_size = IN_PERIOD,
        .period_count = IN_PERIODS,
        .format = PCM_FORMAT_S32_LE,
};

static uint32_t in_get_sample_rate(const struct audio_stream* stream __unused) {
    return IN_RATE;
}

static int in_set_sample_rate(struct audio_stream* stream __unused, uint32_t rate __unused) {
    return -ENOSYS;
}

static size_t in_get_buffer_size(const struct audio_stream* stream) {
    return IN_PERIOD * ((const struct donut_stream_in*)stream)->channels * sizeof(int16_t);
}

static audio_channel_mask_t in_get_channels(const struct audio_stream* stream) {
    return ((const struct donut_stream_in*)stream)->channels == 2 ? AUDIO_CHANNEL_IN_STEREO
                                                                  : AUDIO_CHANNEL_IN_MONO;
}

static audio_format_t in_get_format(const struct audio_stream* stream __unused) {
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int in_set_format(struct audio_stream* stream __unused, audio_format_t format __unused) {
    return -ENOSYS;
}

static int in_standby(struct audio_stream* stream) {
    struct donut_stream_in* in = IN(stream);

    pthread_mutex_lock(&in->lock);
    if (in->pcm) {
        pcm_close(in->pcm);
        in->pcm = NULL;
    }
    pthread_mutex_unlock(&in->lock);
    return 0;
}

static int in_dump(const struct audio_stream* stream __unused, int fd __unused) {
    return 0;
}

static audio_devices_t in_get_device(const struct audio_stream* stream __unused) {
    return AUDIO_DEVICE_IN_BUILTIN_MIC;
}

static int in_set_device(struct audio_stream* stream __unused, audio_devices_t device __unused) {
    return 0;
}

static int in_set_parameters(struct audio_stream* stream __unused, const char* kv __unused) {
    return 0;
}

static char* in_get_parameters(const struct audio_stream* stream __unused,
                               const char* keys __unused) {
    return strdup("");
}

static int in_add_audio_effect(const struct audio_stream* stream __unused,
                               effect_handle_t effect __unused) {
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream* stream __unused,
                                  effect_handle_t effect __unused) {
    return 0;
}

static int in_set_gain(struct audio_stream_in* stream __unused, float gain __unused) {
    return 0;
}

static ssize_t in_read(struct audio_stream_in* stream, void* buffer, size_t bytes) {
    struct donut_stream_in* in = IN(stream);
    int16_t* dst = buffer;
    size_t frames = bytes / (in->channels * sizeof(int16_t));
    size_t i;
    int ret = 0;

    pthread_mutex_lock(&in->lock);
    if (!in->pcm) {
        struct pcm_config config = kInConfig;
        in->pcm = pcm_open(CARD, PCM_MICS, PCM_IN | PCM_MONOTONIC, &config);
        if (!in->pcm || !pcm_is_ready(in->pcm)) {
            ALOGE("cannot open the microphones: %s",
                  in->pcm ? pcm_get_error(in->pcm) : "no memory");
            if (in->pcm) pcm_close(in->pcm);
            in->pcm = NULL;
            ret = -ENODEV;
        }
    }
    if (in->pcm && frames > in->raw_frames) {
        int32_t* raw = realloc(in->raw, frames * IN_CHANNELS * sizeof(int32_t));

        if (raw) {
            in->raw = raw;
            in->raw_frames = frames;
        } else {
            ret = -ENOMEM;
        }
    }
    if (in->pcm && ret == 0) {
        ret = pcm_read(in->pcm, in->raw, frames * IN_CHANNELS * sizeof(int32_t));
        if (ret != 0) ALOGW("microphone read failed: %s", pcm_get_error(in->pcm));
    }
    if (ret == 0 && !in->adev->mic_muted) {
        /* The converters' samples fill 32 bits. Android gets the top 16. */
        for (i = 0; i < frames; i++) {
            const int32_t* frame = in->raw + i * IN_CHANNELS;

            dst[i * in->channels] = (int16_t)(frame[in->mic] >> 16);
            if (in->channels == 2) {
                dst[i * 2 + 1] = (int16_t)(frame[(in->mic + 1) % IN_MICS] >> 16);
            }
        }
    } else {
        memset(buffer, 0, bytes);
    }
    if (ret == 0) in->frames_read += frames;
    pthread_mutex_unlock(&in->lock);

    if (ret != 0) usleep(frames * 1000000ULL / IN_RATE);
    return bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in* stream __unused) {
    return 0;
}

static int in_get_capture_position(const struct audio_stream_in* stream, int64_t* frames,
                                   int64_t* time) {
    struct donut_stream_in* in = (struct donut_stream_in*)stream;
    struct timespec ts;
    unsigned int avail;
    int ret = -ENODATA;

    if (!frames || !time) return -EINVAL;
    pthread_mutex_lock(&in->lock);
    if (in->pcm && pcm_get_htimestamp(in->pcm, &avail, &ts) == 0) {
        *frames = (int64_t)in->frames_read + avail;
        *time = ts.tv_sec * 1000000000LL + ts.tv_nsec;
        ret = 0;
    }
    pthread_mutex_unlock(&in->lock);
    return ret;
}

static int adev_open_input_stream(struct audio_hw_device* dev, audio_io_handle_t handle __unused,
                                  audio_devices_t devices __unused, struct audio_config* config,
                                  struct audio_stream_in** stream_in,
                                  audio_input_flags_t flags __unused, const char* address __unused,
                                  audio_source_t source __unused) {
    unsigned int channels = audio_channel_count_from_in_mask(config->channel_mask);
    struct donut_stream_in* in;
    struct audio_stream_in* s;

    /* One format: Android converts from it when an app asks for another. */
    if (config->sample_rate != IN_RATE || config->format != AUDIO_FORMAT_PCM_16_BIT ||
        (channels != 1 && channels != 2)) {
        config->sample_rate = IN_RATE;
        config->format = AUDIO_FORMAT_PCM_16_BIT;
        if (channels != 1 && channels != 2) config->channel_mask = AUDIO_CHANNEL_IN_MONO;
        return -EINVAL;
    }

    in = calloc(1, sizeof(*in));
    if (!in) return -ENOMEM;
    pthread_mutex_init(&in->lock, NULL);
    in->adev = (struct donut_device*)dev;
    in->channels = channels;
    in->mic = (unsigned int)property_get_int32("persist.donut.mic", 0) % IN_MICS;

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
    s->set_gain = in_set_gain;
    s->read = in_read;
    s->get_input_frames_lost = in_get_input_frames_lost;
    s->get_capture_position = in_get_capture_position;
    *stream_in = s;
    return 0;
}

static void adev_close_input_stream(struct audio_hw_device* dev __unused,
                                    struct audio_stream_in* stream) {
    in_standby(&stream->common);
    free(IN(stream)->raw);
    free(stream);
}

/* The device ------------------------------------------------------------- */

static int adev_init_check(const struct audio_hw_device* dev __unused) {
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device* dev __unused, float volume __unused) {
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device* dev __unused, audio_mode_t mode __unused) {
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device* dev, bool state) {
    ((struct donut_device*)dev)->mic_muted = state;
    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device* dev, bool* state) {
    if (!state) return -EINVAL;
    *state = ((const struct donut_device*)dev)->mic_muted;
    return 0;
}

static int adev_set_parameters(struct audio_hw_device* dev __unused, const char* kv __unused) {
    return 0;
}

static char* adev_get_parameters(const struct audio_hw_device* dev __unused,
                                 const char* keys __unused) {
    return strdup("");
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device* dev __unused,
                                         const struct audio_config* config) {
    unsigned int channels = audio_channel_count_from_in_mask(config->channel_mask);

    return IN_PERIOD * (channels == 2 ? 2 : 1) * sizeof(int16_t);
}

static int adev_dump(const struct audio_hw_device* dev __unused, int fd __unused) {
    return 0;
}

static int adev_close(struct hw_device_t* device) {
    free(device);
    return 0;
}

static int adev_open(const struct hw_module_t* module, const char* name,
                     struct hw_device_t** device) {
    struct donut_device* adev;
    struct audio_hw_device* d;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0) return -EINVAL;

    adev = calloc(1, sizeof(*adev));
    if (!adev) return -ENOMEM;
    pthread_mutex_init(&adev->lock, NULL);

    d = &adev->device;
    d->common.tag = HARDWARE_DEVICE_TAG;
    /* 2.0: Android routes by stream parameters, with no audio patches. */
    d->common.version = AUDIO_DEVICE_API_VERSION_2_0;
    d->common.module = (struct hw_module_t*)module;
    d->common.close = adev_close;
    d->init_check = adev_init_check;
    d->set_voice_volume = adev_set_voice_volume;
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

    apply_mixer_settings();
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
                        .name = "Echo Dot (3rd gen) audio HAL",
                        .author = "The LineageOS Project",
                        .methods = &hal_module_methods,
                },
};
