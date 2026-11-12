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

# Audio: the Dot's own HAL on the kernel's sound card (audio/), with none of
# Fire OS's audio libraries.
PRODUCT_PACKAGES += \
    android.hardware.audio@2.0-impl \
    android.hardware.audio.effect@2.0-impl \
    android.hardware.audio.service \
    audio.a2dp.default \
    audio.primary.donut \
    audio.r_submix.default

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/audio/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/a2dp_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/a2dp_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml

# Bluetooth
# The profile overlay goes into the Bluetooth app itself, as on the Echo Dot
# (2nd gen): as a runtime overlay the sink services resolve as disabled while
# the profile list expects them.
DEVICE_PACKAGE_OVERLAYS += \
    $(LOCAL_PATH)/overlay

PRODUCT_ENFORCE_RRO_EXCLUDED_OVERLAYS += \
    $(LOCAL_PATH)/overlay/packages/apps/Bluetooth

PRODUCT_PACKAGES += \
    android.hardware.bluetooth@1.0-impl \
    android.hardware.bluetooth@1.0-service \
    donut-btmac \
    libbt-vendor

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml

# Buttons
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/keylayout/gpio-keys.kl:$(TARGET_COPY_OUT_SYSTEM)/usr/keylayout/gpio-keys.kl \
    $(LOCAL_PATH)/keylayout/gpio-privacy.kl:$(TARGET_COPY_OUT_SYSTEM)/usr/keylayout/gpio-privacy.kl

# No configstore HAL: SurfaceFlinger takes its settings from properties.
# The service has nothing to register here and crash-looped.
PRODUCT_PACKAGES += \
    disable_configstore

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
    init.mt8167.usb.rc

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/init/ueventd.mt8167.rc:$(TARGET_COPY_OUT_VENDOR)/ueventd.rc

# Screen
TARGET_SCREEN_DENSITY := 213
TARGET_SCREEN_HEIGHT := 1
TARGET_SCREEN_WIDTH := 1

# Shipping API level (Fire OS 6, Android 7.1)
PRODUCT_SHIPPING_API_LEVEL := 25

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(LOCAL_PATH)

# Light ring: the ring daemon, its animations and biscuit-ledctl to drive it,
# shared with the Echo Dot (2nd gen) (vendor/kiosk-satellite/leds). Both
# Dots have the same IS31FL3236 driver interface for 12 LEDs.
PRODUCT_PACKAGES += \
    biscuit-ledctl \
    biscuit-ledd

PRODUCT_COPY_FILES += \
    $(call find-copy-subdir-files,*.animation,vendor/kiosk-satellite/leds/animations,$(TARGET_COPY_OUT_SYSTEM)/etc/biscuit-ledd)

# The network stack and tethering inside system_server, as on the other
# Echos, and the device name as the DHCP hostname (rro/NetworkStack).
PRODUCT_PACKAGES += \
    InProcessNetworkStack \
    com.android.tethering.inprocess \
    DonutNetworkStackOverlay

# Android's features, from the tablet list the Echo Dot (2nd gen) also has.
# Without android.software.device_admin Kiosk Satellite cannot become the
# device owner. The Dots' layer takes out the ones that only cost memory
# (vendor/kiosk-satellite/dot/permissions).
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/tablet_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/tablet_core_hardware.xml

# USB
PRODUCT_PACKAGES += \
    android.hardware.usb@1.0-service.basic

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.accessory.xml

# Wi-Fi
PRODUCT_PACKAGES += \
    android.hardware.wifi@1.0-service-lazy \
    hostapd \
    libwifi-hal \
    wpa_supplicant

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    $(LOCAL_PATH)/configs/wifi/wpa_supplicant.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant.conf \
    $(LOCAL_PATH)/configs/wifi/wpa_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant_overlay.conf

# The proprietary files: the MT7668's firmware (blobs/donut,
# scripts/extract-donut-blobs.sh).
$(call inherit-product, vendor/amazon/donut/donut-vendor.mk)
