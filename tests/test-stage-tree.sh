#!/usr/bin/env bash
# Stage a fake tree: patches apply in order, an unchanged list is skipped and
# a changed list puts the patched files back before applying again.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

# A copy of the repo's scripts with its own patches/, common/ and device/.
REPO="$WORK/repo"
mkdir -p "$REPO/scripts" "$REPO/common/config" "$REPO/device/crown" "$REPO/patches/common"
cp "$REPO_ROOT/scripts/stage-tree.sh" "$REPO_ROOT/scripts/apply-patches.sh" "$REPO/scripts/"
echo '# common' >"$REPO/common/config/common.mk"
echo '# crown' >"$REPO/device/crown/device.mk"

TREE="$WORK/tree"
mkdir -p "$TREE/build" "$TREE/device/amazon/mt8163-common" "$TREE/patches/frameworks/av" "$TREE/device/amazon/crown"
git_project() {
  git -C "$1" init -q
  git -C "$1" add -A
  git -C "$1" -c user.name=t -c user.email=t@t commit -qm init
}
printf 'one\ntwo\nthree\n' >"$TREE/device/amazon/crown/lineage_crown.mk"
git_project "$TREE/device/amazon/crown"
mkdir -p "$TREE/frameworks/av"
printf 'av\n' >"$TREE/frameworks/av/av.txt"
git_project "$TREE/frameworks/av"

cat >"$TREE/patches/frameworks/av/0001-upstream.patch" <<'EOF'
upstream

--- a/av.txt
+++ b/av.txt
@@ -1 +1 @@
-av
+av patched
EOF

mkdir -p "$REPO/patches/crown/device/amazon/crown"
cat >"$REPO/patches/crown/device/amazon/crown/0001-two.patch" <<'EOF'
two

--- a/lineage_crown.mk
+++ b/lineage_crown.mk
@@ -1,3 +1,3 @@
 one
-two
+two crown
 three
--- /dev/null
+++ b/added.mk
@@ -0,0 +1 @@
+added
EOF

stage() { DEVICE=crown LINEAGE_TREE="$TREE" "$REPO/scripts/stage-tree.sh"; }

stage >/dev/null
grep -qx 'av patched' "$TREE/frameworks/av/av.txt" || fail "upstream patch not applied"
grep -qx 'two crown' "$TREE/device/amazon/crown/lineage_crown.mk" || fail "device patch not applied"
[[ -f "$TREE/device/amazon/crown/added.mk" ]] || fail "added file missing"
[[ -f "$TREE/vendor/kiosk-satellite/config/common.mk" ]] || fail "common layer not staged"
[[ -f "$TREE/device/kiosk-satellite/crown/device.mk" ]] || fail "device layer not staged"

stage | grep -q 'SKIP sources and patches unchanged' || fail "unchanged patches were applied again"

# Dropping the added file from the patch must remove it from the tree.
cat >"$REPO/patches/crown/device/amazon/crown/0001-two.patch" <<'EOF'
two

--- a/lineage_crown.mk
+++ b/lineage_crown.mk
@@ -1,3 +1,3 @@
 one
-two
+two again
 three
EOF
stage >/dev/null
grep -qx 'two again' "$TREE/device/amazon/crown/lineage_crown.mk" || fail "edited patch not applied"
grep -qx 'av patched' "$TREE/frameworks/av/av.txt" || fail "upstream patch lost on restage"
[[ ! -f "$TREE/device/amazon/crown/added.mk" ]] || fail "file from the old patch left behind"

# A patch on one of our own source trees survives a change to that source.
mkdir -p "$REPO/sources/device/amazon/dot" "$REPO/patches/crown/device/amazon/dot"
printf 'a\nb\nc\n' >"$REPO/sources/device/amazon/dot/product.mk"
cat >"$REPO/patches/crown/device/amazon/dot/0001-b.patch" <<'EOF'
b

--- a/product.mk
+++ b/product.mk
@@ -1,3 +1,3 @@
 a
-b
+b patched
 c
EOF
stage >/dev/null
grep -qx 'b patched' "$TREE/device/amazon/dot/product.mk" || fail "patch on a source tree not applied"
printf 'a\nb\nc\nd\n' >"$REPO/sources/device/amazon/dot/product.mk"
stage >/dev/null
grep -qx 'b patched' "$TREE/device/amazon/dot/product.mk" || fail "patch lost after a source change"
grep -qx 'd' "$TREE/device/amazon/dot/product.mk" || fail "source change not staged"

echo "PASS test-stage-tree"
