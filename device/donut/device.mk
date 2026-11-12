#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# What only the Echo Dot (3rd gen) gets on top of its own device tree in
# sources/device/amazon/donut and the layer both Echo Dots share
# (vendor/kiosk-satellite/config/dot.mk). stage-tree.sh copies device/donut/
# to device/kiosk-satellite/donut in the tree.
#

$(call inherit-product, vendor/kiosk-satellite/config/dot.mk)

LOCAL_PATH := device/kiosk-satellite/donut

# Shared storage stays on FUSE, unlike on the Echo Dot (2nd gen): Amazon's
# kernel release for this Dot has no sdcardfs source.

# Kiosk Satellite applies its settings for an Echo image, such as the Dot's
# microphone gain, when the image says it takes system updates through
# BiscuitService. The Dot has no update script yet (ota/ on the Echo Dot
# (2nd gen)), and without one BiscuitService offers no update.
# scripts/build.sh sets BISCUIT_RELEASE from CHANGELOG-donut.md.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.biscuit.ota=1
ifneq ($(BISCUIT_RELEASE),)
PRODUCT_PROPERTY_OVERRIDES += \
    ro.biscuit.release=$(BISCUIT_RELEASE)
endif
