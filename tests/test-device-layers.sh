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

# Neither kind's parts in the shared layer.
for part in EchoService echo_service echo-ks-install init.kiosk-satellite.rc ro.echo. \
    BiscuitService biscuit_service ks-install ro.biscuit.; do
  ! grep -Fq "$part" common/config/common.mk || fail "common.mk has $part, which is not for every Echo"
done

echo "PASS device layers"
