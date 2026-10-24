# Changelog

All notable changes to LineageOS for Amazon Echo devices, Kiosk Satellite Edition are documented here.

## Unreleased

### Fixed
- **Media provider restart loop on the Dot.** The media provider waits for the DRM server when it starts, which the Dot had left out, so Android killed and restarted it every 80 seconds and shared storage never mounted.
- **mediaserver crash in `dumpsys`.** Without the OMX service there is no codec list to show, and `dumpsys media.player` crashed mediaserver.

### Added
- **One build for several Echo models.** The repository syncs LineageOS 18.1 with R0rt1z2's amazon-oss MT8163 projects, stages a shared Kiosk Satellite Edition layer and per-device patches into one tree and builds in Docker. The Echo Dot (2nd gen) comes first.
- **Echo Dot (2nd gen) on LineageOS 18.1.** The Dot boots LineageOS 18.1 on amazon-oss's 4.9 kernel with our own Biscuit kernel support: the device tree, the mic array FPGA and status frame, the IS31FL3236 LED ring, board thermistors and a `biscuit_defconfig`. Our own device tree brings up Wi-Fi, Wi-Fi adb, Bluetooth as a speaker, the light sensor and a headless display that SurfaceFlinger never draws to. Builds install from TWRP on amonet v2 into slot A only.
- **Debloated Dot build.** The Dot drops the apps, services, overlays, fonts and HALs a headless speaker never uses, as CM14 did. KSM is on and Wi-Fi power save stays off.
- **236 MB of free RAM on the Dot.** With Kiosk Satellite connected the Dot now has 236 MB available, up from 190 MB. Only system_server, Kiosk Satellite and Bluetooth run as apps: the empty launcher runs inside system_server, Android keeps no idle processes around and the Dot declares none of the features whose services it never uses, such as autofill, backup, print and Wi-Fi Direct. Shared storage runs on sdcardfs instead of FUSE. A backported upstream kernel change halves the kernel's page map, which gives Android 19 MB more RAM.
- **Audio on the Dot.** The speaker plays and the microphones listen through the Fire OS 6 audio HAL behind our own Android 11 wrapper, with CM14's speaker removal across all seven microphones: about 18 to 20 dB less music, and the wake word works with music at 100%. The speaker plays at +3 dB, and `adb shell setprop persist.biscuit.spk_gain_db` with a value from -6 to 29 changes it within a second. Amazon's audio processor stays off, as on CM14.
- **The Dot's LED ring, buttons and light sensor.** `biscuit-ledd` takes the ring over from the kernel's boot animation and drives it for Kiosk Satellite: frames, animations, the volume overlay and timer countdowns. The volume, microphone and Action buttons work, and BiscuitService gives Kiosk Satellite the same interface as on CM14, from the ring and volume to microphone mute, Bluetooth pairing and Wi-Fi. The ambient light sensor reads on Dots with either light sensor chip. System updates are not available yet.
- **The Dot's name on the network.** The Dot asks the router for its address as echo-dot and the end of its serial, as on CM14, instead of as localhost.
- **Faster Dot boot.** system_server and the system apps are compiled ahead of time, which cuts boot from 39 to 27 seconds. The system image is 517 MB.
- **Credits for meiser79's Dot port.** [docs/credits.md](docs/credits.md) records which Dot components his port guided.
