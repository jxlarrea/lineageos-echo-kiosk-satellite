/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * A hardware composer for the Echo Dot, which has no screen. It reports one
 * 1x1 display that nobody sees and claims every layer it may as an overlay,
 * which it then drops. SurfaceFlinger so never composes with its software
 * GLES, which would cost CPU and memory for a picture nobody sees.
 *
 * Vsync runs at one frame a second. Nothing is ever shown, so anything still
 * animating only needs to keep running, not to wake SurfaceFlinger 60 times
 * a second.
 *
 * It is a HWC 1.1 module: the composer 2.1 passthrough service wraps it with
 * HWC2On1Adapter, the way it does the other MT8163 Echos' stock HWC.
 */

#define LOG_TAG "hwcomposer.biscuit"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <atomic>

#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <log/log.h>

namespace {

constexpr int32_t kWidth = 1;
constexpr int32_t kHeight = 1;
// TV density, as on CM14. The value is in dots per thousand inches.
constexpr int32_t kDpi = 213 * 1000;
constexpr int64_t kVsyncPeriodNs = 1000000000;

struct BiscuitHwc {
    hwc_composer_device_1_t base;
    const hwc_procs_t* procs = nullptr;
    std::atomic<bool> vsync_enabled{false};
    std::atomic<bool> running{true};
    pthread_t vsync_thread;
};

BiscuitHwc* toHwc(hwc_composer_device_1_t* dev) {
    return reinterpret_cast<BiscuitHwc*>(dev);
}

int64_t nowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

void* vsyncLoop(void* arg) {
    auto* hwc = static_cast<BiscuitHwc*>(arg);
    int64_t next = nowNs();

    while (hwc->running) {
        next += kVsyncPeriodNs;
        struct timespec ts = {
                .tv_sec = static_cast<time_t>(next / 1000000000),
                .tv_nsec = static_cast<long>(next % 1000000000),
        };
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
        }
        const hwc_procs_t* procs = hwc->procs;
        if (hwc->vsync_enabled && procs && procs->vsync) {
            procs->vsync(procs, HWC_DISPLAY_PRIMARY, next);
        }
    }
    return nullptr;
}

int hwcPrepare(hwc_composer_device_1_t*, size_t numDisplays,
               hwc_display_contents_1_t** displays) {
    for (size_t d = 0; d < numDisplays; d++) {
        hwc_display_contents_1_t* contents = displays[d];
        if (!contents) continue;
        for (size_t i = 0; i < contents->numHwLayers; i++) {
            hwc_layer_1_t& layer = contents->hwLayers[i];
            if (layer.compositionType == HWC_FRAMEBUFFER_TARGET) continue;
            // Skip layers have to be composed by SurfaceFlinger.
            layer.compositionType =
                    (layer.flags & HWC_SKIP_LAYER) ? HWC_FRAMEBUFFER : HWC_OVERLAY;
        }
    }
    return 0;
}

int hwcSet(hwc_composer_device_1_t*, size_t numDisplays, hwc_display_contents_1_t** displays) {
    // Nothing scans the buffers out, so they are free as soon as they arrive.
    for (size_t d = 0; d < numDisplays; d++) {
        hwc_display_contents_1_t* contents = displays[d];
        if (!contents) continue;
        for (size_t i = 0; i < contents->numHwLayers; i++) {
            hwc_layer_1_t& layer = contents->hwLayers[i];
            if (layer.acquireFenceFd >= 0) {
                close(layer.acquireFenceFd);
                layer.acquireFenceFd = -1;
            }
            layer.releaseFenceFd = -1;
        }
        contents->retireFenceFd = -1;
    }
    return 0;
}

int hwcEventControl(hwc_composer_device_1_t* dev, int disp, int event, int enabled) {
    if (disp != HWC_DISPLAY_PRIMARY || event != HWC_EVENT_VSYNC) return -EINVAL;
    toHwc(dev)->vsync_enabled = enabled != 0;
    return 0;
}

int hwcBlank(hwc_composer_device_1_t*, int disp, int) {
    return disp == HWC_DISPLAY_PRIMARY ? 0 : -EINVAL;
}

