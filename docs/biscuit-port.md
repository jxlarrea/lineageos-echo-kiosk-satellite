# Echo Dot (2nd gen) on LineageOS 18.1

The plan for bringing the Echo Dot (`biscuit`) up on the shared amazon-oss base. All Dot code here is our own. Where meiser79's port guided a component, [credits.md](credits.md) says so.

## Sources

| Source | What it gives us |
| --- | --- |
| amazon-oss `kernel/amazon/mt8163-4.9` | The 4.9 kernel, `mt8163.dtsi` and the other Echos' boards as examples (`rook` is the closest: Echo Spot) |
| Fire OS 6.5.7.1 GPL source (`Echo_Dot_src-6.5.7.1-20251024.tar.bz2`, pinned in the CM14 repo) | The Dot's 3.18 kernel: `biscuit.dtsi`, `biscuit_evt.dts`, `biscuit_defconfig` and Amazon's drivers for the LED ring, the light sensors and privacy mode |
| Fire OS 6.5.7.4 OTA (pinned in the CM14 repo) | The Dot's own blobs: connsys loader and firmware, the audio HAL and its libraries |
| amazon-oss `device/amazon/mt8163-common`, `hardware/amazon`, `vendor/amazon/mt8163-common` | Everything the Echos share on Android 11: board config, audio HAL wrapper, blob shims, lights, sensors, amonet init |
| Our CM14 Dot repo (`lineageos-biscuit-kiosk-satellite`) | BiscuitService, the LED daemons, the light sensor HAL, the Fire OS 6 audio wrapper and `mic_beam`, `ks-install.sh`, the OTA service, amonet v2 releasetools |

## Gates

Each gate ends with a build installed into slot A on the test Dot. Slot B keeps 2026.10.5.

0. **Kernel builds.** `biscuit` dts on arm64 4.9 from `biscuit.dtsi`, a `biscuit_defconfig` and the boot image layout for the Dot's 512 MB.
1. **Boots to adb over USB.** Kernel command line and the amonet v2 slot, slotted fstab, headless display, SELinux.
2. **Radio.** connsys modules and the Fire OS 6 loader, Wi-Fi, Bluetooth.
3. **Audio.** Speaker and the mic array. Try amazon-oss's `audio.primary.amazon_wrapper` with the Fire OS 6 HAL first.
4. **Dot hardware.** LED ring, buttons, light sensor, microphone privacy.
5. **Kiosk Satellite Edition.** `ks-install`, BiscuitService, system updates.
6. **Go or no-go.** `MemAvailable` with Kiosk Satellite idle, and the wake word over full-volume music against 2026.10.5.

## What the CM14 image does, on Android 11

The new port carries everything the CM14 Dot image (`lineageos-biscuit-kiosk-satellite`) does that still applies on Android 11. Two CM14 pieces came from meiser79's work and are rewritten here: the speaker amplifier timing (CM14 patch 031) and the TSL2540 fallback in `sensors_biscuit.c`.

### Leaving things out

Only the Dot leaves these out. The Echo Shows have a screen and keep WebView, SystemUI and the rest.

| CM14 | Android 11 | Gate |
| --- | --- | --- |
| Empty HOME activity that also overrides every unused app (`empty-launcher/`) | The same, drawn in software: hardware acceleration on Android 11 loads SwiftShader into HWUI, 17 MB against 5 MB on CM14 | 1b |
| No SystemUI, wallpaper service, LiveDisplay, WebView, telephony, user apps, backup, printing, text to speech | The same list with Android 11's names (Trebuchet, Etar, Jelly, Recorder, Seedvault, Updater and so on) | 1b |
| No boot animation, 512 MB Dalvik heap, `ro.config.low_ram`, at most 8 cached processes | Boot animation and heap are in. Cached process limit to add | 1b |
| zram 256 MB with lzo (patch 029) | zram size and algorithm in the fstab and init | 1b |
| HWUI off for apps on a device without a GPU (patches 018, 019) | Port, or keep apps in software rendering | 1b |
| Wi-Fi P2P, Bluetooth PAN reported unsupported (patches 013, 024) | Feature files and Bluetooth config | 2 |
| STA-only supplicant (patch 022) | Supplicant build options | 2 |
| Measured on 18.1: `statsd`, `incidentd`, `storaged`, `credstore`, DRM and Widevine, `mediametrics`, `gpuservice`, `kisd`, the vendor OMX service, `android.ext.services`, `com.android.keychain`, the media providers | Leave out or stop what Android 11 runs without. Never the package installer, check what the media providers break first | 1b |

### Features

