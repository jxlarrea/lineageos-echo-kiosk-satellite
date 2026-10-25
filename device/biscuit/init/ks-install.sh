#!/system/bin/sh
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Kiosk Satellite Edition: installs Kiosk Satellite on first boot and keeps
# its grants in place on every boot. Runs as root once Android has booted.
#
# Without a screen nothing can accept a permission prompt, so everything the
# kiosk needs is granted here: the microphone, location for the Bluetooth
# proxy, storage and all files access for the File Manager, the battery
# exemption and device owner, which lets Kiosk Satellite update itself
# without a confirmation. The buttons need no accessibility service:
# BiscuitService broadcasts them (ro.biscuit.action_broadcast).
#
# setprop persist.biscuit.ks_install 0 turns this off.

PKG=me.jxl.kiosk_satellite
RELEASES=https://api.github.com/repos/jxlarrea/kiosk-satellite/releases/latest
CERTS=/system/etc/security/cacerts
APK=/data/local/tmp/kiosk-satellite.apk

log() { /system/bin/log -t ks-install "$*"; }
fetch() { curl -sSL --fail --capath "$CERTS" --connect-timeout 20 "$@"; }

[ "$(getprop persist.biscuit.ks_install)" = "0" ] && exit 0

fresh=false
pm path "$PKG" >/dev/null 2>&1 || fresh=true

if $fresh; then
    # Wait for the network: Wi-Fi may still be joining, or not set up yet.
    tries=0
    until fetch -o /dev/null --max-time 20 https://api.github.com; do
        tries=$((tries + 1))
        if [ "$tries" -ge 360 ]; then
            log "no network after an hour, giving up until the next boot"
            exit 1
        fi
        sleep 10
    done

    # The split for this device's ABI, the same file the in-app updater
    # picks, so later updates install over it. The universal APK otherwise.
    release=$(fetch "$RELEASES") || { log "could not read the latest release"; exit 1; }
    abi=$(getprop ro.product.cpu.abi)
    urls=$(echo "$release" | grep -o '"browser_download_url": *"[^"]*\.apk"' | sed 's/.*"\(https[^"]*\)"/\1/')
    url=$(echo "$urls" | grep "\.$abi\.apk$" | head -n 1)
    [ -n "$url" ] || url=$(echo "$urls" | grep -E 'kiosk-satellite-v?[0-9.]+\.apk$' | head -n 1)
    [ -n "$url" ] || { log "no APK in the latest release"; exit 1; }

    log "downloading $url"
    fetch -o "$APK" "$url" || { log "download failed"; rm -f "$APK"; exit 1; }
    if ! pm install -r "$APK" >/dev/null; then
        log "install failed"
        rm -f "$APK"
        exit 1
    fi
    rm -f "$APK"
    log "installed Kiosk Satellite"
fi

for permission in RECORD_AUDIO ACCESS_COARSE_LOCATION ACCESS_FINE_LOCATION \
        READ_EXTERNAL_STORAGE WRITE_EXTERNAL_STORAGE; do
    pm grant "$PKG" "android.permission.$permission" 2>/dev/null
done
# Android 11 gives these only through app ops, each behind its own Settings
# screen: all files access for the File Manager, drawing over other apps,
# which starting itself again from the background needs, changing system
# settings, usage access for the foreground app sensor and installing its
# own updates.
for op in MANAGE_EXTERNAL_STORAGE SYSTEM_ALERT_WINDOW WRITE_SETTINGS GET_USAGE_STATS \
        REQUEST_INSTALL_PACKAGES; do
    appops set "$PKG" "$op" allow 2>/dev/null
done
dumpsys deviceidle whitelist "+$PKG" >/dev/null

# Device owner needs a device with no accounts, which a headless one has.
# Setting it starts Kiosk Satellite for the first time, after the grants
# above, which do not start it. Its first run needs the device owner to read
# the real Wi-Fi MAC address.
if ! dumpsys device_policy | grep -A3 "Device Owner" | grep -q "package="; then
    dpm set-device-owner "$PKG/.KioskAdminReceiver" >/dev/null 2>&1 &&
        log "Kiosk Satellite is the device owner"
fi

# A fresh install opens Kiosk Satellite once, as Android requires before it
# delivers the boot broadcast.
if $fresh; then
    am start -n "$PKG/.ProvisionActivity" >/dev/null 2>&1
fi
