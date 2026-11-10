#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)
$(call inherit-product, device/amazon/athena/device.mk)
$(call inherit-product, vendor/lineage/config/common_full_tablet_wifionly.mk)

PRODUCT_NAME := lineage_athena
PRODUCT_DEVICE := athena
PRODUCT_MANUFACTURER := Amzn
PRODUCT_BRAND := google
PRODUCT_MODEL := Echo Show 8 (2nd Gen)
PRODUCT_CHARACTERISTICS := tablet,nosdcard