| CM14 | Android 11 | Gate |
| --- | --- | --- |
| Headless display: fake 1x1 display, no framebuffer usage, software GLES (patches 017, 020, 032) | `hwcomposer.biscuit`, SwiftShader and the FramebufferSurface patch | 1, done |
| amonet v2 slots and TWRP installs (patches 001, 002, releasetools) | Slotted fstab, kernel names slot A, `recovery.fstab` and the slot assertion | 1, done |
| Root adb without authentication (patch 014) | `WITH_ADB_INSECURE` and `/data/adbroot` | 1, done |
| adb over Wi-Fi on port 5555, and adbd that survives a host that never closed its connection (patch 038) | Port the adbd change, set the port | 2 |
| Radio: Fire OS 6 `wmt_loader` and `wmt_launcher`, device node permissions (patch 021), `wifi.interface` (patch 008) | `blobs/biscuit/radio.txt`, mt66xx modules, init | 2 |
| Wi-Fi power save off unless `persist.biscuit.wifi_powersave` is 1 (patch 033) | Port to Android 11's Wi-Fi stack | 2 |
| Stable hostname for mDNS from the serial (patch 015) | Port | 2 |
| Bluetooth speaker: A2DP sink, AVRCP controller, pairing without input (patches 009 to 012) | Port to Android 11's Bluetooth | 2 |
| Fire OS 6 audio HAL with route and amplifier control, GPIO mic mute, log fixes (patches 004, 005, 023, 025 to 028, 034) | Try amazon-oss's `audio.primary.amazon_wrapper` first. The Fire OS 6 HAL's PCM numbers may not match 4.9's | 3 |
| No crackle on the first sound after boot (patch 031) | Our own amplifier timing | 3 |
| Speaker removal from the seven mics (`mic_beam.c`, patch 036), `persist.biscuit.mic_beam` and `mic_beam_out` | Port. The test against Amazon's libasp path decides which one ships | 3 |
| Speaker high-pass and limiter, both off (patch 035) | Port | 3 |
| Mic preamp 40 to 72, `biscuit_mic_test` and the other audio tools | Port | 3 |
| Action button broadcast (patch 030), volume button claim (patch 037) | Port to Android 11's PhoneWindowManager | 4 |
| LED ring daemon and animations (`biscuit-ledd`, `biscuit-ledctl`) | Port, on our IS31FL3236 kernel driver | 4 |
| Light sensor HAL (`sensors_biscuit.c`) | Port the TSL2584 path, our own TSL2540 path | 4 |
| Mic button privacy | `amz_priv` port with its keypad hook | 4 |
| All four cores online | Check what `mt8163-common`'s power init does on 4.9 | 4 |
| BiscuitService and `biscuit_service`: Wi-Fi, Bluetooth, volume, mic, LED binder | Port | 5 |
| `ks-install.sh`: Kiosk Satellite on first boot, grants, device owner, `persist.biscuit.ks_install` | Port, with `curl` in the image | 5 |
| System updates: GitHub releases, checksum and signature, TWRP installs into slot A then B (`SystemUpdate.java`, `ota/`, `sign-release.sh`) | Port | 5 |
| Amazon's wake words: `biscuit-pryond` with Fire OS 6's Pryon and the DAVS models | Port, check the Android 7 libraries under Android 11's linker | 5 |
| FLAC decoding (patch 016) | Android 11 decodes FLAC itself | none |

## Testing a kernel

`tools/biscuit-boot-test/build.sh test` builds a boot image with a small init that saves the kernel log, the command line, memory, I2C devices and sound cards to `/cache/k49-test`, then reboots into recovery. `build.sh zip` turns any boot image into a TWRP zip that writes it into `boot_a`. Put the old boot image back with the same zip built from the 2026.10.5 `boot.img`.

There is no way to read the log of a kernel that does not reach init: TWRP has no pstore or `last_kmsg`, and the 4.9 kernel has no MediaTek ram_console for CM14's `last_kmsg` to read. A Dot whose ring stays dark needs the **+** button to reach TWRP.

## Status

**Gate 0 passed on 2026-10-24.** Our kernel (`patches/biscuit/kernel/amazon/mt8163-4.9`) boots on the test Dot and its test init runs. The mic ADCs, the DAC, the sound card, BTIF, the keypad and the thermistors probe. `MemTotal` is 481,608 kB against 492,868 kB on CM14's 3.18 kernel.

