#!/usr/bin/env bash
# Put the Echo Show 8 (2nd gen)'s vendor image in the tree, as
# vendor/amazon/athena/vendor.img. The image runs on the vendor partition of
# Fire OS 7575.1 (PS75751/7809), and its update zip writes that partition, so
# the image installs whatever system was on the Show before. The repository
# does not hold the image. This takes it from a dump of the vendor partition
# of a Show on that Fire OS version and checks it against the checksum in
# blobs/athena/vendor.img.sha256.
#
# usage: scripts/athena-vendor.sh <vendor partition dump>
#
# To dump the partition from TWRP, on a Show that was on Fire OS 7575.1 when
# it was unlocked and whose vendor partition nothing wrote since:
#   adb exec-out 'dd if=/dev/block/platform/bootdevice/by-name/vendor bs=4194304 2>/dev/null' > vendor-dump.img
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORKSPACE_DIR="${ECHO_WORKSPACE:-$REPO_ROOT/workspace}"
TREE="${LINEAGE_TREE:-$WORKSPACE_DIR/lineage-18.1}"
SUM_FILE="$REPO_ROOT/blobs/athena/vendor.img.sha256"
OUT="$TREE/vendor/amazon/athena/vendor.img"

[[ $# -eq 1 && -f "$1" ]] || { echo "usage: $0 <vendor partition dump>" >&2; exit 2; }
[[ -d "$TREE/build" ]] || { echo "ERROR: LineageOS 18.1 not synced at $TREE" >&2; exit 1; }
for tool in python3 sha256sum; do
  command -v "$tool" >/dev/null || { echo "ERROR: required tool not found: $tool" >&2; exit 1; }
done

mkdir -p "$(dirname "$OUT")"
TMP="$OUT.tmp"
trap 'rm -f "$TMP"' EXIT

# The partition is larger than the file system Amazon's build put in it, and
# what follows the file system differs from Show to Show. Keep only the file
# system, so every dump of the same Fire OS version gives the same image.
python3 -I - "$1" "$TMP" <<'PY'
import struct, sys
src, dst = sys.argv[1], sys.argv[2]
with open(src, "rb") as f:
    f.seek(1024)
    sb = f.read(1024)
    if struct.unpack_from("<H", sb, 56)[0] != 0xEF53:
        sys.exit("ERROR: %s is not an ext4 image" % src)
    blocks = struct.unpack_from("<I", sb, 4)[0]
    block_size = 1024 << struct.unpack_from("<I", sb, 24)[0]
    mount_count = struct.unpack_from("<H", sb, 52)[0]
    if mount_count:
        print("WARNING: this file system was mounted for writing %d time(s), so it may not be Amazon's image any more" % mount_count, file=sys.stderr)
    size = blocks * block_size
    f.seek(0)
    with open(dst, "wb") as out:
        left = size
        while left:
            chunk = f.read(min(left, 4 << 20))
            if not chunk:
                sys.exit("ERROR: %s is shorter than its file system (%d bytes)" % (src, size))
            out.write(chunk)
            left -= len(chunk)
PY

read -r WANT_SUM WANT_SIZE <"$SUM_FILE"
GOT_SUM="$(sha256sum "$TMP" | cut -d' ' -f1)"
GOT_SIZE="$(stat -c %s "$TMP")"
if [[ "$GOT_SUM" != "$WANT_SUM" || "$GOT_SIZE" != "$WANT_SIZE" ]]; then
  echo "ERROR: this is not the vendor image of Fire OS 7575.1 (PS75751/7809):" >&2
  echo "  got  $GOT_SUM $GOT_SIZE" >&2
  echo "  want $WANT_SUM $WANT_SIZE" >&2
  grep -a -m1 '^ro.vendor.build.fingerprint=' "$TMP" >&2 || true
  exit 1
fi

mv "$TMP" "$OUT"
trap - EXIT
echo "Wrote $OUT ($GOT_SIZE bytes, sha256 $GOT_SUM)"
