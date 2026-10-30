# Building

One LineageOS 18.1 tree builds every device. It is R0rt1z2's LineageOS 18.1 port in [amazon-oss](https://github.com/amazon-oss), with this repository's Kiosk Satellite Edition layer and patches staged on top before each build.

## Layout

| Path | What it holds |
| --- | --- |
| `manifest/local.xml` | The amazon-oss projects on top of LineageOS 18.1, plus the Linaro toolchain the kernel needs |
| `manifest/lock.xml` | Every project pinned to a commit. The first sync writes it and later syncs use it |
| `patches/common/` | Patches every device gets |
| `patches/<device>/` | Patches for one device, its kernel included |
| `common/` | The Kiosk Satellite Edition layer every device inherits. It is staged as `vendor/kiosk-satellite` |
| `device/<device>/` | What one device adds on top of its amazon-oss tree. It is staged as `device/kiosk-satellite/<device>` |
| `sources/device/amazon/<device>/` | A device tree of this repository's own, where amazon-oss has none. It is staged as `device/amazon/<device>` |
| `blobs/<device>/` | The proprietary files a device takes from its Fire OS image, each with its checksum |
| `docker/` | The build container |
| `scripts/` | Sync, stage, build and release signing |
| `tests/` | Checks that run on the host |

Patches follow the amazon-oss layout: `patches/<set>/<project path>/NNNN-*.patch`, for example `patches/crown/device/amazon/crown/0001-crown-Inherit-the-Kiosk-Satellite-Edition-layer.patch`. Staging applies amazon-oss's own `patches/` project first, then `patches/common` and then the device's set. Every device shares one tree, so when the list of patches changes the files any patch touches go back to their synced state before the patches apply again.

## Building an image

Builds run in Docker. The tree, ccache and build output take well over 100 GB, so `workspace` can be a symlink to a larger disk. `ECHO_WORKSPACE` can also point somewhere else.

```sh
make docker
make sync
make biscuit
docker logs -f lineage-18.1-biscuit-build
```

`make biscuit` stages the tree and starts a detached `lineage-18.1-biscuit-build` container that runs `m bacon`. The zip lands in `workspace/lineage-18.1/out/target/product/biscuit/`. Check the log for `#### build completed successfully` instead of trusting an exit code. `CLEAN=1 make biscuit` empties the device's output folder first, which release builds need (see [Releases](#releases)). `make crown` does the same for the Echo Show 8 once its image is ready.

`make stage DEVICE=biscuit` only stages the tree, which is useful before building a single module by hand. `BUILD_TARGET=<module> DEVICE=biscuit scripts/build.sh` builds one module in the container.

`make test` runs the host checks. `tests/test-biscuit-system-update.sh` checks the Echo Dot's system update script against a fake TWRP.

## The Echo Dot's proprietary files

`scripts/extract-biscuit-blobs.sh` downloads the Fire OS 6.5.7.4 OTA, unpacks its system image and copies every file `blobs/biscuit/*.txt` lists, after checking its size and checksum. It also applies the fixups the Fire OS audio libraries need on Android 11. Staging runs it.

