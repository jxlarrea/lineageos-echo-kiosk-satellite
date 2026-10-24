# LineageOS for Amazon Echo devices, Kiosk Satellite Edition

One repository that builds Kiosk Satellite Edition images of LineageOS 18.1 for several Echo models from a shared base: R0rt1z2's LineageOS 18.1 port in [amazon-oss](https://github.com/amazon-oss).

| Device | Codename | Status |
| --- | --- | --- |
| Echo Show 8 (1st gen) | `crown` | Being set up |
| Echo Show 5 (1st gen) | `checkers` | Planned |
| Echo Dot (2nd gen) | `biscuit` | Kernel boots. Android bring-up next, see [docs/biscuit-port.md](docs/biscuit-port.md) |

The Echo Dot image for CM14.1 lives in [lineageos-biscuit-kiosk-satellite](https://github.com/jxlarrea/lineageos-biscuit-kiosk-satellite) until the Dot moves here.

## Layout

| Path | What it holds |
| --- | --- |
| `manifest/local.xml` | The amazon-oss projects on top of LineageOS 18.1, plus the Linaro toolchain the kernel needs |
| `manifest/lock.xml` | Every project pinned to a commit. The first sync writes it and later syncs use it |
| `patches/common/` | Patches every device gets |
| `patches/<device>/` | Patches for one device |
| `common/` | The Kiosk Satellite Edition layer every device inherits. It is staged as `vendor/kiosk-satellite` |
| `device/<device>/` | What one device adds on top of its amazon-oss tree. It is staged as `device/kiosk-satellite/<device>` |
| `docker/` | The build container |
| `scripts/` | Sync, stage and build |

Patches follow the amazon-oss layout: `patches/<set>/<project path>/NNNN-*.patch`, for example `patches/crown/device/amazon/crown/0001-crown-Inherit-the-Kiosk-Satellite-Edition-layer.patch`. Staging applies amazon-oss's own `patches/` project first, then `patches/common` and then the device's set. Every device shares one tree, so when the list of patches changes the files any patch touches go back to their synced state before the patches apply again.

## Building

Builds run in Docker. The tree, ccache and build output take well over 100 GB, so `workspace` can be a symlink to a larger disk, or `ECHO_WORKSPACE` can point somewhere else.

```sh
make docker
make sync
make crown
docker logs -f lineage-18.1-crown-build
```

`make crown` stages the tree and starts a detached `lineage-18.1-crown-build` container that runs `m bacon`. The zip lands in `workspace/lineage-18.1/out/target/product/crown/`. Check the log for `#### build completed successfully` instead of trusting an exit code.

`make stage DEVICE=crown` only stages the tree, which is useful before building a single module by hand.

## WebView

The Echo Shows ship LineageOS's prebuilt Chromium WebView (`external/chromium-webview/prebuilt/arm`), which LineageOS keeps current on its `main` branch for every release. Before each release, set that project's revision in `manifest/lock.xml` to the newest commit on `main` and sync, so the Shows ship the newest WebView. The Echo Dot has no screen and ships no WebView.

## Credits

The shared base is R0rt1z2's work in [amazon-oss](https://github.com/amazon-oss). meiser79 brought the Echo Dot up on LineageOS 18.1 first, and [docs/credits.md](docs/credits.md) lists the components his port guided.
