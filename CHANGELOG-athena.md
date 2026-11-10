# Echo Show 8 (2nd gen) changelog

All notable changes to the Echo Show 8 (2nd gen) (`athena`) image are documented here. The [Echo Dot (2nd gen)](CHANGELOG-biscuit.md), [Echo Show 5 (1st gen)](CHANGELOG-checkers.md), [Echo Show 5 (2nd gen)](CHANGELOG-cronos.md) and [Echo Show 8 (1st gen)](CHANGELOG-crown.md) have their own.

## Unreleased

### Added
- **A first LineageOS 18.1 port for the Echo Show 8 (2nd gen).** The repository has its own device tree in `sources/device/amazon/athena`. The image is a system for the vendor partition Fire OS 7 left on the Show, with a 32-bit userspace on an arm64 kernel built from Amazon's Fire OS 7.5.7.1 source release. It boots to the home screen with the display, touch, sound, Wi-Fi and adb. SELinux is permissive, as on the other Echos. The kernel source is not in the repository yet, there is no update zip and the Kiosk Satellite Edition layer is not on it yet.
- **wpa_supplicant built into the system image.** Fire OS's own wpa_supplicant on the vendor partition links a HIDL library Amazon added to its Android 9 VNDK, so it cannot start on Android's. The image builds wpa_supplicant from source with MediaTek's driver commands, overrides the vendor's service with it and declares its HAL in the framework manifest. A patch lets init keep the interfaces of a service definition that another one overrides.
- **Settings Fire OS's vendor partition expects from the system image.** The SELinux mapping files under the version name Fire OS's vendor policy uses (28.0.0), an empty `ro.frp.pst` because the Show has no frp partition, the USB IDs Fire OS's vendor init scripts read and the navigation bar.