Amazon's wake word engine Pryon and the en-US Alexa model come from that OTA like the other files. The other wake words and locales come from Amazon's DAVS. DAVS hands them out only to an Echo registered to an Amazon account, so the build cannot download them. They live in the private [amazon-davs-models](https://github.com/jxlarrea/amazon-davs-models) repository, one folder per set. Check it out next to this repository or set `DAVS_MODELS` to where it is. The build checks every file against `blobs/biscuit/davs-models.manifest` before it installs them under `/system/etc/pryon`. `common/pryon/locales.txt` names the sets each locale listens with.

The extract script writes Pryon and its models to `pryon-vendor.mk` and the Dot's other files to `biscuit-vendor.mk`. Every Echo is an MT8163 with the same 32-bit userspace, so every image installs Pryon with `biscuit-pryond` (`common/pryon`, `common/config/common.mk`). Only the Dot's image gets the rest.

## WebView

The Echo Shows ship LineageOS's prebuilt Chromium WebView (`external/chromium-webview/prebuilt/arm`), which LineageOS keeps current on its `main` branch for every release. Before each release, set that project's revision in `manifest/lock.xml` to the newest commit on `main` and sync, so the Shows ship the newest WebView. The Echo Dot has no screen and ships no WebView.

## Releases

Each device has releases of its own, with a version of its own, tagged with its codename and the release:

| Device | Tag | Title |
| --- | --- | --- |
| Echo Dot (2nd gen) | `biscuit-2026.10.11` | `Echo Dot (2nd gen) 2026.10.11` |
| Echo Show 5 (2nd gen) | `cronos-2026.10.1` | `Echo Show 5 (2nd gen) 2026.10.1` |

A device's updater only looks at its own tags.

Each device has its own changelog: [CHANGELOG-biscuit.md](../CHANGELOG-biscuit.md), [CHANGELOG-cronos.md](../CHANGELOG-cronos.md) and [CHANGELOG-crown.md](../CHANGELOG-crown.md). The release a build is comes from the newest release heading in the device's changelog, such as `## 2026.10.11 - 2026-10-25`. While a `## Unreleased` heading is above it, the build is that release with `-dev` after it. Before the device's first release it is `0.0.0-dev`. The image reports it as `ro.biscuit.release` on the Echo Dot and `ro.echo.release` on the Echo Shows. System updates compare it with the device's releases on GitHub. `device_name` in `scripts/build.sh` holds the name each device's releases go by on GitHub.

To make a release, here of the Echo Dot. For an Echo Show, use its codename and name instead, such as `cronos` and `Echo Show 5 (2nd gen)`:

1. Rename the `Unreleased` heading in the device's changelog, here `CHANGELOG-biscuit.md`, to the release, for example `## 2026.10.12 - 2026-11-02`. Then commit.
2. Build it from a clean output folder with `CLEAN=1 make biscuit`. Without it, files from earlier builds that the image no longer installs, such as an app taken out, stay in the output folder and end up in the zip. Commit everything first: the build stages the working tree as it is, so a clean build refuses to start with uncommitted changes.
3. Check the image with `make check DEVICE=biscuit` and run `make test`. The check fails if the image misses one of its own device's parts or carries another device's, such as EchoService on the Dot or BiscuitService on a Show. It also fails if the image reports a release other than the one in the device's changelog. Do not release an image that fails either one.
4. Copy the zip to a name with the release and the codename in it, such as `lineageos-echo-kiosk-satellite-2026.10.12-biscuit.zip`, then sign it. The device checks the signature before it installs an update:

   ```sh
   scripts/sign-release.sh lineageos-echo-kiosk-satellite-2026.10.12-biscuit.zip
   sha256sum lineageos-echo-kiosk-satellite-2026.10.12-biscuit.zip > lineageos-echo-kiosk-satellite-2026.10.12-biscuit.zip.sha256
   ```

   The private key stays outside the repository, in `~/.config/biscuit-ota/release-key.pem` unless `BISCUIT_OTA_KEY` says otherwise. The Echo Dot image ships the public half from `device/biscuit/ota/release-key.pem` and the Echo Show images from `common/ota/release-key.pem`.

5. Test the signed zip as an update on a device first. Serve it with a releases list in GitHub's format from your computer and point the device at the list with `setprop persist.biscuit.ota.releases <url>` on the Echo Dot or `setprop persist.echo.ota.releases <url>` on an Echo Show. Set it back to `""` afterward.
6. Publish a GitHub release tagged `biscuit-2026.10.12` and titled `Echo Dot (2nd gen) 2026.10.12`. Attach the zip, its `.sig` and its `.sha256`.

A release without a signed zip can only be installed by hand.
