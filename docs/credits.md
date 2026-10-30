# Credits

## R0rt1z2 and amazon-oss

[R0rt1z2](https://github.com/R0rt1z2) made the shared base every device builds on: LineageOS 18.1 for the MT8163 Echos, the 4.9 kernel, `mt8163-common`, `hardware/amazon` and the vendor trees in [amazon-oss](https://github.com/amazon-oss). R0rt1z2 also continued the Echo Dot's port of amonet, released it with TWRP and wrote kaeru.

The Echo Show 5 (2nd gen) image is R0rt1z2's `cronos` port in amazon-oss and the Echo Show 8 (1st gen) image is R0rt1z2's `crown` port. Each brings its Show's screen, camera and Bluetooth, with this repository's Kiosk Satellite Edition layer on top.

## hkfuertes' CM14.1 port

[hkfuertes](https://github.com/hkfuertes) ported CM14.1 to the Echo Dot (2nd gen) in [amazon_device_biscuit](https://github.com/hkfuertes/amazon_device_biscuit), which the CM14.1 Kiosk Satellite Edition image was built on. This repository's Echo Dot image carries that work over to Android 11:

- **BiscuitService and `biscuit_service`:** the service Kiosk Satellite drives the Dot through and its command, with the service's package, AIDL interface and intents.
- **The light ring:** `biscuit-ledd`, `biscuit-ledctl` and their animations.
- **The audio route:** the speaker route and amplifier control behind the Fire OS audio HAL.
- **The light sensor HAL.**

## meiser79's Echo Dot port

[meiser79](https://github.com/meiser79) brought the Echo Dot (2nd gen) up on LineageOS 18.1 first, in [android_device_amazon_biscuit](https://github.com/meiser79/android_device_amazon_biscuit), [android_kernel_amazon_biscuit](https://github.com/meiser79/android_kernel_amazon_biscuit) and [android_vendor_amazon_biscuit](https://github.com/meiser79/android_vendor_amazon_biscuit). This repository does not include meiser79's code. The README, TODO and port study in [the device tree](https://github.com/meiser79/android_device_amazon_biscuit) showed what the Dot needs and what to avoid. This table records which component that work guided and how.

| Component | What the port showed us |
| --- | --- |
| Manifest | The Dot's MT8163 connsys needs amazon-oss's `hardware/mediatek/mt66xx` (libbt-vendor and the Wi-Fi HAL), not `mt76xx` like the Shows |
| Testing on the Dot | amonet v2.0.0's kaeru boots `boot_a` whatever slot is active ([kaeru #102](https://github.com/R0rt1z2/kaeru/issues/102)), so a second kernel in slot B needs kaeru with the A/B fix |
| Memory (`init.target.biscuit.rc`) | The 4.9 kernel costs about 55 to 60 MB more than CM14's 3.18 kernel: unreclaimable slab, 16 KB arm64 kernel stacks and more kernel threads. KSM's rmap items cost about 6 MB but KSM shares about 39 MB, which is why the Dot turns KSM on |
| Package state | Never disable `com.android.packageinstaller` on Android 11: without an installer PackageManagerService refuses to start |
| Kernel: device tree | Fire OS 6's LK reports 504 MiB and the 4.9 kernel hangs after `paging_init` unless the top 8 MiB (0x5f800000) stays unmapped. The port's commit titles also pointed at `imgsys` having to stay enabled for the DAC's MCLK |
| Kernel: mic array | The port's commit titles pointed at the 8-channel FPGA status frame and the TI DAC links needing work on 4.9. We traced both in Amazon's Fire OS 6.5.7.1 Dot kernel |
| Debloat (`device/biscuit/Android.bp`, `init/`, `BoardConfig.mk`) | The names of the theme, icon, accent, navigation bar and display cutout overlays and of the small system packages a headless Dot can drop. That the font loader skips missing files, so the CJK fonts can go. That LiveDisplay's HAL never registers on the Dot while its VINTF fragment declares it, which blocks LiveDisplayService until the watchdog restarts system_server. That a declared HAL that never starts blocks its clients in general. That inherited `PRODUCT_COPY_FILES` can be filtered in the board config. That Fire OS 6's TEE has nothing for `securetime` and `rpmb_svc`. That a Dot has to stay awake on power |
| Radio (`patches/biscuit/kernel/amazon/mt66xx-common`, `patches/biscuit/system/bt`, `init.target.biscuit.rc`) | That Fire OS 6's `wmt_launcher` passes the connsys ROM patch as a full path, which the firmware loader never finds. That its `wmt_loader` publishes the chip ID under the Android 7 property name while the mt66xx `libbt-vendor` reads the vendor one. That the MT8163 controller rejects a feature page it reports, which Android 11's Bluetooth stack aborts on (`patches/biscuit/system/bt`) |
| Kernel: memory map (`patches/biscuit/kernel/amazon/mt8163-4.9/0010`) | The port's commit list carries a smaller arm64 sparsemem section size. We backported the upstream Linux commit it matches, which halves the Dot's 16 MB `struct page` map |
| Kernel: audio clocks (`patches/biscuit/kernel/amazon/mt8163-4.9/0011`) | That the Dot's audio node carried the clock names of a `CONFIG_SND_I2S_MCLK` build, so the driver prepared no clock, the AFE never ran and every speaker write timed out |
| Bluetooth speaker (`sources/device/amazon/biscuit/bluetooth`, `device/biscuit/biscuit-service`) | That a Dot playing what phones send should present itself as a loudspeaker with no input or output. That BiscuitService has to answer the pairing requests a headless Dot gets |
| Audio: speaker amplifier (`sources/device/amazon/biscuit/audio`) | That the amplifier has to come on only after the first write has powered up the codec. Otherwise the first playback after boot crackles |
| Kernel: mic array FPGA | The 4.9 FPGA reset and programming delays are 2 ms against 100 ms on the Dot's 3.18 kernel |
| Kernel: suspend (`patches/biscuit/kernel/amazon/mt8163-4.9/0014`) | That the Dot panics when it suspends without the MD32 node. We reproduced the panic in `read_md32_cfgreg` and enabled the node as the Echo Shows' device trees do |

## Unlocking

- [xyz](https://github.com/xyzz/amonet) wrote amonet.
- [k4y0z](https://github.com/chaosmaster) made the first port of amonet to the Echo Dot.
- [R0rt1z2](https://github.com/R0rt1z2) continued [that port](https://github.com/R0rt1z2/amonet/tree/mt8163-biscuit), released it with [TWRP](https://github.com/R0rt1z2/twrp_device_amazon_echo-mt8163) and keeps the [XDA thread](https://xdaforums.com/t/unlock-root-twrp-unbrick-amazon-echo-dot-2nd-gen-2016-biscuit.4761416/).
- [bboe](https://github.com/bboe) wrote [firebreak](https://github.com/bboe/firebreak), which unlocks a Dot in one run.
- [k4y0z](https://github.com/chaosmaster) and [R0rt1z2](https://github.com/R0rt1z2) made amonet's `fastbrick`, which unlocks the Echo Shows on amonet 2. R0rt1z2 keeps the Echo Show 5 (2nd gen) [XDA thread](https://xdaforums.com/t/unlock-root-twrp-unbrick-amazon-echo-show-5-2nd-gen-2021-cronos.4772596/) and the Echo Show 8 (1st gen) [XDA thread](https://xdaforums.com/t/unlock-root-twrp-unbrick-amazon-echo-show-8-1st-gen-2019-crown.4766687/).

## SpeexDSP

The Echo Shows' echo canceller uses [SpeexDSP](https://gitlab.xiph.org/xiph/speexdsp)'s linear echo canceller below 1 kHz, by the Xiph.Org Foundation and Jean-Marc Valin among others. It is in `common/audio/amznaec/speexdsp` under its BSD license, in `COPYING` there.
