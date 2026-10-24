/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The Echo Dot's ambient light sensor, a TSL2584TSV or a TSL2540 depending
 * on the Dot. The TSL2584TSV reports through IIO (illuminance0_input). The
 * TSL2540 publishes als_lux on its I2C device and only measures while
 * als_power_state is 1, so the HAL powers it with the sensor's activation.
 * The HAL lists the sensor even before its driver has probed and finds the
 * chip when it first reads, so SensorService always gets a HAL to talk to.
 */

#define LOG_TAG "BiscuitSensors"

#include <errno.h>
#include <fcntl.h>
#include <hardware/hardware.h>
#include <hardware/sensors.h>
#include <log/log.h>
#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef IIO_DIR
#define IIO_DIR "/sys/bus/iio/devices"
#endif

#define TSL2540_DIR "/sys/bus/i2c/devices/0-0039"

#define LIGHT_HANDLE 1
#define DEFAULT_DELAY_NS 500000000LL
#define MIN_DELAY_US 200000

/* Android 11's sensors HAL service needs device version 1.3: batch and flush. */
struct biscuit_poll_device {
    struct sensors_poll_device_1 device;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    int active;
    int flush_pending;
    int64_t delay_ns;
    char lux_path[PATH_MAX];
    /* The TSL2540 found the lux, and its power follows activation. */
    int tsl2540;
};

static const struct sensor_t sSensorList[] = {{
    .name = "Echo Dot ambient light",
    .vendor = "AMS",
    .version = 1,
    .handle = LIGHT_HANDLE,
    .type = SENSOR_TYPE_LIGHT,
    .maxRange = 65535.0f,
    .resolution = 1.0f,
    .power = 0.1f,
    .minDelay = MIN_DELAY_US,
    .fifoReservedEventCount = 0,
    .fifoMaxEventCount = 0,
    .stringType = SENSOR_STRING_TYPE_LIGHT,
    .requiredPermission = "",
    .maxDelay = 1000000,
    .flags = SENSOR_FLAG_ON_CHANGE_MODE,
    .reserved = {0},
}};

static void tsl2540_power(int on)
{
    int fd = open(TSL2540_DIR "/als_power_state", O_WRONLY | O_CLOEXEC);
    if (fd < 0) return;
    if (write(fd, on ? "1" : "0", 1) != 1) ALOGW("could not power the TSL2540 %s", on ? "on" : "off");
    close(fd);
}

static int find_lux_path(char *out, size_t out_len, int *tsl2540)
{
    const char *fixed = IIO_DIR "/iio:device0/illuminance0_input";
    *tsl2540 = 0;
    if (access(fixed, R_OK) == 0) {
        strlcpy(out, fixed, out_len);
        return 0;
    }

    DIR *dir = opendir(IIO_DIR);
    if (dir) {
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            if (strncmp(de->d_name, "iio:device", 10) != 0) continue;
            char path[PATH_MAX];
            snprintf(path, sizeof(path), IIO_DIR "/%s/illuminance0_input", de->d_name);
            if (access(path, R_OK) == 0) {
                strlcpy(out, path, out_len);
                closedir(dir);
                return 0;
            }
        }
        closedir(dir);
    }

    if (access(TSL2540_DIR "/als_lux", R_OK) == 0) {
        strlcpy(out, TSL2540_DIR "/als_lux", out_len);
        *tsl2540 = 1;
        return 0;
    }
    return -ENOENT;
}

static int read_lux(struct biscuit_poll_device *dev, float *lux)
{
    if (!dev->lux_path[0]) {
        if (find_lux_path(dev->lux_path, sizeof(dev->lux_path), &dev->tsl2540) < 0)
            return -ENOENT;
        /* Found while active: it needs its power now. */
        if (dev->tsl2540) tsl2540_power(dev->active);
    }

    FILE *f = fopen(dev->lux_path, "r");
    if (!f) {
        dev->lux_path[0] = '\0';
        return -errno;
    }

    float value = 0.0f;
    int ok = fscanf(f, "%f", &value);
    fclose(f);
    if (ok != 1) return -EIO;

    *lux = value < 0.0f ? 0.0f : value;
    return 0;
}

static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void sleep_ns(int64_t ns)
{
    if (ns < 10000000LL) ns = 10000000LL;
    struct timespec ts = { ns / 1000000000LL, ns % 1000000000LL };
    nanosleep(&ts, NULL);
}

static int poll__activate(struct sensors_poll_device_t *d, int handle, int enabled)
{
    if (handle != LIGHT_HANDLE) return -EINVAL;
    struct biscuit_poll_device *dev = (struct biscuit_poll_device *)d;
    pthread_mutex_lock(&dev->lock);
    dev->active = enabled ? 1 : 0;
    if (dev->tsl2540) tsl2540_power(dev->active);
    pthread_cond_signal(&dev->wake);
    pthread_mutex_unlock(&dev->lock);
    return 0;
}

