#!/usr/bin/env bash
# Sync LineageOS 18.1 with the amazon-oss projects and write the lock
# manifest on the first successful sync.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# The checkout, ccache and build output take well over 100 GB, so they can
# live outside the repository (ECHO_WORKSPACE).
WORKSPACE_DIR="${ECHO_WORKSPACE:-$REPO_ROOT/workspace}"
TREE="${LINEAGE_TREE:-$WORKSPACE_DIR/lineage-18.1}"
LOCAL_MANIFEST="$REPO_ROOT/manifest/local.xml"
LOCK="$REPO_ROOT/manifest/lock.xml"
REPO_BIN="${REPO_BIN:-}"

[[ -f "$LOCAL_MANIFEST" ]] || { echo "ERROR: missing $LOCAL_MANIFEST" >&2; exit 1; }
command -v git-lfs >/dev/null 2>&1 || {
  echo "ERROR: the WebView prebuilt needs git-lfs: sudo apt-get install git-lfs" >&2
  exit 1
}
mkdir -p "$TREE" "$WORKSPACE_DIR/bin"

if [[ -z "$REPO_BIN" ]]; then
  if command -v repo >/dev/null 2>&1; then
    REPO_BIN=repo
  else
    REPO_BIN="$WORKSPACE_DIR/bin/repo"
    if [[ ! -x "$REPO_BIN" ]]; then
      curl -L --fail -o "$REPO_BIN" https://storage.googleapis.com/git-repo-downloads/repo
      chmod +x "$REPO_BIN"
    fi
  fi
fi

cd "$TREE"
"$REPO_BIN" init -u https://github.com/LineageOS/android.git -b lineage-18.1 \
  --depth "${LINEAGE_DEPTH:-1}"

# The lock already holds the local manifest's projects, so the two must not
# be loaded together.
if [[ -f "$LOCK" ]]; then
  rm -f .repo/local_manifests/kiosk-satellite.xml
  cp "$LOCK" .repo/manifests/lock.xml
  "$REPO_BIN" init -m lock.xml
else
  mkdir -p .repo/local_manifests
  cp "$LOCAL_MANIFEST" .repo/local_manifests/kiosk-satellite.xml
fi

# More parallel fetches than about 8 mostly earn dropped connections, and
# every interrupted fetch leaves an unpacked pack behind.
for attempt in 1 2 3; do
  if "$REPO_BIN" sync -c --no-tags --no-clone-bundle --fail-fast -j"${SYNC_JOBS:-8}"; then
    if [[ ! -f "$LOCK" ]]; then
      "$REPO_BIN" manifest -r -o "$LOCK"
      echo "Wrote resolved lock manifest: $LOCK"
    fi
    # repo checks the WebView APK out as a Git LFS pointer. The Echos run a
    # 32-bit userspace, so only the arm one is fetched.
    git -C external/chromium-webview/prebuilt/arm lfs pull github
    unzip -tqq external/chromium-webview/prebuilt/arm/webview.apk
    exit 0
  fi
  echo "repo sync failed (attempt $attempt/3)" >&2
  sleep $((attempt * 10))
done
exit 1
