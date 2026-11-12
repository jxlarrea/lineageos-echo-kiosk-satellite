#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# What only the Echo Dot (2nd gen) gets on top of its device tree and the
# layer both Echo Dots share (vendor/kiosk-satellite/config/dot.mk).
# stage-tree.sh copies device/biscuit/ to device/kiosk-satellite/biscuit in
# the tree.
#

$(call inherit-product, vendor/kiosk-satellite/config/dot.mk)

LOCAL_PATH := device/kiosk-satellite/biscuit

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

# Always on mains: mt8163-common then keeps all four cores online.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.vendor.power.wall_powered=true

# Shared storage on sdcardfs instead of FUSE. With FUSE, MediaProvider runs
# all the time to serve /storage/emulated, which cost about 19 MB more. Both
# properties are needed: StorageManagerService resets persist.sys.fuse from
# the settings_fuse flag at boot.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.sys.fuse=false \
    persist.sys.fflag.override.settings_fuse=false
