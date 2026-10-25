#!/usr/bin/env bash
# Generate vendor/amazon/biscuit in the tree: the Echo Dot's own blobs from
# its Fire OS 6.5.7.4 OTA, each checked against the sha256 and size in the
# blobs/biscuit/*.txt manifests. The blobs the Echos share come from
# amazon-oss's vendor/amazon/mt8163-common.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORKSPACE_DIR="${ECHO_WORKSPACE:-$REPO_ROOT/workspace}"
TREE="${LINEAGE_TREE:-$WORKSPACE_DIR/lineage-18.1}"
OTA_URL="https://d1s31zyz7dcc2d.cloudfront.net/2026/8/3/f49aaff7-dd63-4d9c-9e9a-c17498267de5/update-kindle-biscuit_puffin-NS6574_user_7623_0013121734532.bin"
OTA_SHA256="64ab6d2dd85f8093abdd62c275d229c7e9fdd68e4d46892b48bdbd1d100d46d8"
OTA="$WORKSPACE_DIR/downloads/$(basename "$OTA_URL")"
SYSTEM_IMG="$WORKSPACE_DIR/extracted/biscuit-fireos-6.5.7.4/system.img"
SYSTEM_SHA256="eccfa850c3009d5454f69a411c0b757be642059b3224cae8fc941fa0dd22c570"
OUT="$TREE/vendor/amazon/biscuit"

for tool in 7z curl python3 sha256sum stat; do
  command -v "$tool" >/dev/null || { echo "ERROR: required tool not found: $tool" >&2; exit 1; }
done

mkdir -p "$(dirname "$OTA")" "$(dirname "$SYSTEM_IMG")"
if [[ ! -f "$OTA" ]]; then
  curl --fail --location --progress-bar -o "$OTA" "$OTA_URL"
fi
printf '%s  %s\n' "$OTA_SHA256" "$OTA" | sha256sum -c --quiet -

if [[ ! -f "$SYSTEM_IMG" ]] || ! printf '%s  %s\n' "$SYSTEM_SHA256" "$SYSTEM_IMG" | sha256sum -c --quiet - 2>/dev/null; then
  rm -f "$SYSTEM_IMG"
  python3 "$REPO_ROOT/scripts/extract-fireos6-payload.py" "$OTA" system "$SYSTEM_IMG"
fi
printf '%s  %s\n' "$SYSTEM_SHA256" "$SYSTEM_IMG" | sha256sum -c --quiet -

PATCHELF="$TREE/prebuilts/extract-tools/linux-x86/bin/patchelf-0_9"
[[ -x "$PATCHELF" ]] || { echo "ERROR: $PATCHELF is missing, sync the tree first" >&2; exit 1; }

# What Android 11 needs changed in a blob after it passed its checksum.
blob_fixup() {
  local destination="$1" file="$2"
  case "$destination" in
    vendor/lib/hw/audio.primary_amazon.mt8163.so)
      # TinyALSA and libutils symbols that Android 11 lost.
      "$PATCHELF" --add-needed libtinyalsa_shim.so "$file"
      "$PATCHELF" --add-needed libbiscuit_audio_shim.so "$file"
      # The microphone mute GPIO moved to /sys/devices/platform/soc on 4.9,
      # a longer path. init links /dev/mt_gpio to it.
      python3 - "$file" <<'PY'
import sys
path = sys.argv[1]
data = open(path, "rb").read()
old = b"/sys/devices/soc/1000b000.pinctrl/mt_gpio\0"
new = b"/dev/mt_gpio".ljust(len(old), b"\0")
if data.count(old) != 1:
    sys.exit("ERROR: the mt_gpio path is not in the audio HAL exactly once")
