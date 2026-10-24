#!/usr/bin/env bash
# Build an Echo Dot kernel boot test, or a TWRP zip that writes any boot
# image into slot A.
#
#   build.sh test <Image.gz-dtb> <kernel out dir> <output dir>
#   build.sh zip <boot.img> <output zip>
#
# The test boot image runs init.c: it saves what the kernel found to
# /cache/k49-test and reboots into recovery. The zip refuses to write
# unless TWRP's current-boot is boot_a, saves the old boot image to
# /tmp/boot-backup.img and checks what it wrote.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
TREE="$(realpath "${LINEAGE_TREE:-$REPO_ROOT/workspace/lineage-18.1}")"
IMAGE="lineage-18.1-ubuntu20:latest"
GCC="$TREE/prebuilts/linaro/linux-x86/aarch64/aarch64-linux-gnu/bin/aarch64-linux-gnu-"

make_zip() {
  local boot="$1" out="$2" work
  work="$(mktemp -d)"
  mkdir -p "$work/META-INF/com/google/android"
  install -m 0755 "$HERE/update-binary" "$work/META-INF/com/google/android/update-binary"
  echo '# update-binary does the work' >"$work/META-INF/com/google/android/updater-script"
  cp "$boot" "$work/boot.img"
  (cd "$work" && sha256sum boot.img >boot.img.sha256 && rm -f "$out" &&
    zip -q -r "$(realpath -m "$out")" META-INF boot.img boot.img.sha256)
  rm -rf "$work"
}

case "${1:-}" in
  test)
    kernel="$(realpath "$2")" kout="$(realpath "$3")" out="$(realpath -m "$4")"
    mkdir -p "$out"
    docker run --rm -v "$TREE:$TREE" -v "$HERE:$HERE" -v "$out:$out" "$IMAGE" \
      bash -c "${GCC}gcc -static -Os -Wall -o '$out/init' '$HERE/init.c' && ${GCC}strip '$out/init'"
    printf '%s\n' 'dir /dev 0755 0 0' 'nod /dev/console 0600 0 0 c 5 1' 'dir /proc 0755 0 0' \
      'dir /sys 0755 0 0' 'dir /cache 0755 0 0' "file /init $out/init 0755 0 0" >"$out/ramdisk.list"
    "$kout/usr/gen_init_cpio" "$out/ramdisk.list" | gzip -9 >"$out/ramdisk.cpio.gz"
    # The Dot's boot image layout: mt8163-common's arm64 kernel, tags and
    # second addresses, the ramdisk at the stock 0x44000000, and the bootopt
    # that has LK start a 64-bit kernel.
    python3 -I "$TREE/system/tools/mkbootimg/mkbootimg.py" --kernel "$kernel" \
      --ramdisk "$out/ramdisk.cpio.gz" --cmdline 'bootopt=64S3,32N2,64N2' \
      --base 0x40000000 --kernel_offset 0x00080000 --ramdisk_offset 0x04000000 \
      --second_offset 0x00f00000 --tags_offset 0x08000000 --pagesize 2048 \
      --header_version 0 -o "$out/boot-test.img"
    make_zip "$out/boot-test.img" "$out/flash-boot-test.zip"
    echo "Built $out/flash-boot-test.zip"
    ;;
  zip)
    make_zip "$2" "$3"
    echo "Built $3"
    ;;
  *)
    sed -n '2,12p' "$0" >&2
    exit 2
    ;;
esac
