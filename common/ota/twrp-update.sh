#!/sbin/sh
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Kiosk Satellite Edition: install a system update on an Echo Show from
# inside TWRP.
#
# EchoService copies this to /data/system/echo-ota with the release zip and
# its update-binary and reboots into recovery with an openrecoveryscript that
# runs this. TWRP's own install command stops the script when an install
# fails and leaves the Show waiting in TWRP, so this runs the zip's updater
# itself. The result goes to /data/system/echo-ota/result for EchoService to
# read once Android is back.
#
# This reboots the Show itself. The one case it does not is a failure after
# the updater started writing system: that system may not boot, so the Show
# stays in TWRP, where adb works and the zip can be installed by hand.

DIR=${ECHO_OTA_DIR:-/data/system/echo-ota}
TMP=${ECHO_OTA_TMP:-/tmp}
REBOOT=${ECHO_OTA_REBOOT:-/sbin/reboot}
UPDATER=$TMP/echo-ota-updater

# TWRP mounts /data when it starts. This is in case it did not.
grep -q " /data " /proc/mounts || mount /data

exec >>"$DIR/twrp.log" 2>&1
echo "=== $(date)"

finish() {
    echo "result: $*"
    echo "$*" >"$DIR/result"
    sync
}

if [ ! -f "$DIR/update.zip" ]; then
    finish "failed update.zip is missing"
    "$REBOOT"
    exit 0
fi
if ! cp "$DIR/update-binary" "$UPDATER" || ! chmod 0755 "$UPDATER"; then
    finish "failed update-binary is missing"
    "$REBOOT"
    exit 0
fi

# The updater writes its progress commands to the file descriptor named in
# its second argument. 1 sends them to $OUT, which goes into the log after.
OUT=$TMP/echo-ota-updater.out
"$UPDATER" 3 1 "$DIR/update.zip" >"$OUT" 2>&1
rc=$?
cat "$OUT"
rm -f "$UPDATER"

if [ $rc -eq 0 ]; then
    finish "ok"
    "$REBOOT"
    exit 0
fi

# The updater writes system first. A failure before that leaves the Show as
# it was, so it can go back to Android.
if grep -q "Patching system image" "$OUT"; then
    finish "failed updater exited with $rc while writing system, install the release by hand"
    exit 0
fi
finish "failed updater exited with $rc"
"$REBOOT"
exit 0
