#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

# adb comes up without a key prompt: the Dot has no screen to answer one on.
WITH_ADB_INSECURE := true

$(call inherit-product, $(SRC_TARGET_DIR)/product/generic_no_telephony.mk)
$(call inherit-product, device/amazon/donut/device.mk)
$(call inherit-product, vendor/lineage/config/common_mini_tablet_wifionly.mk)

PRODUCT_NAME := lineage_donut
PRODUCT_DEVICE := donut
PRODUCT_MANUFACTURER := Amzn
PRODUCT_BRAND := google
PRODUCT_MODEL := Echo Dot 3rd Gen (KS)
PRODUCT_CHARACTERISTICS := tablet,nosdcard
PRODUCT_LOCALES := en_US
