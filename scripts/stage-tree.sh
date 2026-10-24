#!/usr/bin/env bash
# Stage one device's Kiosk Satellite Edition layer and patches into the
# shared LineageOS 18.1 tree.
#
# Patch sets use the amazon-oss layout: <set>/<project path>/NNNN-*.patch.
# They apply in this order: amazon-oss's own patches/ project, then
# patches/common, then patches/<device>. The tree is shared by every device,
# so when the patch list changes (another device, a new or edited patch)
# every file any patch set touches goes back to its synced state first.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORKSPACE_DIR="${ECHO_WORKSPACE:-$REPO_ROOT/workspace}"
TREE="${LINEAGE_TREE:-$WORKSPACE_DIR/lineage-18.1}"
DEVICE="${DEVICE:-}"
STATE_DIR="$TREE/.repo/kiosk-satellite-state"
STATE_FILE="$STATE_DIR/patches.sha256"
TOUCHED_FILE="$STATE_DIR/touched-files"

case "$DEVICE" in
  crown|checkers|biscuit) ;;
  *) echo "ERROR: set DEVICE to crown, checkers or biscuit" >&2; exit 1 ;;
esac

[[ -d "$TREE/build" ]] || { echo "ERROR: LineageOS 18.1 not synced at $TREE" >&2; exit 1; }
[[ -d "$TREE/device/amazon/mt8163-common" ]] || { echo "ERROR: amazon-oss sources missing; run scripts/sync.sh" >&2; exit 1; }

PATCH_SETS=("$TREE/patches" "$REPO_ROOT/patches/common" "$REPO_ROOT/patches/$DEVICE")

copy_dir() {
  local src="$1" dst="$2"
  mkdir -p "$dst"
  rsync -a --delete --exclude .git "$src/" "$dst/"
}

# Every *.patch of the given sets, in the order they apply.
list_patches() {
  local set
  for set in "$@"; do
    [[ -d "$set" ]] || continue
    LC_ALL=C find "$set" -type f -name '*.patch' -not -path '*/.git/*' | LC_ALL=C sort
  done
}

# The project directories of one set that hold patches.
list_patch_dirs() {
  [[ -d "$1" ]] || return 0
  LC_ALL=C find "$1" -type f -name '*.patch' -not -path '*/.git/*' -printf '%h\n' | LC_ALL=C sort -u
}

# The tree paths the given patch files touch.
patched_files() {
  local patch_file project
  for patch_file in "$@"; do
    project="${patch_file%/*}"
    project="${project#"$TREE"/patches/}"
    project="${project#"$REPO_ROOT"/patches/*/}"
    # git's own header names binary files too, which have no ---/+++ lines.
    sed -nE 's#^diff --git a/([^ ]+) b/([^ ]+)$#\1\n\2#p; s#^(\+\+\+|---) [ab]/([^\t]+).*#\2#p' "$patch_file" |
      sed "s#^#$project/#"
  done | LC_ALL=C sort -u
}

# Put every file a patch touches back to its synced state: tracked files are
# checked out, files a patch added are removed. That covers the patches of
# every set and device as they are now and the files the last staging
# touched, which a patch may no longer name.
reset_patched_files() {
  local target file dir repo relative
  while IFS= read -r target; do
    file="$TREE/$target"
    dir="$(dirname "$file")"
    while [[ ! -d "$dir" && "$dir" != "$TREE" ]]; do
      dir="$(dirname "$dir")"
    done
    repo="$(git -C "$dir" rev-parse --show-toplevel 2>/dev/null || true)"
    relative="${file#"$repo"/}"
    if [[ -n "$repo" ]] && git -C "$repo" ls-files --error-unmatch -- "$relative" >/dev/null 2>&1; then
      git -C "$repo" checkout -- "$relative"
    else
      rm -f "$file"
    fi
  done < <(
    {
      [[ -f "$TOUCHED_FILE" ]] && cat "$TOUCHED_FILE"
      mapfile -t all < <(list_patches "$TREE/patches" "$REPO_ROOT"/patches/*)
      ((${#all[@]} == 0)) || patched_files "${all[@]}"
    } | LC_ALL=C sort -u
  )
}

[[ -d "$REPO_ROOT/device/$DEVICE" ]] || { echo "ERROR: $DEVICE has no device/$DEVICE yet" >&2; exit 1; }

# Our own source trees, at the same path in the tree.
list_source_trees() {
  [[ -d "$REPO_ROOT/sources" ]] || return 0
  (cd "$REPO_ROOT/sources" && LC_ALL=C find . -mindepth 3 -maxdepth 3 -type d | sed 's#^\./##' | LC_ALL=C sort)
}

copy_sources() {
  local tree
  copy_dir "$REPO_ROOT/common" "$TREE/vendor/kiosk-satellite"
  # Only the device being built, so two devices never define the same module.
  mkdir -p "$TREE/device/kiosk-satellite"
  rsync -a --delete --exclude .git --include "/$DEVICE/***" --exclude '*' \
    "$REPO_ROOT/device/" "$TREE/device/kiosk-satellite/"
  while IFS= read -r tree; do
    copy_dir "$REPO_ROOT/sources/$tree" "$TREE/$tree"
  done < <(list_source_trees)
}

# Patches may change copied files, so what gets copied counts too.
staged_files() {
  local dirs=() dir
  for dir in common "device/$DEVICE" sources; do
    [[ -d "$REPO_ROOT/$dir" ]] && dirs+=("$dir")
  done
  (cd "$REPO_ROOT" && LC_ALL=C find "${dirs[@]}" -type f | LC_ALL=C sort | xargs -r sha256sum)
}

mkdir -p "$STATE_DIR"
manifest="$( { echo "$DEVICE"; list_patches "${PATCH_SETS[@]}" | xargs -r sha256sum; staged_files; } | sha256sum | awk '{print $1}')"
if [[ -f "$STATE_FILE" && "$(cat "$STATE_FILE")" == "$manifest" ]]; then
  echo "SKIP sources and patches unchanged for $DEVICE"
else
  rm -f "$STATE_FILE"
  reset_patched_files
  copy_sources
  for set in "${PATCH_SETS[@]}"; do
    while IFS= read -r dir; do
      project="${dir#"$set"/}"
      [[ -d "$TREE/$project" ]] || { echo "ERROR: patches for missing project $project" >&2; exit 1; }
      "$REPO_ROOT/scripts/apply-patches.sh" "$TREE/$project" 1 "$dir"
    done < <(list_patch_dirs "$set")
  done
  mapfile -t applied < <(list_patches "${PATCH_SETS[@]}")
  ((${#applied[@]} == 0)) || patched_files "${applied[@]}" >"$TOUCHED_FILE"
  printf '%s\n' "$manifest" >"$STATE_FILE"
fi

# Blobs a device takes from its own stock firmware instead of amazon-oss.
if [[ -x "$REPO_ROOT/scripts/extract-$DEVICE-blobs.sh" ]]; then
  LINEAGE_TREE="$TREE" "$REPO_ROOT/scripts/extract-$DEVICE-blobs.sh"
fi

echo "Staged the Kiosk Satellite Edition layer and patches for $DEVICE."
