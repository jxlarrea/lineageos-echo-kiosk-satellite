#!/usr/bin/env bash
# The Echo Dot and the Echo Shows share common/config/common.mk. Only the
# Shows inherit common/config/show.mk, and what is in the shared layer must
# not be one kind's parts. 2026.10.7 to 2026.10.9 shipped the Shows' layer on
# the Dot because the Dot inherited what had become the Shows' layer.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"
fail() { echo "FAIL $*" >&2; exit 1; }

inherits() {
  grep -h '^+\$(call inherit-product' patches/"$1"/device/amazon/"$1"/*.patch
}

inherits biscuit | grep -q 'vendor/kiosk-satellite/config/common.mk' || fail "biscuit does not inherit common.mk"
! inherits biscuit | grep -q 'vendor/kiosk-satellite/config/show.mk' || fail "biscuit inherits the Echo Shows' show.mk"
! grep -rq 'kiosk-satellite/config/show.mk' device/biscuit sources/device/amazon/biscuit || fail "the Dot's layer pulls in show.mk"
for show in crown cronos; do
  inherits "$show" | grep -q 'vendor/kiosk-satellite/config/common.mk' || fail "$show does not inherit common.mk"
  inherits "$show" | grep -q 'vendor/kiosk-satellite/config/show.mk' || fail "$show does not inherit show.mk"
done

# Each Echo Show gets its own device layer and not the other's.
inherits crown | grep -q 'device/kiosk-satellite/crown/device.mk' || fail "crown does not inherit its device layer"
inherits cronos | grep -q 'device/kiosk-satellite/cronos/device.mk' || fail "cronos does not inherit its device layer"
! inherits crown | grep -q 'kiosk-satellite/cronos' || fail "crown inherits the Echo Show 5's layer"
! inherits cronos | grep -q 'kiosk-satellite/crown' || fail "cronos inherits the Echo Show 8's layer"

# One Show's settings in neither shared layer.
for part in ro.echo.hostname= asr_capture ro.vendor.amznaec.engine= ro.vendor.amznaec.enable= \
    media_vol_default noise_suppression privacy_hold; do
  for layer in common/config/common.mk common/config/show.mk; do
    ! grep -Fq "$part" "$layer" || fail "$layer has $part, which each device sets"
  done
done

# Neither kind's parts in the shared layer.
for part in EchoService echo_service echo-ks-install init.kiosk-satellite.rc ro.echo. \
    BiscuitService biscuit_service ks-install ro.biscuit.; do
  ! grep -Fq "$part" common/config/common.mk || fail "common.mk has $part, which is not for every Echo"
done

echo "PASS device layers"
