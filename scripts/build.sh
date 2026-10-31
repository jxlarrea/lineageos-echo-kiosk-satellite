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
# The build gets part of the host, so the rest of it keeps running. Without
# limits it took every core and the whole host stalled until it finished.
# BUILD_CPUS defaults to half the cores. BUILD_MEMORY defaults to half the
# memory, but never more than what is free when the build starts minus 4 GB.
# The build gets no swap. A build that needs more fails with killed processes
# instead of freezing the host: lower BUILD_JOBS or free memory and run it
# again.
HOST_MEMORY_GB=$(( $(awk '/^MemTotal:/ {print $2}' /proc/meminfo) / 1048576 ))
FREE_MEMORY_GB=$(( $(awk '/^MemAvailable:/ {print $2}' /proc/meminfo) / 1048576 ))
DEFAULT_MEMORY_GB=$(( HOST_MEMORY_GB / 2 ))
(( FREE_MEMORY_GB - 4 < DEFAULT_MEMORY_GB )) && DEFAULT_MEMORY_GB=$(( FREE_MEMORY_GB - 4 ))
(( DEFAULT_MEMORY_GB < 4 )) && DEFAULT_MEMORY_GB=4
BUILD_CPUS="${BUILD_CPUS:-$(( $(nproc) > 1 ? $(nproc) / 2 : 1 ))}"
BUILD_MEMORY="${BUILD_MEMORY:-${DEFAULT_MEMORY_GB}g}"
if [[ "$BUILD_MEMORY" =~ ^([0-9]+)g$ ]] && (( BASH_REMATCH[1] < 16 )); then
  echo "WARNING: the build gets $BUILD_MEMORY of memory (${FREE_MEMORY_GB} GB free). A full build may run out." >&2
  echo "         Free some memory or set BUILD_MEMORY and lower BUILD_JOBS." >&2
fi
# About 5 GB a job: frameworks/base's metalava steps each take several GB, and
# 10 jobs in 21 GB ran out of memory there.
MEMORY_JOBS=1
[[ "$BUILD_MEMORY" =~ ^([0-9]+)g$ ]] && MEMORY_JOBS=$(( BASH_REMATCH[1] / 5 > 1 ? BASH_REMATCH[1] / 5 : 1 ))
BUILD_JOBS="${BUILD_JOBS:-$(( MEMORY_JOBS < BUILD_CPUS ? MEMORY_JOBS : BUILD_CPUS ))}"
# Soong runs metalava in its highmem pool and sizes that pool at 8 GB a job
# of the host's memory, not the container's, so every job could be metalava
# at once. Size it from BUILD_MEMORY instead, less 4 GB for Soong itself.
HIGHMEM_JOBS=1
[[ "$BUILD_MEMORY" =~ ^([0-9]+)g$ ]] && HIGHMEM_JOBS=$(( (BASH_REMATCH[1] - 4) / 8 > 1 ? (BASH_REMATCH[1] - 4) / 8 : 1 ))
BUILD_HIGHMEM_JOBS="${BUILD_HIGHMEM_JOBS:-$HIGHMEM_JOBS}"
CCACHE_DIR="${CCACHE_DIR:-$WORKSPACE_DIR/ccache}"
CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-50G}"
LUNCH_TARGET="lineage_$DEVICE-userdebug"

# The name each device's releases go by on GitHub.
device_name() {
  case "$1" in
    biscuit) echo "Echo Dot (2nd gen)" ;;
    crown) echo "Echo Show 8 (1st gen)" ;;
    cronos) echo "Echo Show 5 (2nd gen)" ;;
  esac
}

# The release this build is, for ro.biscuit.release and ro.echo.release: the
# newest release heading in the device's CHANGELOG-<device>.md, such as
# "## 2026.10.6 - 2026-10-25". While an "## Unreleased" heading is above it,
# it is that release with -dev after it, or 0.0.0-dev before the device's
# first release. System updates compare it with the device's GitHub releases.
release_version() {
  awk '/^## / {
    heading = substr($0, 4)
    if (heading == "Unreleased") { dev = 1; next }
    split(heading, words, " ")
    print words[1] (dev ? "-dev" : ""); found = 1; exit
  }
  END { if (!found) print "0.0.0" (dev ? "-dev" : "") }' "$REPO_ROOT/CHANGELOG-$DEVICE.md"
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
  --cpus "$BUILD_CPUS" \
  --cpu-shares 256 \
  --memory "$BUILD_MEMORY" \
  --memory-swap "$BUILD_MEMORY" \
  --oom-score-adj 500 \
  -v "$REPO_ROOT:$REPO_ROOT" \
  -v "$WORKSPACE_DIR:$WORKSPACE_DIR" \
  -w "$TREE" \
  "$IMAGE" \
  bash -lc "
    set -e
    # Lowest CPU priority for the shell and everything the build starts, so
    # the host's own work, such as an SSH session, runs first.
    renice -n 19 -p \$\$ >/dev/null
    source build/envsetup.sh >/dev/null
    export BISCUIT_RELEASE='$BISCUIT_RELEASE'
    # The build number in Android's build string and fingerprint, such as
    # lineage_crown-userdebug 11 RQ3A.211001.001 2026.10.6 test-keys, instead
    # of eng.<user>.<date>.
    export BUILD_NUMBER='$BISCUIT_RELEASE'
    export NINJA_HIGHMEM_NUM_JOBS='$BUILD_HIGHMEM_JOBS'
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
echo "Limits: $BUILD_CPUS CPUs, $BUILD_MEMORY memory without swap, $BUILD_JOBS jobs, $BUILD_HIGHMEM_JOBS for metalava."
echo "Monitor: docker logs -f $CONTAINER"
