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

# adb over Wi-Fi on port 5555 without a key, as on the CM14 image: nothing on
# the Dot can accept a key prompt.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.adb.tcp.port=5555

# Bring-up: logcat kept in /data/misc/logd, which TWRP can read when Android
# does not get as far as adb. Releases leave this out.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.logd.logpersistd=logcatd \
    persist.logd.logpersistd.buffer=all
