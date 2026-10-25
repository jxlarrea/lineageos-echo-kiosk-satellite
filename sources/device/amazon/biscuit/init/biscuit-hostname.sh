#!/system/bin/sh
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Names the Dot after its model and the last three characters of its serial,
# echo-dot-ks-he4 for example, so each Dot shows up under its own name on the
# network: Android sends the device name as the DHCP hostname
# (rro/NetworkStack). Only the default name, the model, is replaced, and the
# name the CM14 image gave, echo-dot and the same three characters, so a Dot
# moved from CM14 shows that it runs this image.

serial="$(getprop ro.serialno | tr -cd 'A-Za-z0-9' | tr 'A-Z' 'a-z')"
[ -n "$serial" ] && [ "$serial" != "unknown" ] || exit 0
suffix="$(echo "$serial" | tail -c 4)"

current="$(settings get global device_name)"
model="$(getprop ro.product.model)"
[ "$current" = "null" ] || [ -z "$current" ] || [ "$current" = "$model" ] ||
    [ "$current" = "echo-dot-$suffix" ] || exit 0

prefix="$(echo "$model" | tr 'A-Z' 'a-z' | tr -cs 'a-z0-9' '-' | sed 's/^-//; s/-$//')"

settings put global device_name "${prefix:-echo-dot}-$suffix"
