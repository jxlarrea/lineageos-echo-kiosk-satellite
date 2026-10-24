#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

$(call inherit-product, frameworks/native/build/phone-hdpi-512-dalvik-heap.mk)

# AAPT
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := mdpi

# Audio
# AOSP's stub primary HAL for now, which plays into nothing, with AOSP's
# generic policy. Without a primary HAL audioserver keeps restarting.
PRODUCT_PACKAGES += \
    android.hardware.audio@2.0-impl \
    android.hardware.audio.effect@2.0-impl \
    audio.primary.default

PRODUCT_COPY_FILES += \
    frameworks/av/services/audiopolicy/config/audio_policy_configuration_generic.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml

# Bluetooth
# The profile overlay goes into the Bluetooth app itself: as a runtime
# overlay the sink services resolve as disabled while the profile list
# expects them.
DEVICE_PACKAGE_OVERLAYS += \
    $(LOCAL_PATH)/overlay

PRODUCT_ENFORCE_RRO_EXCLUDED_OVERLAYS += \
    $(LOCAL_PATH)/overlay/packages/apps/Bluetooth

PRODUCT_PACKAGES += \
    android.hardware.bluetooth@1.0-impl \
    android.hardware.bluetooth@1.0-service \
    libbt-vendor

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml

# Connsys: MediaTek's mt66xx modules, which init.insmod.sh loads from
# init.insmod.cfg, and Fire OS 6's loader, launcher and firmware
# (vendor/amazon/biscuit, from scripts/extract-biscuit-blobs.sh).
PRODUCT_PACKAGES += \
    mt66xx_bt \
    mt66xx_common \
    mt66xx_wlan

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/init.insmod.cfg:$(TARGET_COPY_OUT_VENDOR)/etc/init.insmod.cfg

# Display
# The Dot has no screen and no GPU driver. hwcomposer.biscuit reports one
# display nobody sees, SurfaceFlinger composes with SwiftShader and
# gralloc.default allocates from ashmem (vendor.prop picks all three).
PRODUCT_PACKAGES += \
    gralloc.default \
    hwcomposer.biscuit \
    libEGL_swiftshader \
    libGLESv1_CM_swiftshader \
    libGLESv2_swiftshader

# Fstab
# The first PRODUCT_COPY_FILES entry for a destination wins, so these come
# before mt8163-common's unslotted fstab.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/fstab.mt8163:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.mt8163 \
    $(LOCAL_PATH)/configs/fstab.mt8163:$(TARGET_COPY_OUT_RAMDISK)/fstab.mt8163

# Init
PRODUCT_PACKAGES += \
    init.target.biscuit.rc

# Sensors (ambient light)
PRODUCT_PACKAGES += \
    sensors.biscuit

# Screen
TARGET_SCREEN_DENSITY := 213
TARGET_SCREEN_HEIGHT := 1
TARGET_SCREEN_WIDTH := 1

# Shipping API level (Fire OS 6, Android 7.1)
PRODUCT_SHIPPING_API_LEVEL := 25

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(LOCAL_PATH)

# Wi-Fi
PRODUCT_PACKAGES += \
    android.hardware.wifi@1.0-service-lazy \
    libwifi-hal

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/wifi/wpa_supplicant.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant.conf \
    $(LOCAL_PATH)/configs/wifi/wpa_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant_overlay.conf

# Vendor partition
TARGET_HAS_VENDOR_PARTITION := false

# Inherit from mt8163-common
$(call inherit-product, device/amazon/mt8163-common/mt8163.mk)

# Inherit the proprietary files
$(call inherit-product, vendor/amazon/biscuit/biscuit-vendor.mk)
