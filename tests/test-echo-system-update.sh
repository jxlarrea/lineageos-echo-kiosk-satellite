#!/usr/bin/env bash
# Run the Echo Shows' TWRP update script against a fake getprop, updater and
# reboot.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$REPO_ROOT/common/ota/twrp-update.sh"
TMPDIR="$(TMPDIR=/tmp mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

sh -n "$SCRIPT"

# A fake updater that records it ran.
cat >"$TMPDIR/updater" <<'EOF'
#!/bin/sh
: >"$CASE/updated"
case "$MODE" in
    ok) echo "ui_print Patching system image unconditionally..."; exit 0 ;;
    early) echo "ui_print assert failed"; exit 7 ;;
    late) echo "ui_print Patching system image unconditionally..."; exit 7 ;;
esac
EOF
chmod +x "$TMPDIR/updater"

# run <TWRP version> <updater mode>
run() {
    CASE="$TMPDIR/case"
    rm -rf "$CASE"
    mkdir -p "$CASE/dir" "$CASE/tmp"
    : >"$CASE/dir/update.zip"
    cp "$TMPDIR/updater" "$CASE/dir/update-binary"
    printf '#!/bin/sh\n[ "$1" = ro.twrp.version ] && echo "%s"\n' "$1" >"$CASE/getprop"
    printf '#!/bin/sh\n: >"%s/rebooted"\n' "$CASE" >"$CASE/reboot"
    chmod +x "$CASE/getprop" "$CASE/reboot"
    CASE="$CASE" MODE="$2" ECHO_OTA_DIR="$CASE/dir" ECHO_OTA_TMP="$CASE/tmp" \
        ECHO_OTA_REBOOT="$CASE/reboot" ECHO_OTA_GETPROP="$CASE/getprop" \
        sh "$SCRIPT"
    RESULT=$(cat "$CASE/dir/result")
    UPDATED=$([ -e "$CASE/updated" ] && echo yes || echo no)
    REBOOTED=$([ -e "$CASE/rebooted" ] && echo yes || echo no)
}

expect() {
    [[ "$RESULT" == "$1" && "$UPDATED" == "$2" && "$REBOOTED" == "$3" ]] || {
        echo "FAIL: got '$RESULT' updated=$UPDATED rebooted=$REBOOTED, want '$1' updated=$2 rebooted=$3" >&2
        exit 1
    }
}

run 3.7.0_9-0 ok; expect "ok" yes yes
run 3.7.0_9-0 early; expect "failed updater exited with 7" yes yes
run 3.7.0_9-0 late; expect "failed updater exited with 7 while writing system, install the release by hand" yes no
# Any other TWRP writes nothing and goes back to Android (issue #13).
run 3.2.3-0 ok
expect "failed TWRP 3.2.3-0 cannot install this image, update to amonet 2 and TWRP 3.7.0_9-0" no yes
run "" ok
expect "failed TWRP of unknown version cannot install this image, update to amonet 2 and TWRP 3.7.0_9-0" no yes

# The build ships the script where EchoService copies it from.
grep -Fq '$(LOCAL_PATH)/ota/twrp-update.sh:$(TARGET_COPY_OUT_SYSTEM)/etc/echo-ota/twrp-update.sh' \
    "$REPO_ROOT/common/config/show.mk"
grep -Fq '"/system/etc/echo-ota/twrp-update.sh"' \
    "$REPO_ROOT/common/echo-service/src/me/jxl/echo/service/SystemUpdate.java"

echo "PASS Echo Show system update script"
