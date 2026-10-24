#!/usr/bin/env bash
# Apply every *.patch in one directory, sorted, fuzz-free and idempotently.
# The target root must be a git checkout: git apply also takes the binary
# patches that add firmware files.
set -euo pipefail

usage() {
  echo "usage: $0 <target-root> <strip-level> <patch-dir>" >&2
}

[[ $# == 3 ]] || { usage; exit 2; }
ROOT="$1"
STRIP="$2"
PATCH_DIR="$3"
PATCH_REAPPLY="${PATCH_REAPPLY:-0}"
PATCH_REVERSE_ONLY="${PATCH_REVERSE_ONLY:-0}"
PATCH_STATE_DIR="${PATCH_STATE_DIR:-}"

[[ -d "$ROOT" ]] || { echo "ERROR: target root missing: $ROOT" >&2; exit 1; }
[[ -d "$PATCH_DIR" ]] || { echo "ERROR: patch directory missing: $PATCH_DIR" >&2; exit 1; }
[[ "$STRIP" =~ ^[0-9]+$ ]] || { echo "ERROR: strip level must be numeric: $STRIP" >&2; exit 1; }

mapfile -d '' PATCHES < <(LC_ALL=C find "$PATCH_DIR" -maxdepth 1 -type f -name '*.patch' -print0 | LC_ALL=C sort -z)

patch_manifest() {
  for patch_file in "${PATCHES[@]}"; do
    sha256sum "$patch_file"
  done | sha256sum | awk '{print $1}'
}

state_file=""
if [[ -n "$PATCH_STATE_DIR" ]]; then
  mkdir -p "$PATCH_STATE_DIR"
  state_file="$PATCH_STATE_DIR/$(basename "$PATCH_DIR")-p$STRIP.sha256"
  manifest="$(patch_manifest)"
  if [[ "$PATCH_REAPPLY" == 1 && -f "$state_file" && "$(cat "$state_file")" == "$manifest" ]]; then
    echo "SKIP patch directory unchanged ${PATCH_DIR#$PWD/}"
    exit 0
  fi
fi

apply() {
  git -C "$ROOT" apply --whitespace=nowarn -p"$STRIP" "$@"
}

try_reverse_patch() {
  apply --check -R "$1" >/dev/null 2>&1
}

reverse_applied_patches() {
  local i patch_file count
  for ((i=${#PATCHES[@]}-1; i>=0; i--)); do
    patch_file="${PATCHES[$i]}"
    count=0
    while try_reverse_patch "$patch_file"; do
      apply -R "$patch_file"
      echo "UNAPPLIED ${patch_file#$PWD/}"
      count=$((count + 1))
      if ((count > 20)); then
        echo "ERROR: too many reverse applications for ${patch_file#$PWD/}" >&2
        exit 1
      fi
    done
  done
}

if [[ "$PATCH_REAPPLY" == 1 || "$PATCH_REVERSE_ONLY" == 1 ]]; then
  reverse_applied_patches
fi

if [[ "$PATCH_REVERSE_ONLY" == 1 ]]; then
  exit 0
fi

for patch_file in "${PATCHES[@]}"; do
  rel="${patch_file#$PWD/}"
  if apply --check "$patch_file" 2>/dev/null; then
    apply "$patch_file"
    echo "APPLIED $rel"
  elif [[ "$PATCH_REAPPLY" != 1 ]] && try_reverse_patch "$patch_file"; then
    echo "SKIP already applied $rel"
  else
    echo "ERROR: patch does not apply cleanly: $rel" >&2
    apply --check "$patch_file"
    exit 1
  fi
done

if [[ -n "$state_file" ]]; then
  printf '%s\n' "$manifest" >"$state_file"
fi
