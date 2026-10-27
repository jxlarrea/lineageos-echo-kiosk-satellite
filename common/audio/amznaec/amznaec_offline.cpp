/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * amznaec_offline: runs the shim's processing on a recording instead of the
 * live microphones, to compare settings on identical audio. It builds the
 * shim's own source and feeds its pcm_open/pcm_read/pcm_close from a file in
 * the FPGA stream's shape (S24_3LE, 16 kHz, the microphones then the two
 * loopback channels), such as a persist.vendor.amznaec.dump .in.raw file.
 *
 *   amznaec_offline <in.raw> <out.raw> [channels] [setting=value ...]
 *
 * channels is 6 on the Echo Show 8 and 4 on the Echo Show 5 (default 6). A
 * setting is a persist.vendor.amznaec property without the prefix, such as
 * engine=0 or gain_db=20. Settings not given take the device's ro.vendor
 * defaults and otherwise the shim's own, never the live persist ones, so a
 * run does not depend on what was left set on the device. With
 * AMZNAEC_OFFLINE_MONO=1 set, only channel 0, the one the HAL hands Android,
 * is written. Not in the image.
 */

#include "amznaec_shim.cpp"

#include <sys/system_properties.h>

#include <map>

namespace {

std::map<std::string, std::string> overrides;
FILE* input;

const char* lookup(const char* key, char* buf) {
    static const std::string prefix = "persist.vendor.amznaec.";
    std::string k = key;
    if (k.compare(0, prefix.size(), prefix) == 0) {
        auto it = overrides.find(k.substr(prefix.size()));
        if (it == overrides.end()) return nullptr;
        snprintf(buf, PROPERTY_VALUE_MAX, "%s", it->second.c_str());
        return buf;
    }
    if (__system_property_get(key, buf) > 0) return buf;
    return nullptr;
}

struct pcm* fake_open(unsigned, unsigned, unsigned, struct pcm_config*) {
    return reinterpret_cast<struct pcm*>(&input);
}

int fake_read(struct pcm*, void* data, unsigned count) {
    return fread(data, 1, count, input) == count ? 0 : -1;
}

int fake_close(struct pcm*) { return 0; }

}  // namespace

extern "C" int property_get(const char* key, char* value, const char* default_value) {
    char buf[PROPERTY_VALUE_MAX];
    const char* v = lookup(key, buf);
    if (!v) v = default_value ? default_value : "";
    snprintf(value, PROPERTY_VALUE_MAX, "%s", v);
    return strlen(value);
}

extern "C" int32_t property_get_int32(const char* key, int32_t default_value) {
    char buf[PROPERTY_VALUE_MAX];
    const char* v = lookup(key, buf);
    return v && *v ? (int32_t)strtol(v, nullptr, 0) : default_value;
}

extern "C" int8_t property_get_bool(const char* key, int8_t default_value) {
    char buf[PROPERTY_VALUE_MAX];
    const char* v = lookup(key, buf);
    if (!v) return default_value;
    if (!strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "y") || !strcmp(v, "yes") ||
        !strcmp(v, "on"))
        return 1;
    if (!strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "n") || !strcmp(v, "no") ||
        !strcmp(v, "off"))
        return 0;
    return default_value;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <in.raw> <out.raw> [channels] [setting=value ...]\n", argv[0]);
        return 1;
    }
    int arg = 3;
    unsigned channels = 6;
    if (argc > 3 && !strchr(argv[3], '=')) channels = (unsigned)atoi(argv[arg++]);
    for (; arg < argc; arg++) {
        const char* eq = strchr(argv[arg], '=');
        if (!eq) continue;
        overrides[std::string(argv[arg], eq - argv[arg])] = eq + 1;
    }
    input = fopen(argv[1], "rb");
    FILE* output = fopen(argv[2], "wb");
    if (!input || !output) {
        perror("cannot open the files");
        return 1;
    }

    real_pcm_open = fake_open;
    real_pcm_read = fake_read;
    real_pcm_close = fake_close;

    struct pcm_config config = {};
    config.channels = channels;
    config.rate = kRate;
    config.period_size = 320;
    config.period_count = 4;
    config.format = PCM_FORMAT_S24_3LE;
    struct pcm* pcm = pcm_open(0, 22, PCM_IN, &config);
    if (pcm != g.pcm) {
        fprintf(stderr, "the shim did not take the stream (enable=0 or a setup error)\n");
        return 1;
    }

    std::vector<uint8_t> buf(config.period_size * channels * 3);
    bool mono = getenv("AMZNAEC_OFFLINE_MONO") != nullptr;
    while (pcm_read(pcm, buf.data(), buf.size()) == 0) {
        if (!mono) {
            fwrite(buf.data(), 1, buf.size(), output);
            continue;
        }
        for (unsigned f = 0; f < config.period_size; f++) fwrite(&buf[f * channels * 3], 1, 3, output);
    }
    pcm_close(pcm);
    fclose(output);
    fclose(input);
    return 0;
}
