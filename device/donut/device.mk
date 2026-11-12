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

# System updates are downloaded to /data: the Dot's /cache holds 27 MB.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.biscuit.ota.dir=/data/biscuit-ota
