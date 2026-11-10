#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)
$(call inherit-product, device/amazon/athena/device.mk)
$(call inherit-product, vendor/lineage/config/common_full_tablet_wifionly.mk)

# The Kiosk Satellite Edition layer. The echo canceller it brings for the
# other Shows loads into Amazon's MT8163 audio HAL, which this Show does not
# have.
ECHO_SHOW_AMZNAEC := false
$(call inherit-product, vendor/kiosk-satellite/config/common.mk)
$(call inherit-product, vendor/kiosk-satellite/config/show.mk)
$(call inherit-product, device/kiosk-satellite/athena/device.mk)

PRODUCT_NAME := lineage_athena
PRODUCT_DEVICE := athena
PRODUCT_MANUFACTURER := Amzn
PRODUCT_BRAND := google
# (KS) shows in Home Assistant and on the router that the Show runs this
# image.
PRODUCT_MODEL := Echo Show 8 2nd Gen (KS)
PRODUCT_CHARACTERISTICS := tablet,nosdcard
