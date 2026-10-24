/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BISCUIT_FIREOS_AUDIO_H
#define BISCUIT_FIREOS_AUDIO_H

#include <hardware/audio.h>
#include <system/audio.h>

/*
 * The audio HAL structures as the Echo Dot's Fire OS 6 HAL knows them, from
 * Android 7.1. Android 11 grew most of them: the offload info and port
 * config have new fields in the middle, and the streams and the device have
 * new methods at the end, which the Fire OS HAL never fills. struct
 * audio_stream did not change, so both versions share it.
 */

struct fireos_offload_info {
    uint16_t version;
    uint16_t size;
    uint32_t sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    audio_stream_type_t stream_type;
    uint32_t bit_rate;
    int64_t duration_us;
    bool has_video;
    bool is_streaming;
    uint32_t bit_width;
    uint32_t offload_buffer_size;
    audio_usage_t usage;
};

struct fireos_config {
    uint32_t sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    struct fireos_offload_info offload_info;
    size_t frame_count;
};

/* Android 11 added flags before ext. */
struct fireos_port_config {
    audio_port_handle_t id;
    audio_port_role_t role;
    audio_port_type_t type;
    unsigned int config_mask;
    unsigned int sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    struct audio_gain_config gain;
    union {
        struct audio_port_config_device_ext device;
        struct audio_port_config_mix_ext mix;
        struct audio_port_config_session_ext session;
    } ext;
};

struct fireos_stream_out {
    struct audio_stream common;
    uint32_t (*get_latency)(const struct fireos_stream_out* stream);
    int (*set_volume)(struct fireos_stream_out* stream, float left, float right);
    ssize_t (*write)(struct fireos_stream_out* stream, const void* buffer, size_t bytes);
    int (*get_render_position)(const struct fireos_stream_out* stream, uint32_t* dsp_frames);
    int (*get_next_write_timestamp)(const struct fireos_stream_out* stream, int64_t* timestamp);
    int (*set_callback)(struct fireos_stream_out* stream, stream_callback_t callback,
                        void* cookie);
    int (*pause)(struct fireos_stream_out* stream);
    int (*resume)(struct fireos_stream_out* stream);
    int (*drain)(struct fireos_stream_out* stream, audio_drain_type_t type);
    int (*flush)(struct fireos_stream_out* stream);
    int (*get_presentation_position)(const struct fireos_stream_out* stream, uint64_t* frames,
                                     struct timespec* timestamp);
};

struct fireos_stream_in {
    struct audio_stream common;
    int (*set_gain)(struct fireos_stream_in* stream, float gain);
    ssize_t (*read)(struct fireos_stream_in* stream, void* buffer, size_t bytes);
    uint32_t (*get_input_frames_lost)(struct fireos_stream_in* stream);
    int (*get_capture_position)(const struct fireos_stream_in* stream, int64_t* frames,
                                int64_t* time);
};

struct fireos_device {
    struct hw_device_t common;
    uint32_t (*get_supported_devices)(const struct fireos_device* dev);
    int (*init_check)(const struct fireos_device* dev);
    int (*set_voice_volume)(struct fireos_device* dev, float volume);
    int (*set_master_volume)(struct fireos_device* dev, float volume);
    int (*get_master_volume)(struct fireos_device* dev, float* volume);
    int (*set_mode)(struct fireos_device* dev, audio_mode_t mode);
    int (*set_mic_mute)(struct fireos_device* dev, bool state);
    int (*get_mic_mute)(const struct fireos_device* dev, bool* state);
    int (*set_parameters)(struct fireos_device* dev, const char* kv_pairs);
    char* (*get_parameters)(const struct fireos_device* dev, const char* keys);
    size_t (*get_input_buffer_size)(const struct fireos_device* dev,
                                    const struct fireos_config* config);
    int (*open_output_stream)(struct fireos_device* dev, audio_io_handle_t handle,
                              audio_devices_t devices, audio_output_flags_t flags,
                              struct fireos_config* config, struct fireos_stream_out** stream_out,
                              const char* address);
    void (*close_output_stream)(struct fireos_device* dev, struct fireos_stream_out* stream_out);
    int (*open_input_stream)(struct fireos_device* dev, audio_io_handle_t handle,
                             audio_devices_t devices, struct fireos_config* config,
                             struct fireos_stream_in** stream_in, audio_input_flags_t flags,
                             const char* address, audio_source_t source);
    void (*close_input_stream)(struct fireos_device* dev, struct fireos_stream_in* stream_in);
    int (*dump)(const struct fireos_device* dev, int fd);
    int (*set_master_mute)(struct fireos_device* dev, bool mute);
    int (*get_master_mute)(struct fireos_device* dev, bool* mute);
    int (*create_audio_patch)(struct fireos_device* dev, unsigned int num_sources,
                              const struct fireos_port_config* sources, unsigned int num_sinks,
                              const struct fireos_port_config* sinks,
                              audio_patch_handle_t* handle);
    int (*release_audio_patch)(struct fireos_device* dev, audio_patch_handle_t handle);
    /* get_audio_port and set_audio_port_config follow. The wrapper offers
     * neither, since struct audio_port changed too. */
};

#endif
