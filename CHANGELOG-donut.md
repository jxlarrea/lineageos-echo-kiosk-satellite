# Echo Dot (3rd gen) changelog

All notable changes to the Echo Dot (3rd gen) (`donut`) image are documented here. The [Echo Dot (2nd gen)](CHANGELOG-biscuit.md), [Echo Show 5 (1st gen)](CHANGELOG-checkers.md), [Echo Show 5 (2nd gen)](CHANGELOG-cronos.md), [Echo Show 8 (1st gen)](CHANGELOG-crown.md) and [Echo Show 8 (2nd gen)](CHANGELOG-athena.md) have their own.

## Unreleased

### Added
- **A first LineageOS 18.1 port for the Echo Dot (3rd gen).** The repository has its own device tree in `sources/device/amazon/donut`, since amazon-oss has none for the Dot's MT8167 chip. The image boots to adb over USB on the Dot, with no screen and software rendering as on the Echo Dot (2nd gen). It has no sound, Wi-Fi, Bluetooth or light ring yet, and no Kiosk Satellite Edition layer.
- **System as the root.** The Dot's bootloader does not run a boot image's ramdisk on a normal boot. It has the kernel mount the booted slot's system partition as the root, so the image is built that way and its boot image carries an empty ramdisk.
- **The kernel, in [android_kernel_amazon_mt8167](https://github.com/jxlarrea/android_kernel_amazon_mt8167), which the manifest pulls in.** It is Amazon's Fire OS 6.5.7.1 source release (Linux 4.4.22) with what Android 11 needs: the binder driver of Amazon's newer 4.4 kernel, the upstream files the release leaves out and `lineage_donut_defconfig`. The root mapping works without a verity key on an unlocked Dot. Android 11's adb no longer panics the kernel on its first transfer. A reboot no longer hangs in the light ring driver while its boot animation plays.