open(path, "wb").write(data.replace(old, new))
PY
      ;;
    vendor/lib/libacehal_log.so | vendor/lib/libcommon_time_client.so)
      "$PATCHELF" --add-needed libbiscuit_audio_shim.so "$file"
      ;;
    vendor/lib/libshm.so)
      # libshm asks for Fire OS's lab126.service.shm, which does not exist
      # here. At boot the wait took 4 to 5 seconds, enough to time out
      # audioserver's first calls into the audio HAL. Its callers already get
      # on without it, so it is never looked up:
      # - A constructor looked it up as soon as the library loaded. It now
      #   takes the path SHMLIB_DISABLE_ONLOAD in the environment would, less
      #   the log line: b to where it leaves the lookup for later.
      # - getShmInterface(), which does that later lookup, returns no service
      #   at once: movs r1, #0; str r1, [r0]; bx lr, a null sp<> through the
      #   result pointer.
      python3 - "$file" <<'PY'
import sys
path = sys.argv[1]
data = bytearray(open(path, "rb").read())
for offset, old, new, what in (
    (0xb014, "50b1094a", "05e0094a", "the libshm constructor"),
    (0xb068, "b0b504461748", "002101607047", "getShmInterface"),
):
    old, new = bytes.fromhex(old), bytes.fromhex(new)
    if data[offset:offset + len(old)] != old:
        sys.exit(f"ERROR: {what} is not where libshm.so had it")
    data[offset:offset + len(new)] = new
open(path, "wb").write(data)
PY
      ;;
    vendor/lib/libaudiocomponentengine.so)
      # CallStack moved out of libutils.
      "$PATCHELF" --add-needed libutilscallstack.so "$file"
      ;;
    etc/audio_device.xml)
      # Fire OS sets the four TI microphone ADCs to a 20 dB analog preamp and
      # relies on Amazon's audio processor for the rest of the gain. That
      # processor never runs here, so the preamp goes to 36 dB, as on CM14:
      # speech then reaches apps at a normal level and the loudest speech
      # measured still peaks about 4 dB below full scale.
      sed -i -E 's/(name="ADC_[A-D] MICPGA Volume Ctrl" value=)"40"/\1"72"/' "$file"
      [[ "$(grep -c 'MICPGA Volume Ctrl" value="72"' "$file")" == 4 ]] || {
        echo "ERROR: could not raise the microphone preamp in $destination" >&2; exit 1;
      }
      ;;
  esac
}

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/proprietary"
copy_files=()