- The first attempt never reached the LED driver. It disabled the display and multimedia nodes the way Fire OS does and selected VBUS pin states the Dot does not have. Leaving the nodes enabled like rook and guarding the VBUS states fixed it. Which of the two it was is still open, and it matters for trimming memory later.
- The sound card puts the mic array capture at PCM 22 and the DAC at 23. On CM14's 3.18 kernel the mic array is PCM 24 (`mic_beam.c`). Check which numbers the Fire OS 6 audio HAL opens before Gate 3. The mic PCM has 9 channels: the seven microphones on 0 to 6, an unconnected ADC input on 7 and the codec's copy of the speaker signal on 8.
- Still to port for Gate 4: `amz_priv` with its keypad hook and the TSL2540 and TSL2583/2584 light sensor drivers.

**Gate 1 passed on 2026-10-24.** Our device tree boots LineageOS 18.1 on the test Dot from slot A: `boot_completed` at about one minute, adb over USB, SurfaceFlinger composing onto `hwcomposer.biscuit`'s 1x1 display. The debloated system is 472 MB of the 768 MB partition.

- Two first boots hung with system_server's watchdog restarting it every 100 seconds: `mt8163-common` declares a sensors HAL and the Dot had none, then Android 11's sensors service refused our CM14 HAL's device version 1.0. `sensors.biscuit` reports device version 1.3 now.
- `mt8163-common`'s USB init turns adb off on boot until system_server's USB manager turns it back on. The Dot's `init.target.rc` turns it on itself, so adb works even when system_server hangs.
- Audio runs on AOSP's stub primary HAL until Gate 3, since without a primary HAL audioserver keeps restarting.
- Memory two minutes after boot, without Kiosk Satellite: `MemAvailable` 205 MB, used 302 MB (kernel 61 MB). The largest: system_server 68 MB, SurfaceFlinger 37 MB (SwiftShader for a 1x1 display), zygote 29 MB, then Settings and its CryptKeeper, PermissionController, the media providers, LineageParts, KeyChain and the backup transport.
- Bluetooth crash-loops without a Bluetooth HAL (Gate 2). CameraManager retries the disabled camera service every second for LineageOS's power button torch.

**Gate 2 passed on 2026-10-24.** Wi-Fi joins on its own after a reboot, adb works over Wi-Fi on port 5555 without a key, Wi-Fi power save stays off (`persist.biscuit.wifi_powersave`), and Bluetooth is on as a speaker: A2DP source and sink, AVRCP target and controller, HID host and BLE. Kiosk Satellite 2026.10.38, installed by hand with the settings restored, starts at boot as device owner and connects to Home Assistant and Sendspin.

- The mt66xx modules load from `init.insmod.cfg`, then Fire OS 6's `wmt_loader` and `wmt_launcher` start connsys. The launcher passes the ROM patch as a full path, which the firmware loader never finds: `patches/biscuit/kernel/amazon/mt66xx-common` requests it by its base name.
- The MT8163 controller rejects feature page 2 (secure connections), which Android 11's Bluetooth stack aborted on: `patches/biscuit/system/bt`. Pages 0 and 1 read, so BLE works.
- The Bluetooth app started PBAP, which needs the contacts provider the Dot leaves out. The profile overlay is built into the Bluetooth app, not a runtime overlay.
- Still open from the CM14 list for Gate 2: the stable mDNS hostname (patch 015, it is `localhost` now), STA-only Wi-Fi without P2P (patches 022, 024) and adbd's keepalives (patch 038).
- Memory, all at 18 minutes of uptime with Kiosk Satellite connected (`MemAvailable`, 3 samples):

  | Build | `MemAvailable` | SurfaceFlinger | What changed |
  | --- | --- | --- | --- |
  | Gate 2 | 153 MB | 34 MB | |
  | mem1 | 182 MB | 13 MB | `hwcomposer.biscuit` takes every layer, so SurfaceFlinger never composes with SwiftShader. LineageParts, LocalTransport, SimpleDeviceConfig and the camera features out, no torch callback, the sensors HAL sleeps while unused |
  | mem2 | 190 MB | 10 MB | KSM on (6,033 pages shared after 3 full scans), DRM, ClearKey, Widevine, the OMX service and MediaTek's video codecs out with their VINTF declarations |

  The CM14 comparison, 298 MB, was taken at 8.4 hours of uptime. The kernel uses 72 MB against CM14's 36 MB.
- Earlier, 3 minutes after boot with Kiosk Satellite connected: `MemAvailable` 160 MB against the 225 MB target, used 351 MB, kernel 73 MB, swap in use 54 MB. Kiosk Satellite 85 MB, system_server 60 MB, SurfaceFlinger 32 MB, then the media provider, Settings, Bluetooth, PermissionController, ExtServices, the launcher, LineageParts, PackageInstaller, KeyChain and LocalTransport at 5 to 11 MB each.
