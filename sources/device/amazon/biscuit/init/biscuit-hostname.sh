#!/system/bin/sh
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Names the Dot echo-dot and the last three characters of its serial, as the
# CM14 image did, so each Dot shows up under its own name on the network:
# Android sends the device name as the DHCP hostname (rro/NetworkStack). Only
# the default name, the model, is replaced.

current="$(settings get global device_name)"
model="$(getprop ro.product.model)"
[ "$current" = "null" ] || [ -z "$current" ] || [ "$current" = "$model" ] || exit 0

serial="$(getprop ro.serialno | tr -cd 'A-Za-z0-9' | tr 'A-Z' 'a-z')"
[ -n "$serial" ] && [ "$serial" != "unknown" ] || exit 0
suffix="$(echo "$serial" | tail -c 4)"
prefix="$(echo "$model" | tr 'A-Z' 'a-z' | tr -cs 'a-z0-9' '-' | sed 's/^-//; s/-$//')"

settings put global device_name "${prefix:-echo-dot}-$suffix"
