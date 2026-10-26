#!/usr/bin/env bash
# Check a built image before it is released: that it carries its own device's
# Kiosk Satellite Edition parts, none of another device's and the release
# CHANGELOG.md gives it. It reads the target files the OTA zip is made from
# and the OTA zip itself.
#
# usage: DEVICE=biscuit scripts/check-image.sh [target-files.zip [ota.zip]]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORKSPACE_DIR="$(realpath "${ECHO_WORKSPACE:-$REPO_ROOT/workspace}")"
TREE="$(realpath "${LINEAGE_TREE:-$WORKSPACE_DIR/lineage-18.1}")"
DEVICE="${DEVICE:-}"
OUT="$TREE/out/target/product/$DEVICE"

case "$DEVICE" in
  biscuit|crown|cronos) ;;
  *) echo "ERROR: set DEVICE to biscuit, crown or cronos" >&2; exit 2 ;;
esac

TARGET_FILES="${1:-$(ls "$OUT"/obj/PACKAGING/target_files_intermediates/lineage_"$DEVICE"-target_files-*.zip 2>/dev/null | head -n 1)}"
OTA="${2:-$(ls -t "$OUT"/lineage-18.1-*-"$DEVICE".zip 2>/dev/null | head -n 1)}"
[[ -f "$TARGET_FILES" ]] || { echo "ERROR: no target files zip for $DEVICE" >&2; exit 2; }
[[ -f "$OTA" ]] || { echo "ERROR: no OTA zip for $DEVICE" >&2; exit 2; }

# The release the build should be, as scripts/build.sh works it out.
eval "$(sed -n '/^device_name()/,/^}/p;/^release_version()/,/^}/p' "$REPO_ROOT/scripts/build.sh")"
RELEASE="$(release_version)"

FILES="$(unzip -Z1 "$TARGET_FILES")"
BUILD_PROP="$(unzip -p "$TARGET_FILES" SYSTEM/build.prop)"
# The ramdisk's default.prop, where PRODUCT_SYSTEM_DEFAULT_PROPERTIES go.
DEFAULT_PROP="$(unzip -p "$TARGET_FILES" ROOT/default.prop 2>/dev/null || true)"
failures=0

fail() {
  echo "FAIL $*"
  failures=$((failures + 1))
}

# Each argument is an extended regex for a path in the target files.
must_have() {
  local path
  for path in "$@"; do
    grep -Eq "^SYSTEM/($path)" <<<"$FILES" || fail "missing $path"
  done
}

must_not_have() {
  local path
  for path in "$@"; do
    grep -Eq "^SYSTEM/($path)" <<<"$FILES" && fail "has $path, which is not for $DEVICE"
  done
  return 0
}

prop() {
  sed -n "s/^$1=//p" <<<"$BUILD_PROP" | tail -n 1
}

default_prop() {
  sed -n "s/^$1=//p" <<<"$DEFAULT_PROP" | tail -n 1
}

# Every Echo: the Wi-Fi overlay and Amazon's wake word engine with its
# models (common.mk).
must_have "vendor/overlay/EchoWifiOverlay\.apk" "bin/biscuit-pryond$" "etc/init/biscuit-pryond\.rc$" \
  "lib/pryon/libpryon\.so$" "etc/pryon/locales\.txt$" "etc/pryon/alexa-en-US/"

# Each device's own parts, and none of the other kind's. The Echo Dot has
# BiscuitService and its own ks-install. The Echo Shows have EchoService.
DOT_PARTS=(
  "priv-app/BiscuitService/"
  "bin/biscuit_service$"
  "bin/ks-install$"
  "etc/init/init\.kiosk-satellite\.biscuit\.rc$"
  "etc/biscuit-ota/"
)
SHOW_PARTS=(
  "priv-app/EchoService/"
  "bin/echo_service$"
  "bin/echo-ks-install$"
  "etc/init/init\.kiosk-satellite\.rc$"
  "etc/echo-ota/"
)

if [[ "$DEVICE" == biscuit ]]; then
  must_have "${DOT_PARTS[@]}"
  must_not_have "${SHOW_PARTS[@]}"
  RELEASE_PROP_NAME=ro.biscuit.release
  RELEASE_PROP_FILE=META-INF/com/amazon/biscuit/release.prop
  [[ "$(prop ro.biscuit.ota)" == 1 ]] || fail "ro.biscuit.ota is not 1"
  [[ "$(default_prop ro.biscuit.pryon)" == 1 ]] || fail "ro.biscuit.pryon is not 1"
  grep -q '^ro\.echo\.' <<<"$BUILD_PROP" && fail "build.prop has ro.echo properties, which are the Echo Shows'"
  grep -q '^ro\.echo\.' <<<"$DEFAULT_PROP" && fail "default.prop has ro.echo properties, which are the Echo Shows'"
  # A speaker without a screen ships none of these.
  must_not_have "app/webview/" "product/app/webview/" "system_ext/priv-app/SystemUI/"
else
  must_have "${SHOW_PARTS[@]}"
  must_not_have "${DOT_PARTS[@]}"
  RELEASE_PROP_NAME=ro.echo.release
  RELEASE_PROP_FILE=META-INF/com/kiosk-satellite/release.prop
  [[ "$(prop ro.echo.ota)" == 1 ]] || fail "ro.echo.ota is not 1"
  [[ "$(default_prop ro.echo.pryon)" == 1 ]] || fail "ro.echo.pryon is not 1"
  grep -q '^ro\.biscuit\.' <<<"$BUILD_PROP" && fail "build.prop has ro.biscuit properties, which are the Echo Dot's"
  grep -q '^ro\.biscuit\.' <<<"$DEFAULT_PROP" && fail "default.prop has ro.biscuit properties, which are the Echo Dot's"
  # The screen's parts, which the Dot leaves out.
  must_have "product/app/webview/" "system_ext/priv-app/SystemUI/"
  # What the Echo Shows leave out (common/Android.bp).
  must_not_have "priv-app/Updater/" "priv-app/TeleService/" "priv-app/LineageSetupWizard/" \
    "system_ext/priv-app/Camera2/" "system_ext/priv-app/Gallery2/" "product/app/Eleven/"
fi

[[ "$(prop ro.product.device)" == "$DEVICE" || "$(prop ro.product.system.device)" == "$DEVICE" ]] ||
  fail "ro.product.device is not $DEVICE"
[[ "$(prop "$RELEASE_PROP_NAME")" == "$RELEASE" ]] ||
  fail "$RELEASE_PROP_NAME is '$(prop "$RELEASE_PROP_NAME")', CHANGELOG.md says $RELEASE"
unzip -p "$OTA" "$RELEASE_PROP_FILE" 2>/dev/null | grep -qx "$RELEASE_PROP_NAME=$RELEASE" ||
  fail "the OTA zip's release.prop does not say $RELEASE_PROP_NAME=$RELEASE"

if (( failures > 0 )); then
  echo "$DEVICE $RELEASE: $failures problem(s) in $(basename "$OTA")"
  exit 1
fi
echo "PASS $DEVICE $RELEASE: $(basename "$OTA")"
