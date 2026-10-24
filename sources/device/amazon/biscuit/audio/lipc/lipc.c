/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * A liblipc that connects nowhere. LIPC is Amazon's messaging between Fire
 * OS processes, carried over D-Bus. Its only user on the Echo Dot is
 * libasp, Amazon's audio processor, which the Fire OS audio HAL starts and
 * which asserts when LIPC cannot connect. Android 11 has no D-Bus, and
 * nothing on the Dot talks to the processor over LIPC, so every call here
 * succeeds and does nothing. That also leaves Amazon's liblipc, libdbus and
 * GLib, 2.5 MB, out of the image.
 *
 * These are the calls libasp makes, with the signatures of Amazon's
 * openlipc API.
 */

#define LOG_TAG "lipc.biscuit"

#include <stddef.h>

#include <log/log.h>

typedef int LIPCcode;
#define LIPC_OK 0
#define LIPC_ERROR_UNKNOWN 1

typedef void LIPC;
typedef void LIPCevent;

static int handle;

LIPC* LipcOpenEx(const char* service, LIPCcode* code) {
    ALOGI("LIPC for %s goes nowhere on this device", service ? service : "(anonymous)");
    if (code) *code = LIPC_OK;
    return &handle;
}

void LipcClose(LIPC* lipc __unused) {}

LIPCcode LipcSetStringProperty(LIPC* lipc __unused, const char* service __unused,
                               const char* property __unused, const char* value __unused) {
    return LIPC_OK;
}

/* Nobody can call them, so the properties are never read or written. */
LIPCcode LipcRegisterIntProperties(LIPC* lipc __unused, ...) {
    return LIPC_OK;
}

LIPCcode LipcRegisterStringProperties(LIPC* lipc __unused, ...) {
    return LIPC_OK;
}

LIPCcode LipcUnregisterProperty(LIPC* lipc __unused, const char* property __unused, void** data) {
    if (data) *data = NULL;
    return LIPC_OK;
}

/* No events ever arrive. */
LIPCcode LipcSubscribeExt(LIPC* lipc __unused, ...) {
    return LIPC_OK;
}

LIPCcode LipcUnsubscribeExt(LIPC* lipc __unused, const char* service __unused,
                            const char* name __unused, void** data) {
    if (data) *data = NULL;
    return LIPC_OK;
}

LIPCcode LipcCreateAndSendEvent(LIPC* lipc __unused, const char* name __unused) {
    return LIPC_OK;
}

LIPCcode LipcCreateAndSendEventWithParameters(LIPC* lipc __unused, const char* name __unused,
                                              const char* format __unused, ...) {
    return LIPC_OK;
}

/* Only called from event callbacks, which never run. */
LIPCcode LipcGetIntParam(LIPCevent* event __unused, int* value) {
    if (value) *value = 0;
    return LIPC_ERROR_UNKNOWN;
}

LIPCcode LipcGetStringParam(LIPCevent* event __unused, char** value) {
    if (value) *value = NULL;
    return LIPC_ERROR_UNKNOWN;
}