static int poll__setDelay(struct sensors_poll_device_t *d, int handle, int64_t ns)
{
    if (handle != LIGHT_HANDLE) return -EINVAL;
    if (ns < (int64_t)MIN_DELAY_US * 1000LL) ns = (int64_t)MIN_DELAY_US * 1000LL;
    struct biscuit_poll_device *dev = (struct biscuit_poll_device *)d;
    pthread_mutex_lock(&dev->lock);
    dev->delay_ns = ns;
    pthread_mutex_unlock(&dev->lock);
    return 0;
}

static int poll__batch(struct sensors_poll_device_1 *d, int handle, int flags,
        int64_t period_ns, int64_t max_report_latency_ns)
{
    (void)flags;
    (void)max_report_latency_ns;
    return poll__setDelay((struct sensors_poll_device_t *)d, handle, period_ns);
}

/* The sensor has no FIFO, so a flush completes at once, on the next poll. */
static int poll__flush(struct sensors_poll_device_1 *d, int handle)
{
    if (handle != LIGHT_HANDLE) return -EINVAL;
    struct biscuit_poll_device *dev = (struct biscuit_poll_device *)d;
    pthread_mutex_lock(&dev->lock);
    dev->flush_pending++;
    pthread_cond_signal(&dev->wake);
    pthread_mutex_unlock(&dev->lock);
    return 0;
}

static int poll__poll(struct sensors_poll_device_t *d, sensors_event_t *data, int count)
{
    if (count < 1) return -EINVAL;
    struct biscuit_poll_device *dev = (struct biscuit_poll_device *)d;

    pthread_mutex_lock(&dev->lock);
    // Nothing to report while no app listens: sleep until one does.
    while (!dev->active && !dev->flush_pending)
        pthread_cond_wait(&dev->wake, &dev->lock);
    int active = dev->active;
    int64_t delay = dev->delay_ns;
    int flush = dev->flush_pending > 0;
    if (flush) dev->flush_pending--;
    pthread_mutex_unlock(&dev->lock);

    if (flush) {
        memset(data, 0, sizeof(*data));
        data->version = META_DATA_VERSION;
        data->type = SENSOR_TYPE_META_DATA;
        data->meta_data.what = META_DATA_FLUSH_COMPLETE;
        data->meta_data.sensor = LIGHT_HANDLE;
        return 1;
    }

    sleep_ns(delay);
    if (!active) return 0;

    float lux = 0.0f;
    if (read_lux(dev, &lux) < 0) return 0;

    memset(data, 0, sizeof(*data));
    data->version = sizeof(*data);
    data->sensor = LIGHT_HANDLE;
    data->type = SENSOR_TYPE_LIGHT;
    data->timestamp = now_ns();
    data->light = lux;
    return 1;
}

static int poll__close(struct hw_device_t *dev)
{
    struct biscuit_poll_device *poll = (struct biscuit_poll_device *)dev;
    pthread_cond_destroy(&poll->wake);
    pthread_mutex_destroy(&poll->lock);
    free(poll);
    return 0;
}

static int sensors__get_sensors_list(struct sensors_module_t *module,
        struct sensor_t const **list)
{
    (void)module;
    *list = sSensorList;
    return sizeof(sSensorList) / sizeof(sSensorList[0]);
}

static int open_sensors(const struct hw_module_t *module, const char *name,
        struct hw_device_t **device)
{
    if (strcmp(name, SENSORS_HARDWARE_POLL) != 0) return -EINVAL;

    struct biscuit_poll_device *dev = calloc(1, sizeof(*dev));
    if (!dev) return -ENOMEM;

    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.version = SENSORS_DEVICE_API_VERSION_1_3;
    dev->device.common.module = (struct hw_module_t *)module;
    dev->device.common.close = poll__close;
    dev->device.activate = poll__activate;
    dev->device.setDelay = poll__setDelay;
    dev->device.poll = poll__poll;
    dev->device.batch = poll__batch;
    dev->device.flush = poll__flush;
    dev->delay_ns = DEFAULT_DELAY_NS;
    pthread_mutex_init(&dev->lock, NULL);
    pthread_cond_init(&dev->wake, NULL);
    find_lux_path(dev->lux_path, sizeof(dev->lux_path), &dev->tsl2540);

    *device = &dev->device.common;
    return 0;
}

static struct hw_module_methods_t sensors_module_methods = {
    .open = open_sensors,
};

struct sensors_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .version_major = 1,
        .version_minor = 0,
        .id = SENSORS_HARDWARE_MODULE_ID,
        .name = "Echo Dot sensors",
        .author = "The LineageOS Project",
        .methods = &sensors_module_methods,
    },
    .get_sensors_list = sensors__get_sensors_list,
};
