#!/usr/bin/env bash
# Sign an Echo release zip for system updates: writes <zip>.sig, a
# SHA256withRSA signature that the Echo checks against the public key its
# image ships before it installs the zip. That is BiscuitService and
# device/biscuit/ota/release-key.pem on the Echo Dot, EchoService and
# common/ota/release-key.pem on the Echo Shows. A release without the .sig
# is only installed by hand. Name the zip with the device's codename in it,
# as the build does, since each Echo only takes its own.
#
# The private key stays outside the repository, in
# ~/.config/biscuit-ota/release-key.pem unless BISCUIT_OTA_KEY says otherwise.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KEY="${BISCUIT_OTA_KEY:-$HOME/.config/biscuit-ota/release-key.pem}"

[[ $# -eq 1 && -f "$1" ]] || { echo "usage: $0 <release.zip>" >&2; exit 2; }
[[ -f "$KEY" ]] || { echo "ERROR: no private key at $KEY (set BISCUIT_OTA_KEY)" >&2; exit 1; }

case "$(basename "$1")" in
  *biscuit*.zip) PUBLIC="$REPO_ROOT/device/biscuit/ota/release-key.pem" ;;
  *cronos*.zip|*crown*.zip) PUBLIC="$REPO_ROOT/common/ota/release-key.pem" ;;
  *)
    echo "ERROR: name the zip with the device's codename (biscuit, cronos or crown)" >&2
    exit 1
    ;;
esac

ZIP="$1"
openssl dgst -sha256 -sign "$KEY" -out "$ZIP.sig" "$ZIP"
# Check it the way the Echo will, with the key its image ships.
openssl dgst -sha256 -verify "$PUBLIC" -signature "$ZIP.sig" "$ZIP"
echo "Wrote $ZIP.sig"
