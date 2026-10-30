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

# The service Kiosk Satellite drives the Dot's ring, buttons, volume,
# microphone, Bluetooth and Wi-Fi through, and its command.
PRODUCT_PACKAGES += \
    BiscuitService \
    biscuit_service

# Kiosk Satellite on first boot, with its grants and device owner, as on
# CM14. curl fetches it from GitHub.
PRODUCT_PACKAGES += \
    curl \
    ks-install

# What Kiosk Satellite looks for on a Dot: the Action button and the volume
# buttons as BiscuitService broadcasts, and Amazon's wake word engine.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.biscuit.action_broadcast=1 \
    ro.biscuit.pryon=1 \
    ro.biscuit.volume_claim=1

# System updates from this repository's GitHub releases through BiscuitService
# and TWRP (biscuit-service/SystemUpdate.java, ota/). scripts/build.sh sets
# BISCUIT_RELEASE from the newest release heading in CHANGELOG-biscuit.md,
# with -dev after it while an Unreleased heading is above it.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/ota/twrp-update.sh:$(TARGET_COPY_OUT_SYSTEM)/etc/biscuit-ota/twrp-update.sh \
    $(LOCAL_PATH)/ota/release-key.pem:$(TARGET_COPY_OUT_SYSTEM)/etc/biscuit-ota/release-key.pem

# In system/build.prop, where releasetools.py reads the release from.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.biscuit.ota=1
ifneq ($(BISCUIT_RELEASE),)
PRODUCT_PROPERTY_OVERRIDES += \
    ro.biscuit.release=$(BISCUIT_RELEASE)
endif

# Media volume 11 of 15 on a fresh install, which Kiosk Satellite shows as
# 75%. With Kiosk Satellite's media player and assistant volumes at their
# default 100, that is the level that works best for the wake word.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.config.media_vol_default=11

# The speaker output never goes to standby (issue #21). Every standby
# switches the amplifier off, so a wake chime after a few seconds of silence
# popped when it came back on. This is the longest delay AudioFlinger takes,
# about 49 days of silence. Bluetooth outputs still idle after the usual 3
# seconds (patches/biscuit/frameworks/av).
PRODUCT_PROPERTY_OVERRIDES += \
    ro.audio.flinger_standbytime_ms=4294967295

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
    $(LOCAL_PATH)/overlay/frameworks/base/packages/SettingsProvider \
    $(LOCAL_PATH)/overlay/lineage-sdk

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

# No safe volume limit on the line out jack. Android held it at 10 of 15,
# about 14 dB down, until someone accepted a warning the Dot cannot show.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    audio.safemedia.bypass=true

# Media metrics clients skip the service, which init/ leaves out.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.media.metrics.enabled=0

# adb over Wi-Fi on port 5555 without a key, as on the CM14 image: nothing on
# the Dot can accept a key prompt.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.adb.tcp.port=5555
