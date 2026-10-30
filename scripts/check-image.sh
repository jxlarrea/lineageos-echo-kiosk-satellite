#!/usr/bin/env bash
# Check a built image before it is released: that it carries its own device's
# Kiosk Satellite Edition parts, none of another device's and the release
# its CHANGELOG-<device>.md gives it. It reads the target files the OTA zip is made from
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

# What sets the two Echo Shows apart (device/<device>/device.mk and
# patches/<device>), so one Show's settings never reach the other's image.
# Each value is what the device sets, or empty where it sets none.
expect() {
  local where="$1" name="$2" want="$3" got
  got="$("$where" "$name")"
  [[ "$got" == "$want" ]] || fail "$name is '$got', $DEVICE should have '$want'"
}

# The ASR pipeline's state in asp.cfg: true for passthrough.
asr_passthrough() {
  unzip -p "$TARGET_FILES" SYSTEM/vendor/etc/audio-algorithms/asp.cfg |
    grep -A1 '"ASR"' | grep -o 'true\|false'
}

# grep -c reads to the end, so unzip never stops early on a closed pipe.
wrapper_has_asr_capture() {
  (( $(unzip -p "$TARGET_FILES" SYSTEM/vendor/lib/hw/audio.primary.amazon_wrapper.so | grep -ac 'asr_capture') > 0 ))
}

# The boot animation for the device's screen height (show.mk), and the
# wallpaper.
boot_animation() {
  [[ "$(unzip -p "$TARGET_FILES" SYSTEM/media/bootanimation.zip | sha256sum)" == \
     "$(sha256sum <"$REPO_ROOT/common/media/bootanimation-$1.zip")" ]] ||
    fail "media/bootanimation.zip is not bootanimation-$1.zip"
  must_have "media/wallpaper\.png$"
  expect default_prop ro.config.wallpaper /system/media/wallpaper.png
}

case "$DEVICE" in
  crown)
    expect default_prop ro.echo.hostname echo-show-8-ks
    expect default_prop ro.echo.privacy_hold 1
    expect default_prop persist.sys.audio.noise_suppression 0
    expect prop ro.vendor.audio.asr_capture 1
    expect prop ro.vendor.audio.asr_capture_gain_db 24
    expect prop ro.vendor.amznaec.enable 0
    expect prop ro.vendor.amznaec.engine 3
    expect prop ro.config.media_vol_default 8
    [[ "$(asr_passthrough)" == false ]] || fail "asp.cfg has the ASR pipeline in passthrough"
    wrapper_has_asr_capture || fail "the audio HAL wrapper cannot send captures through the ASR pipeline"
    boot_animation 800
    ;;
  cronos)
    expect default_prop ro.echo.hostname echo-show-5-ks
    expect default_prop ro.echo.privacy_hold ""
    expect default_prop persist.sys.audio.noise_suppression ""
    expect prop ro.vendor.audio.asr_capture ""
    expect prop ro.vendor.audio.asr_capture_gain_db ""
    expect prop ro.vendor.amznaec.enable ""
    expect prop ro.vendor.amznaec.engine 2
    expect prop ro.config.media_vol_default 10
    [[ "$(asr_passthrough)" == true ]] || fail "asp.cfg has the ASR pipeline on, which is the Echo Show 8's"
    wrapper_has_asr_capture && fail "the audio HAL wrapper has the Echo Show 8's ASR capture"
    boot_animation 480
    ;;
  biscuit)
    must_not_have "media/wallpaper\.png$"
    expect default_prop ro.config.wallpaper ""
    for height in 480 800; do
      [[ "$(unzip -p "$TARGET_FILES" SYSTEM/media/bootanimation.zip | sha256sum)" != \
         "$(sha256sum <"$REPO_ROOT/common/media/bootanimation-$height.zip")" ]] ||
        fail "media/bootanimation.zip is the Echo Shows'"
    done
    ;;
esac

[[ "$(prop ro.product.device)" == "$DEVICE" || "$(prop ro.product.system.device)" == "$DEVICE" ]] ||
  fail "ro.product.device is not $DEVICE"
[[ "$(prop "$RELEASE_PROP_NAME")" == "$RELEASE" ]] ||
  fail "$RELEASE_PROP_NAME is '$(prop "$RELEASE_PROP_NAME")', CHANGELOG-$DEVICE.md says $RELEASE"
unzip -p "$OTA" "$RELEASE_PROP_FILE" 2>/dev/null | grep -qx "$RELEASE_PROP_NAME=$RELEASE" ||
  fail "the OTA zip's release.prop does not say $RELEASE_PROP_NAME=$RELEASE"
unzip -p "$OTA" "$RELEASE_PROP_FILE" 2>/dev/null | grep -qx "ro.product.device=$DEVICE" ||
  fail "the OTA zip's release.prop does not say ro.product.device=$DEVICE"
# An install writes exactly this image: no backuptool, which would bring
# back what another image left in /system/addon.d, such as GApps or Magisk.
unzip -p "$OTA" META-INF/com/google/android/updater-script 2>/dev/null | grep -q backuptool &&
  fail "the OTA zip runs backuptool, so another image's addon.d survives an install"

if (( failures > 0 )); then
  echo "$DEVICE $RELEASE: $failures problem(s) in $(basename "$OTA")"
  exit 1
fi
echo "PASS $DEVICE $RELEASE: $(basename "$OTA")"
