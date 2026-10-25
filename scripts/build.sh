#!/usr/bin/env bash
# Stage one device and launch its LineageOS 18.1 build in a detached container.
set -euo pipefail

IMAGE="lineage-18.1-ubuntu20:latest"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Resolved, because workspace/ may be a symlink to a larger disk and the
# container only sees the mounted path.
WORKSPACE_DIR="$(realpath "${ECHO_WORKSPACE:-$REPO_ROOT/workspace}")"
TREE="$(realpath "${LINEAGE_TREE:-$WORKSPACE_DIR/lineage-18.1}")"
DEVICE="${DEVICE:-}"
CONTAINER="lineage-18.1-$DEVICE-build"
BUILD_TARGET="${BUILD_TARGET:-bacon}"
# CLEAN=1 empties the device's output folder first. The build never deletes
# what a device stopped installing, such as an app taken out of the image,
# and the zip packs the whole folder, so release builds use it.
CLEAN="${CLEAN:-0}"

# A release build is the commit its notes name, so nothing uncommitted may
# go in: the tree is staged from the working copy as it is.
if [[ "$CLEAN" == 1 && -n "$(git -C "$REPO_ROOT" status --porcelain)" ]]; then
  echo "ERROR: a clean (release) build needs everything committed first:" >&2
  git -C "$REPO_ROOT" status --short >&2
  exit 1
fi
BUILD_JOBS="${BUILD_JOBS:-$(nproc)}"
CCACHE_DIR="${CCACHE_DIR:-$WORKSPACE_DIR/ccache}"
CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-50G}"
LUNCH_TARGET="lineage_$DEVICE-userdebug"

# The name each device's releases go by, in CHANGELOG.md headings and on
# GitHub.
device_name() {
  case "$1" in
    biscuit) echo "Echo Dot (2nd gen)" ;;
    crown) echo "Echo Show 8 (1st gen)" ;;
    cronos) echo "Echo Show 5 (2nd gen)" ;;
  esac
}

# The release this build is, for ro.biscuit.release and ro.echo.release: the
# newest CHANGELOG.md heading for this device, such as "## Echo Dot (2nd gen)
# 2026.10.6". While an Unreleased heading is above it, either a bare one for
# every device or one for this device such as "## Echo Show 5 (2nd gen)
# Unreleased", it is that release with -dev after it, or 0.0.0-dev before
# the device's first release. System updates compare it with the device's
# GitHub releases.
release_version() {
  awk -v name="$(device_name "$DEVICE")" '/^## / {
    heading = substr($0, 4)
    if (heading == "Unreleased" || heading == name " Unreleased") { dev = 1; next }
    if (name != "" && index(heading, name " ") == 1) {
      split(substr(heading, length(name) + 2), words, " ")
      print words[1] (dev ? "-dev" : ""); found = 1; exit
    }
  }
  END { if (!found) print "0.0.0" (dev ? "-dev" : "") }' "$REPO_ROOT/CHANGELOG.md"
}
BISCUIT_RELEASE="${BISCUIT_RELEASE:-$(release_version)}"

DEVICE="$DEVICE" "$REPO_ROOT/scripts/stage-tree.sh"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "ERROR: Docker image '$IMAGE' not found." >&2
  echo "Build it first:" >&2
  echo "  docker build -t $IMAGE -f $REPO_ROOT/docker/lineage-18.1.Dockerfile $REPO_ROOT/docker/" >&2
  exit 1
fi

mkdir -p "$CCACHE_DIR"
docker rm -f "$CONTAINER" >/dev/null 2>&1 || true

# The tree is mounted at the same path as on the host, so paths in the build
# output and in error messages match the host's.
docker run -d \
  --name "$CONTAINER" \
  -v "$REPO_ROOT:$REPO_ROOT" \
  -v "$WORKSPACE_DIR:$WORKSPACE_DIR" \
  -w "$TREE" \
  "$IMAGE" \
  bash -lc "
    set -e
    source build/envsetup.sh >/dev/null
    export BISCUIT_RELEASE='$BISCUIT_RELEASE'
    export USE_CCACHE=1
    export CCACHE_EXEC=/usr/bin/ccache
    export CCACHE_DIR='$CCACHE_DIR'
    ccache -M '$CCACHE_MAXSIZE' >/dev/null
    lunch '$LUNCH_TARGET' >/tmp/lunch.log
    # build.prop does not depend on BISCUIT_RELEASE, so a build that only
    # changes the release would keep the old ro.biscuit.release.
    rm -f \"\$OUT/system/build.prop\" \
      \"\$OUT/obj/ETC/system_build_prop_intermediates/build.prop\"
    if [ '$CLEAN' = 1 ]; then m installclean; fi
    m -j'$BUILD_JOBS' '$BUILD_TARGET'
  "

echo "Started $CONTAINER ($BUILD_TARGET, $LUNCH_TARGET, release $BISCUIT_RELEASE$([[ $CLEAN = 1 ]] && echo ", clean"))."
echo "Monitor: docker logs -f $CONTAINER"