int hwcQuery(hwc_composer_device_1_t*, int what, int* value) {
    switch (what) {
        case HWC_BACKGROUND_LAYER_SUPPORTED:
            *value = 0;
            return 0;
        case HWC_VSYNC_PERIOD:
            *value = static_cast<int>(kVsyncPeriodNs);
            return 0;
        case HWC_DISPLAY_TYPES_SUPPORTED:
            *value = HWC_DISPLAY_PRIMARY_BIT;
            return 0;
        default:
            return -EINVAL;
    }
}

void hwcRegisterProcs(hwc_composer_device_1_t* dev, const hwc_procs_t* procs) {
    toHwc(dev)->procs = procs;
}

void hwcDump(hwc_composer_device_1_t* dev, char* buff, int buff_len) {
    snprintf(buff, buff_len, "  headless %dx%d, vsync %s every %lld ms\n", kWidth, kHeight,
             toHwc(dev)->vsync_enabled ? "on" : "off",
             static_cast<long long>(kVsyncPeriodNs / 1000000));
}

int hwcGetDisplayConfigs(hwc_composer_device_1_t*, int disp, uint32_t* configs,
                         size_t* numConfigs) {
    if (disp != HWC_DISPLAY_PRIMARY) return -EINVAL;
    if (*numConfigs > 0) {
        configs[0] = 0;
        *numConfigs = 1;
    }
    return 0;
}

int hwcGetDisplayAttributes(hwc_composer_device_1_t*, int disp, uint32_t config,
                            const uint32_t* attributes, int32_t* values) {
    if (disp != HWC_DISPLAY_PRIMARY || config != 0) return -EINVAL;
    for (size_t i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
        switch (attributes[i]) {
            case HWC_DISPLAY_VSYNC_PERIOD:
                values[i] = static_cast<int32_t>(kVsyncPeriodNs);
                break;
            case HWC_DISPLAY_WIDTH:
                values[i] = kWidth;
                break;
            case HWC_DISPLAY_HEIGHT:
                values[i] = kHeight;
                break;
            case HWC_DISPLAY_DPI_X:
            case HWC_DISPLAY_DPI_Y:
                values[i] = kDpi;
                break;
            default:
                values[i] = 0;
                break;
        }
    }
    return 0;
}

int hwcClose(hw_device_t* dev) {
    auto* hwc = reinterpret_cast<BiscuitHwc*>(dev);
    hwc->running = false;
    pthread_join(hwc->vsync_thread, nullptr);
    delete hwc;
    return 0;
}

int hwcOpen(const hw_module_t* module, const char* name, hw_device_t** device) {
    if (strcmp(name, HWC_HARDWARE_COMPOSER)) return -EINVAL;

    auto* hwc = new BiscuitHwc();
    hwc->base.common.tag = HARDWARE_DEVICE_TAG;
    hwc->base.common.version = HWC_DEVICE_API_VERSION_1_1;
    hwc->base.common.module = const_cast<hw_module_t*>(module);
    hwc->base.common.close = hwcClose;
    hwc->base.prepare = hwcPrepare;
    hwc->base.set = hwcSet;
    hwc->base.eventControl = hwcEventControl;
    hwc->base.blank = hwcBlank;
    hwc->base.query = hwcQuery;
    hwc->base.registerProcs = hwcRegisterProcs;
    hwc->base.dump = hwcDump;
    hwc->base.getDisplayConfigs = hwcGetDisplayConfigs;
    hwc->base.getDisplayAttributes = hwcGetDisplayAttributes;

    int err = pthread_create(&hwc->vsync_thread, nullptr, vsyncLoop, hwc);
    if (err) {
        ALOGE("could not start the vsync thread: %s", strerror(err));
        delete hwc;
        return -err;
    }
    *device = &hwc->base.common;
    return 0;
}

hw_module_methods_t hwcMethods = {
        .open = hwcOpen,
};

}  // namespace

hwc_module_t HAL_MODULE_INFO_SYM = {
        .common =
                {
                        .tag = HARDWARE_MODULE_TAG,
                        .module_api_version = HWC_MODULE_API_VERSION_0_1,
                        .hal_api_version = HARDWARE_HAL_API_VERSION,
                        .id = HWC_HARDWARE_MODULE_ID,
                        .name = "Echo Dot headless hwcomposer",
                        .author = "The LineageOS Project",
                        .methods = &hwcMethods,
                },
};
