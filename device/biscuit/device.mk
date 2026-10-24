#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# What the Echo Dot (2nd gen) gets on top of its device tree. stage-tree.sh
# copies device/biscuit/ to device/kiosk-satellite/biscuit in the tree.
#

LOCAL_PATH := device/kiosk-satellite/biscuit

# The HOME activity, which also leaves out what a speaker without a screen
# never uses (Android.bp), and the services it never needs (init/).
PRODUCT_PACKAGES += \
    BiscuitEmptyLauncher \
    init.kiosk-satellite.biscuit.rc

# WebView is left out, but its loader and platform support libraries stay:
# public.libraries.txt lists them, and no app can start without them.
PRODUCT_PACKAGES += \
    libwebviewchromium_loader \
    libwebviewchromium_plat_support

# Compile system_server and the system apps ahead of time, as on the Echo
# Show. Compiled code is file backed, so the kernel can drop it under
# pressure, while JIT code stays in each process. It also boots faster.
PRODUCT_SYSTEM_SERVER_COMPILER_FILTER := speed
PRODUCT_DEX_PREOPT_DEFAULT_COMPILER_FILTER := speed

PRODUCT_PRODUCT_PROPERTIES += \
    pm.dexopt.bg-dexopt=speed \
    pm.dexopt.install=speed

# Features the Dot never uses, which keeps their services out of
# system_server.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/permissions/kiosk-satellite-biscuit.xml:$(TARGET_COPY_OUT_SYSTEM)/etc/permissions/kiosk-satellite-biscuit.xml

# Built into the packages they change instead of runtime overlays, so
# SettingsProvider has them when it first creates its database.
DEVICE_PACKAGE_OVERLAYS += \
    $(LOCAL_PATH)/overlay

PRODUCT_ENFORCE_RRO_EXCLUDED_OVERLAYS += \
    $(LOCAL_PATH)/overlay/frameworks/base/core \
    $(LOCAL_PATH)/overlay/frameworks/base/packages/SettingsProvider

# Always on mains: mt8163-common then keeps all four cores online.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.vendor.power.wall_powered=true

# Wi-Fi power save off: the Dot is always on mains, and in power save some
# of the messages between kiosks got lost. setprop
# persist.biscuit.wifi_powersave 1 turns it back on.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.biscuit.wifi_powersave=0

# One cached process and no empty ones (patches/biscuit/frameworks/base).
# Android 11 kept six around, holding about 46 MB that MemAvailable never
# counts. Services system_server binds at boot, like ExtServices for cache
# quotas and health checks, now exit as soon as they are unbound.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.config.max_cached_processes=1

# Every OOM adjustment trims. A quick one skips trimming when a process
# stays cached, so the permission controller and ExtServices lingered empty
# after system_server unbound them. With five app processes a full update
# costs next to nothing.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.config.oomadj_update_quick=false

# Shared storage on sdcardfs instead of FUSE. With FUSE, MediaProvider runs
# all the time to serve /storage/emulated, which cost about 19 MB more. Both
# properties are needed: StorageManagerService resets persist.sys.fuse from
# the settings_fuse flag at boot.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.sys.fuse=false \
    persist.sys.fflag.override.settings_fuse=false

# Nothing on the Dot draws with GLES, so zygote does not load SwiftShader
# into every app it starts.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.zygote.disable_gl_preload=true

# Media metrics clients skip the service, which init/ leaves out.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.media.metrics.enabled=0

# adb over Wi-Fi on port 5555 without a key, as on the CM14 image: nothing on
# the Dot can accept a key prompt.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.adb.tcp.port=5555

# Bring-up: logcat kept in /data/misc/logd, which TWRP can read when Android
# does not get as far as adb. Releases leave this out.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.logd.logpersistd=logcatd \
    persist.logd.logpersistd.buffer=all
