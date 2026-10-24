# Changelog

All notable changes to LineageOS for Amazon Echo devices, Kiosk Satellite Edition are documented here.

## Unreleased

### Added
- **One build for several Echo models.** The repository syncs LineageOS 18.1 with R0rt1z2's amazon-oss MT8163 projects, stages a shared Kiosk Satellite Edition layer and per-device patches into one tree and builds in Docker. The Echo Dot (2nd gen) comes first.
- **Echo Dot (2nd gen) on LineageOS 18.1.** The Dot boots LineageOS 18.1 on amazon-oss's 4.9 kernel with our own Biscuit kernel support: the device tree, the mic array FPGA and status frame, the IS31FL3236 LED ring, board thermistors and a `biscuit_defconfig`. Our own device tree brings up Wi-Fi, Wi-Fi adb, Bluetooth as a speaker, the light sensor and a headless display that SurfaceFlinger never draws to. Builds install from TWRP on amonet v2 into slot A only.
- **Debloated Dot build.** The Dot drops the apps, services, overlays, fonts and HALs a headless speaker never uses, as CM14 did. That brings the system image down to 460 MB and frees about 190 MB of RAM with Kiosk Satellite connected. KSM is on and Wi-Fi power save stays off.
- **Credits for meiser79's Dot port.** [docs/credits.md](docs/credits.md) records which Dot components his port guided.
