# Echo Dot (2nd gen) changelog

All notable changes to the Echo Dot (2nd gen) (`biscuit`) image are documented here. The [Echo Show 5 (2nd gen)](CHANGELOG-cronos.md) and [Echo Show 8 (1st gen)](CHANGELOG-crown.md) have their own.

## Unreleased

### Changed
- **The docs no longer cover moving from the CM14.1 image.** The README section, the Echo Dot guide's Moving from the CM14.1 image section and the notes about Dots on 2026.10.5 are gone. The CM14.1 image's last release came out before this image. Dots still on it can install this image by hand with the steps in Installing.
- **The README lists the Echos in progress.** The devices table now lists the Echo Dot (3rd gen) as `donut`, the Echo Show 5 (1st gen) as `checkers` and the Echo Show 8 (2nd gen) as `athena`, all as work in progress.
- **One changelog for each device.** `CHANGELOG.md` is now `CHANGELOG-biscuit.md`, `CHANGELOG-cronos.md` and `CHANGELOG-crown.md`, with release headings such as `## 2026.10.15 - 2026-10-29`. A build takes its release from its own device's changelog, so an Unreleased section for one Echo never touches another's builds.
- **Setting the time server and the time zone.** The Echo Dot guide has a new Time section. It covers using your own NTP server instead of `time.android.com` and setting the time zone from adb, since the Dot starts on GMT. Android 11 ignores an NTP server the router hands out over DHCP ([#25](https://github.com/jxlarrea/lineageos-echo-kiosk-satellite/issues/25)).

## 2026.10.15 - 2026-10-29

### Fixed
- **The speaker popped when a sound played after a few seconds of silence** ([#21](https://github.com/jxlarrea/lineageos-echo-kiosk-satellite/issues/21)). Android put the speaker output into standby after 3 seconds without sound, and every standby switched the amplifier off. The next sound, such as the wake chime or the answer after a long thinking phase, popped when the amplifier came back on. The speaker output now stays on, so the amplifier is never switched off between sounds. Bluetooth headphones and speakers still go idle 3 seconds after the last sound.
- **The Dot rebooted if it ever suspended.** The image keeps the Dot awake on power and Kiosk Satellite holds it awake too, so it does not suspend in normal use. If it did, the kernel crashed and the Dot rebooted: the sleep code reads registers of the MT8163's MD32 processor, and the Dot's device tree left MD32 off, so they were never mapped. MD32 is now on, as on the Echo Shows, and a forced suspend completes and resumes. Thanks to meiser79 for pointing it out.

## 2026.10.14 - 2026-10-27

### Fixed
- **A system update only installs from TWRP 3.7.0_9-0** ([#13](https://github.com/jxlarrea/lineageos-echo-kiosk-satellite/issues/13)). The image needs amonet v2 and its TWRP 3.7.0_9-0, but an update runs in whatever TWRP the Dot has. On an Echo Show still on amonet 1 and its TWRP 3.2.3, an update left the system half written. The update step in TWRP now checks the TWRP version first. With anything else, such as amonet v1's 3.2.3 or 3.7.0_9-bboe2, it writes nothing to either slot and Android starts again, and BiscuitService reports why in Home Assistant and Kiosk Satellite. This applies to updates from this release on.

## 2026.10.13 - 2026-10-27

### Fixed
- **Streaming stalled on Wi-Fi networks with several access points.** Android 11 looks for a better access point after every scan while a connection carries little traffic. The Echos' Wi-Fi drivers report no traffic at all. On a mesh network Android moved an Echo Show 8 between access points every 20 seconds, even in the middle of a voice conversation. Each move stalled audio and dropped Home Assistant. Android also ranked access points by a throughput estimate that these drivers get wrong, so it picked a weak mesh node over the router next to it. Android now keeps the access point it connected to and picks one by signal strength, preferring a 5 GHz network down to -50 dBm. This applies to every Echo. On the Dot, a scan while connected stalled the link for up to half a second at a time, and the Echo Dot guide now describes the fix.
- **`biscuit_service bt connect` said "found none" for headphones that had connected.** Headphones paired before reconnect by themselves once Bluetooth restarts in the headphones role, which could beat the connection BiscuitService asked for. BiscuitService then searched for headphones in pairing mode and missed that these had connected. It now counts any connection to the headphones it looks for.
- **The microphone check in the Echo Dot guide** ([#6](https://github.com/jxlarrea/lineageos-echo-kiosk-satellite/issues/6)). It stopped Kiosk Satellite with `am force-stop` and ran `biscuit_mic_test` from a normal `adb shell`, as on CM14.1. Neither works on Android 11. A device owner cannot be force-stopped and `adb shell` cannot open the microphones. The guide now turns off Voice Satellite in Kiosk Satellite and runs the test with `adb root`. The Echo Show 5 guide does the same with `echo_mic_test`.

## 2026.10.12 - 2026-10-26

### Fixed
- **Kiosk Satellite's File Manager could not open shared storage.** It said "Cannot read folder: Permission denied". The Dot keeps shared storage on sdcardfs instead of FUSE to save memory. Without FUSE, Android 11 only lets an app into shared storage with the legacy storage permission, which apps that target Android 11 or later never get. All files access only counts under FUSE, so Kiosk Satellite got an empty view although ks-install had given it all files access. An app with all files access now sees all of shared storage, as a legacy app with write access does (`patches/common/frameworks/base`). This fix applies to every Echo.

### Changed
- **75% media volume on a fresh Dot.** A fresh install starts at media volume 11 of 15, which Kiosk Satellite shows as 75%, with Kiosk Satellite's media player and assistant volumes at their default 100. The Echo Dot guide recommends these levels. A Dot that is already set up keeps its volume.
- **Amazon's wake word engine is shared with the Echo Shows.** `biscuit-pryond`, Pryon and its models moved from the Dot's device tree to the layer every Echo gets, with the same files, the same service and the same `ro.biscuit.pryon`. `scripts/extract-biscuit-blobs.sh` writes Pryon to `pryon-vendor.mk` and the Dot's other Fire OS files to `biscuit-vendor.mk`. `make check` fails a Dot image without the engine.

## 2026.10.11 - 2026-10-25

### Fixed
- **Wi-Fi networks that offer WPA2 with SHA256** ([#5](https://github.com/jxlarrea/lineageos-echo-kiosk-satellite/issues/5)). Android 11 adds WPA2's SHA256 key management (PSK-SHA256) to every saved WPA2 network, and picks it when the access point offers it too, as many WPA2/WPA3 networks do. The Dot's Wi-Fi driver cannot do it, so the Dot never joined such a network and logged "Association request to the driver failed". It now connects with plain WPA2, as the CM14 image did. This fix applies to every Echo. A network that requires protected management frames (PMF) still cannot be joined. The Echo Dot guide says so.

### Changed
- **The real Wi-Fi MAC address in Home Assistant.** ks-install now makes Kiosk Satellite the device owner before anything starts it, so on a fresh install its first run can read the Dot's real Wi-Fi MAC address. From its release after 2026.10.40, Kiosk Satellite then turns on Use real Wi-Fi MAC address by itself, so its ESPHome device links with the one Home Assistant's network integrations already track. An existing install keeps its setting, since changing it creates a new ESPHome device in Home Assistant. Per-network MAC randomization was already off on every Echo, and the image now keeps it off for good. This applies to every Echo.

## 2026.10.10 - 2026-10-25

### Fixed
- **Kiosk Satellite installed twice on a fresh Dot** ([#3](https://github.com/jxlarrea/lineageos-echo-kiosk-satellite/issues/3)). Since 2026.10.7 the Dot also got the Echo Shows' layer: their ks-install, which ran next to the Dot's own at every boot, and EchoService, a second service and updater the Dot does not use. In 2026.10.9 the Shows' 8 MB kernel reserve also replaced the Dot's 4 MB one, which left about 20 MB less memory available. The Dot now only gets what every Echo shares, the Wi-Fi overlay.
- **`biscuit_service adb root` printed its usage after it worked.** It set the switch, then went on to the usage text and exited with an error.

### Changed
- **Release images are checked before they go out.** `make check DEVICE=<device>` fails if an image misses one of its own device's parts, carries another device's or reports a release other than the one in this file, and `make test` fails if the Dot's layer ever inherits the Echo Shows'. A clean release build refuses to start with uncommitted changes.

## 2026.10.9 - 2026-10-25

### Added
- **adb root on the Dot.** LineageOS 18.1 only allows `adb root` after Rooted debugging is turned on in Developer options, which the Dot has no screen for. `biscuit_service adb root on` turns it on and `off` turns it off again. It stays off by default. The Echo Dot guide explains it.

## 2026.10.8 - 2026-10-25

### Fixed
- **Wi-Fi passwords with a quote or a backslash** ([#2](https://github.com/jxlarrea/lineageos-echo-kiosk-satellite/issues/2)). `biscuit_service wifi connect` put a backslash before every `"` and `\` in the network name and password, as Android 7 needed. Android 11 hands them to Wi-Fi as they are, so those backslashes became part of the password and the Dot never connected, without an error. They now go as typed. When Android turns a network down, such as for a password shorter than 8 characters, BiscuitService now logs it.

## 2026.10.7 - 2026-10-25

### Fixed
- **Wi-Fi networks in WPA2/WPA3 transition mode** ([#1](https://github.com/jxlarrea/lineageos-echo-kiosk-satellite/issues/1)). Android 11 upgrades a saved WPA2 network to WPA3 (SAE) when the access point offers both, but the Echos' Wi-Fi drivers cannot do SAE, so the Dot never joined such a network. It now connects with WPA2, as the CM14 image did. This fix applies to every Echo. A network that only offers WPA3 still cannot be joined. The Echo Dot guide says so.
- **Kiosk Satellite could not restart itself.** ks-install granted all files access, but not the other permissions Android 11 only gives through their own Settings screens. Restarting needs Display over other apps, so Kiosk Satellite tried to open a Settings screen the Dot cannot show. ks-install now also allows Kiosk Satellite to display over other apps, change system settings, read app usage and install its own updates, on every boot.

## 2026.10.6 - 2026-10-25

### Changed
- **The Echo Show 5 (2nd gen) instead of the 1st gen.** The second Echo Show this repository plans for is the Echo Show 5 (2nd gen), `cronos`, in place of the Echo Show 5 (1st gen), `checkers`. The manifest syncs its amazon-oss device and vendor trees.

### Fixed
- **Stale adb connections over Wi-Fi.** A computer that went to sleep or lost its connection to the Echo without closing adb used to stay connected in adbd for as long as the Echo ran. adbd now checks idle connections every 30 seconds and drops one whose host is gone within about 5 minutes. This fix applies to every Echo.
- **System update checks from Kiosk Satellite got stuck.** BiscuitService runs in system_server, so a check or install Kiosk Satellite asked for ran with Kiosk Satellite's identity. Android refused the status broadcast and the update stayed at "checking" until a reboot. BiscuitService now makes those calls as itself, as it does for the microphone mute, and a failed status broadcast can no longer leave an update stuck.
- **Media provider restart loop on the Dot.** The media provider waits for the DRM server when it starts, which the Dot had left out, so Android killed and restarted it every 80 seconds and shared storage never mounted.
- **mediaserver crash in `dumpsys`.** Without the OMX service there is no codec list to show. `dumpsys media.player` crashed mediaserver.
- **Audio froze after one slow call.** amazon-oss stops Android from restarting audioserver when an audio call takes over 5 seconds, but the expired call then timed out again and again while holding a lock every audio call needs, so audio stayed down. It now gives up on the call once and lets the others go on. This fix applies to every Echo.
- **Slow audio start on the Dot.** The Fire OS audio libraries waited about 4 seconds at boot for a Fire OS service that does not exist on LineageOS. They now go on without it at once.
- **Phones could not play to the Dot.** The Bluetooth stack was built without the A2DP sink's service record, so phones paired but never found a speaker to play to. The pairing confirmation also went to a Settings dialog a Dot has no screen to show. BiscuitService now accepts the pairing while pairing mode lasts. The Dot presents itself as a loudspeaker with no input or output, so phones list it with speakers and pair without a code to compare. On its first boot the Dot starts out taking audio from phones.

### Added
- **One build for several Echo models.** The repository syncs LineageOS 18.1 with R0rt1z2's amazon-oss MT8163 projects, stages a shared Kiosk Satellite Edition layer and per-device patches into one tree and builds in Docker. The Echo Dot (2nd gen) comes first.
- **Echo Dot (2nd gen) on LineageOS 18.1.** The Dot boots LineageOS 18.1 on amazon-oss's 4.9 kernel with our own Biscuit kernel support: the device tree, the mic array FPGA and status frame, the IS31FL3236 LED ring, board thermistors and a `biscuit_defconfig`. Our own device tree brings up Wi-Fi, Wi-Fi adb, Bluetooth as a speaker, the light sensor and a headless display that SurfaceFlinger never draws to. Builds install from TWRP on amonet v2 into slot A only.
- **Debloated Dot build.** The Dot drops the apps, services, overlays, fonts and HALs a headless speaker never uses, as CM14 did. KSM is on and Wi-Fi power save stays off. Logs stay in memory instead of being written to the Dot's flash.
- **235 MB of free RAM on the Dot.** With Kiosk Satellite connected and Amazon's wake word engine listening, the Dot has 235 MB available, up from 190 MB without it. The kernel's emergency reserve is 4 MB instead of the 16 MB mt8163-common sets for Echos with twice the Dot's RAM, close to the kernel's own 2.8 MB that Fire OS and CM14 used, which is worth about 39 MB. Java heaps keep less free space after each collection. Only system_server, Kiosk Satellite and Bluetooth run as apps: the empty launcher runs inside system_server, Android keeps no idle processes around and the Dot declares none of the features whose services it never uses, such as autofill, backup, print and Wi-Fi Direct. Shared storage runs on sdcardfs instead of FUSE. A backported upstream kernel change halves the kernel's page map, which gives Android 19 MB more RAM.
- **Audio on the Dot.** The speaker plays and the microphones listen through the Fire OS 6 audio HAL behind our own Android 11 wrapper, with CM14's speaker removal across all seven microphones: about 18 to 20 dB less music. The wake word works with music at 100%. The speaker plays at +3 dB. `adb shell setprop persist.biscuit.spk_gain_db` with a value from -6 to 29 changes it within a second. Amazon's audio processor stays off, as on CM14.
- **Bluetooth headphones and speakers on the Dot.** `biscuit_service bt connect` pairs with the first headphones or speaker in pairing mode or with the first whose name has the given text in it. Then it plays to them. With a name, ones paired before reconnect without pairing mode. Android 11 cannot play to headphones and take audio from phones at the same time, so the Dot switches between the two: `bt connect` plays to headphones and `bt pair` takes phones again. The Dot keeps the last choice across reboots.
- **5 GHz DFS Wi-Fi on the Dot.** The Dot now sees and joins networks on channels 52 to 144. The kernel builds in the wireless regulatory database and the Wi-Fi driver's DFS channels. The Dot defaults to US rules.
- **The Dot's line out jack.** Audio plays through the line out when something is plugged in, with the Dot's speaker off. It moves back to the speaker when it is unplugged. The jack keeps its own volume and plays at full level, without Android's headphone volume limit, which waits for a warning the Dot cannot show.
- **The Dot's LED ring, buttons and light sensor.** `biscuit-ledd` takes the ring over from the kernel's boot animation and drives it for Kiosk Satellite: frames, animations, the volume overlay and timer countdowns. The volume, microphone and Action buttons work. BiscuitService gives Kiosk Satellite the same interface as on CM14, from the ring and volume to microphone mute, Bluetooth pairing and Wi-Fi. Kiosk Satellite hears the Action and volume buttons as BiscuitService's broadcasts, once for each press and release, without an accessibility service. The ambient light sensor reads on Dots with either light sensor chip.
- **The Dot's name on the network.** The Dot asks the router for its address as echo-dot-ks and the end of its serial instead of as localhost. Android names it Amzn Echo Dot (KS), so Home Assistant and the router show that it runs this image. A Dot moved from CM14 gets the new name too.
- **Kiosk Satellite on first boot.** A freshly installed Dot downloads the latest Kiosk Satellite from GitHub once it is online and makes it the device owner, as on CM14. On every boot it gets the microphone, location, storage, all files access and the battery exemption, since nothing on a Dot can answer a permission prompt. `adb shell setprop persist.biscuit.ks_install 0` turns this off.
- **Amazon's wake words on the Dot.** `biscuit-pryond` runs Fire OS's Pryon engine for Kiosk Satellite with the Alexa, Echo and Computer models of every locale. It loads nothing until Kiosk Satellite asks it to listen.
- **System updates on the Dot.** Kiosk Satellite and `adb shell biscuit_service update check` and `update install` find a newer release of this image on GitHub, check its checksum and signature and install it through TWRP, into slot A and then slot B, as on CM14.
- **Faster Dot boot.** system_server and the system apps are compiled ahead of time, which cuts boot from 39 to 27 seconds. The system image is 517 MB.
- **Documentation for each device.** The README lists the devices and links each one's guide. [The Echo Dot guide](docs/echo-dot.md) covers installing, moving from the CM14.1 image with Kiosk Satellite's settings kept, updating, settings and device commands. [Unlocking](docs/unlocking.md) and [Getting into TWRP](docs/twrp.md) are shared, with a section for each device. [Building](docs/building.md) covers the build and releases.
- **Releases for each device.** Each Echo has releases of its own, tagged with its codename, such as `biscuit-2026.10.6` for the Echo Dot. The Dot's updater only looks at its own. Each device's guide links its releases.
- **Credits.** [docs/credits.md](docs/credits.md) credits R0rt1z2's amazon-oss base, hkfuertes' CM14.1 port that BiscuitService, the light ring and the light sensor HAL came from. It also records the components [meiser79's port](https://github.com/meiser79/android_device_amazon_biscuit) guided.