while IFS=: read -r source destination expected_sha expected_size; do
  [[ -z "$source" || "$source" == \#* ]] && continue
  [[ "$destination" != /* && "$destination" != *".."* ]] || {
    echo "ERROR: unsafe destination in a manifest: $destination" >&2; exit 1;
  }
  rm -rf "$TMP/unpack"
  7z x -y -o"$TMP/unpack" "$SYSTEM_IMG" "$source" >/dev/null 2>&1 || true
  file="$TMP/unpack/$source"
  [[ -f "$file" ]] || { echo "ERROR: $source is not in the Fire OS image" >&2; exit 1; }
  [[ "$(stat -c %s "$file")" == "$expected_size" ]] || { echo "ERROR: wrong size: $source" >&2; exit 1; }
  [[ "$(sha256sum "$file" | awk '{print $1}')" == "$expected_sha" ]] || {
    echo "ERROR: sha256 mismatch: $source" >&2; exit 1;
  }
  install -D -m 0644 "$file" "$TMP/proprietary/$destination"
  blob_fixup "$destination" "$TMP/proprietary/$destination"
  copy_files+=("    vendor/amazon/biscuit/proprietary/$destination:\$(TARGET_COPY_OUT_SYSTEM)/$destination")
done < <(cat "$REPO_ROOT"/blobs/biscuit/*.txt)

# Amazon's other wake word models, every locale. DAVS hands them only to a
# registered Echo, so they come from the private amazon-davs-models
# repository, checked against blobs/biscuit/davs-models.manifest.
DAVS_DIR="${DAVS_MODELS:-$REPO_ROOT/../amazon-davs-models}"
[[ -d "$DAVS_DIR" ]] || {
  echo "ERROR: no DAVS models at $DAVS_DIR: check out amazon-davs-models there or set DAVS_MODELS" >&2; exit 1;
}
davs_count=0
while IFS=: read -r path expected_sha expected_size; do
  [[ -z "$path" || "$path" == \#* ]] && continue
  [[ "$path" != /* && "$path" != *".."* ]] || { echo "ERROR: unsafe path in the DAVS manifest: $path" >&2; exit 1; }
  file="$DAVS_DIR/$path"
  [[ -f "$file" ]] || { echo "ERROR: missing DAVS model file: $path" >&2; exit 1; }
  [[ "$(stat -c %s "$file")" == "$expected_size" ]] || { echo "ERROR: wrong size: $path" >&2; exit 1; }
  [[ "$(sha256sum "$file" | awk '{print $1}')" == "$expected_sha" ]] || {
    echo "ERROR: sha256 mismatch: $path" >&2; exit 1;
  }
  install -D -m 0644 "$file" "$TMP/proprietary/etc/pryon/$path"
  copy_files+=("    vendor/amazon/biscuit/proprietary/etc/pryon/$path:\$(TARGET_COPY_OUT_SYSTEM)/etc/pryon/$path")
  ((davs_count += 1))
done < "$REPO_ROOT/blobs/biscuit/davs-models.manifest"
[[ "$davs_count" -gt 0 ]] || { echo "ERROR: the DAVS manifest lists no files" >&2; exit 1; }

# Amazon's models hear "stop" only for a few seconds after the wake word: the
# spotter starts asleep, where STOP does not count, and the firmware's Alexa
# model scores a "stop" on its own at 0.20 to 0.41 against its 0.55. Kiosk
# Satellite arms the stop word itself while a timer rings or a reply plays,
# so in every model STOP counts while asleep and is accepted from 0.18, as on
# CM14. Near misses still come from 0.05. The newer models keep these files
# in subfolders. Pryon reports checksum.txt but does not check it.
python3 - "$TMP/proprietary/etc/pryon" <<'PY'
import json, pathlib, sys
root = pathlib.Path(sys.argv[1])
patched = set()
for kw_path in root.rglob("kw.cfg.json"):
    kw = json.loads(kw_path.read_text())
    spotters = kw.get("keyword-orchestra", {}).get("keyword-spotters", [])
    stops = [s for s in spotters if s.get("name") == "STOP"]
    if not stops:
        continue
    for spotter in stops:
        thresholds = spotter["spotter"]["classification-thresholds"]
        thresholds["accept-threshold"] = 0.18
        thresholds["notify-threshold"] = 0.05
    kw_path.write_text(json.dumps(kw, indent=2))
    op_path = kw_path.with_name("op.cfg.json")
    op = json.loads(op_path.read_text()) if op_path.exists() else {}
    if "sleep" in op:
        rules = op["sleep"]["rules"]
        if not any(r["name"] == "STOP" for r in rules):
            rules.append({"name": "STOP", "next": "sleep"})
        op_path.write_text(json.dumps(op, indent=2))
    patched.add(kw_path.relative_to(root).parts[0])
if "alexa-en-US" not in patched:
    sys.exit("ERROR: could not let the stop word fire without Alexa")
print(f"Let the stop word fire without the wake word in {len(patched)} model sets.")
PY

{
  echo "# Generated by scripts/extract-biscuit-blobs.sh. Do not edit."
  echo
  echo "PRODUCT_COPY_FILES += \\"
  for ((i = 0; i < ${#copy_files[@]}; i++)); do
    if ((i + 1 < ${#copy_files[@]})); then
      echo "${copy_files[$i]} \\"
    else
      echo "${copy_files[$i]}"
    fi
  done
} >"$TMP/biscuit-vendor.mk"

mkdir -p "$OUT"
rsync -a --delete --checksum "$TMP/proprietary/" "$OUT/proprietary/"
install -m 0644 "$TMP/biscuit-vendor.mk" "$OUT/biscuit-vendor.mk"
echo "Extracted ${#copy_files[@]} Echo Dot blobs to $OUT."
