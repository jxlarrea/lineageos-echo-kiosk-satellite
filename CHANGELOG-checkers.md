# Echo Show 5 (1st gen) changelog

All notable changes to the Echo Show 5 (1st gen) (`checkers`) image are documented here. The [Echo Dot (2nd gen)](CHANGELOG-biscuit.md), [Echo Show 5 (2nd gen)](CHANGELOG-cronos.md) and [Echo Show 8 (1st gen)](CHANGELOG-crown.md) have their own.

## Unreleased

### Changed
- **The nightly storage trim runs.** Android's storage maintenance, which trims `/data` and `/cache`, waited for the device to be idle and charging. An Echo is never idle that way and most report no charging battery, so the trim never ran. It now runs once a day at 3 AM. Regular trims tell the eMMC which blocks are free, which helps it spread wear.
- **No more error report every 30 minutes.** The kernel has no per-app network statistics, so Android's statistics poll failed every 30 minutes and on every network change and wrote an error report to storage each time. Android now reports empty statistics there. Data usage numbers were already empty on this kernel.
- **No lock sound.** With the lock screen off, Android still played its lock sound at every boot and each time the screen turned off. A fresh install now has the lock and unlock sounds off, and EchoService turns them off once on a Show that is already set up. Settings > Sound turns them back on.

## 2026.11.1 - 2026-11-09

### Added
- **Echo Show 5 (1st gen) on LineageOS 18.1.** The repository builds the amazon-oss `checkers` tree by R0rt1z2 and bengris32 with the Kiosk Satellite Edition layer the other Echo Shows have: EchoService, Kiosk Satellite on first boot, the raw microphone for Kiosk Satellite, pryond for Amazon's wake word engine from files you install, Bluetooth both ways, system updates, the Kiosk Satellite boot animation and the same debloat. The model reads Echo Show 5 1st Gen (KS). EchoService names the Show `echo-show-5-g1-ks` and the end of its serial on the network.
- **A raw microphone for Kiosk Satellite.** Kiosk Satellite hears microphone 0 as captured, at Amazon's microphone gain. The image's echo canceller stays loaded and passes the microphones through, so Kiosk Satellite's own echo canceller sees the speaker's echo at a fixed delay. The raw level matches the Echo Show 5 (2nd gen)'s, and Kiosk Satellite 2026.11.14 or newer adds the same 20 dB base gain for it.
- **Privacy and the screen on separate presses.** The privacy chip works as the Echo Show 8's: it turns privacy on at the release of a hold of about a second and a half, and any press turns it off. A short press turns the screen on and off. Android does nothing on a hold. While privacy is on, Android leaves the press to the chip.
- **Double tap to wake.** The touch driver ignored every tap while the screen was off. A double tap now turns the screen back on, and a single tap leaves it off.
- **70% master volume on a fresh install.** A fresh install starts at master volume 10 of 15, which Kiosk Satellite shows as 70%, the Echo Show 5 (2nd gen)'s level, since both have the same speaker.
- **The Echo Show 5 (1st gen) guide.** [docs/echo-show-5-1st-gen.md](docs/echo-show-5-1st-gen.md) covers installing, setting up and updating the Show, its button, volume, Bluetooth and device commands. Unlocking and Getting into TWRP have sections for it: unlocking with `amonet-checkers-v2.0.2.zip`, including the bootloader check before `fastbrick`.
- **Release checks for the Echo Show 5 (1st gen).** `make check` checks its hostname, button, audio settings, volume and boot animation. `make test` fails if it inherits another Show's device layer or another Show inherits its own.

### Fixed
- **The microphones read zeros after privacy ended with a short press.** The privacy circuit lets the microphones back on at any press of the button, but the kernel driver only reported presses of half a second or more. After a short press the sound driver kept the microphones off, so every recording read zeros until a reboot. The driver now reports the microphones back as a long press does, as on the Echo Show 8.
