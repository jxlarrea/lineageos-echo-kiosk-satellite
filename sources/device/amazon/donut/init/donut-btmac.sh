#!/system/bin/sh
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Gives the Bluetooth HAL the Dot's own address. Amazon stores it in the
# idme data as twelve hex digits. The HAL reads it with colons from the file
# ro.bt.bdaddr_path names, and stops without one.

mac=$(cat /proc/idme/bt_mac_addr 2>/dev/null)
case "$mac" in
    [0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]*) ;;
    *) log -t donut-btmac "no Bluetooth address in /proc/idme/bt_mac_addr"; exit 1 ;;
esac
[ "$(echo "$mac" | cut -c1-12)" != 000000000000 ] || { log -t donut-btmac "the stored Bluetooth address is empty"; exit 1; }

formatted=$(echo "$mac" | cut -c1-12 | sed 's/\(..\)/\1:/g; s/:$//')
echo "$formatted" > /data/vendor/bluedroid/macbt
chown bluetooth:bluetooth /data/vendor/bluedroid/macbt
chmod 0644 /data/vendor/bluedroid/macbt
