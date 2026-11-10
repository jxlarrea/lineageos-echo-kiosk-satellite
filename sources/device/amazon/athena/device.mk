#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

# The Echo Show 8 (2nd gen) shipped with Android 9 and a Treble vendor
# partition. The image is a system for that partition as Fire OS left it: it
# builds no vendor image and brings the VNDK 28 libraries Fire OS's HALs
# link against.
PRODUCT_SHIPPING_API_LEVEL := 28
PRODUCT_EXTRA_VNDK_VERSIONS := 28
PRODUCT_BUILD_VENDOR_IMAGE := false

# 1280x800 at 213 dpi
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := tvdpi
TARGET_SCREEN_HEIGHT := 800
TARGET_SCREEN_WIDTH := 1280

$(call inherit-product, frameworks/native/build/tablet-7in-xhdpi-2048-dalvik-heap.mk)

PRODUCT_COPY_FILES += \
    device/amazon/athena/configs/fstab.mt8183:$(TARGET_COPY_OUT_SYSTEM)/etc/fstab.mt8183

# SELinux mapping files under the version name Fire OS's vendor policy uses
PRODUCT_PACKAGES += \
    athena_28.0.0.cil \
    athena_28.0.0.compat.cil

# Wi-Fi
PRODUCT_PACKAGES += \
    wpa_supplicant \
    athena_supplicant_manifest

PRODUCT_COPY_FILES += \
    device/amazon/athena/configs/init.athena.rc:$(TARGET_COPY_OUT_SYSTEM)/etc/init/init.athena.rc
