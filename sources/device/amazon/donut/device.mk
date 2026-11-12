#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

# ART heaps, as on the Echo Dot (2nd gen): frameworks/native's
# phone-hdpi-512-dalvik-heap.mk with a higher target utilization.
PRODUCT_PROPERTY_OVERRIDES += \
    dalvik.vm.heapstartsize=5m \
    dalvik.vm.heapgrowthlimit=48m \
    dalvik.vm.heapsize=128m \
    dalvik.vm.heaptargetutilization=0.9 \
    dalvik.vm.heapminfree=512k \
    dalvik.vm.heapmaxfree=2m

# AAPT
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := mdpi

# Audio: Android's stub HAL until the Dot's own is in.
PRODUCT_PACKAGES += \
    android.hardware.audio@2.0-impl \
    android.hardware.audio.effect@2.0-impl \
    android.hardware.audio.service \
    audio.a2dp.default \
    audio.primary.default \
    audio.r_submix.default \
    audio.usb.default

PRODUCT_COPY_FILES += \
    frameworks/av/services/audiopolicy/config/audio_policy_configuration_generic.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/primary_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/primary_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml

# Buttons
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/keylayout/gpio-keys.kl:$(TARGET_COPY_OUT_SYSTEM)/usr/keylayout/gpio-keys.kl

# Display
# The Dot has no screen and no GPU driver. hwcomposer.donut reports one
# display nobody sees, SurfaceFlinger composes with SwiftShader and
# gralloc.default allocates from ashmem (vendor.prop picks all three).
PRODUCT_PACKAGES += \
    android.hardware.graphics.allocator@2.0-impl \
    android.hardware.graphics.allocator@2.0-service \
    android.hardware.graphics.composer@2.1-service \
    android.hardware.graphics.mapper@2.0-impl \
    gralloc.default \
    hwcomposer.donut \
    libEGL_swiftshader \
    libGLESv1_CM_swiftshader \
    libGLESv2_swiftshader

# Fstab
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/fstab.mt8167:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.mt8167

# Gatekeeper and keymaster in software: the image does not use the TEE.
PRODUCT_PACKAGES += \
    android.hardware.gatekeeper@1.0-service.software \
    android.hardware.keymaster@4.1-service

# Health
PRODUCT_PACKAGES += \
    android.hardware.health@2.1-impl \
    android.hardware.health@2.1-service

# Init
PRODUCT_PACKAGES += \
    init.mt8167.rc \
    init.mt8167.usb.rc \
    ueventd.mt8167.rc

# Screen
TARGET_SCREEN_DENSITY := 213
TARGET_SCREEN_HEIGHT := 1
TARGET_SCREEN_WIDTH := 1

# Shipping API level (Fire OS 6, Android 7.1)
PRODUCT_SHIPPING_API_LEVEL := 25

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(LOCAL_PATH)

# USB
PRODUCT_PACKAGES += \
    android.hardware.usb@1.0-service.basic

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml
