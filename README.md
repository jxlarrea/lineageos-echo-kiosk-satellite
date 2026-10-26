# LineageOS for Amazon Echo devices, Kiosk Satellite Edition

LineageOS 18.1 (Android 11) images for Amazon Echo devices, tuned to run [Kiosk Satellite](https://github.com/jxlarrea/kiosk-satellite) for Home Assistant. The Echo Shows run the full Kiosk Satellite experience on their screens. The Echo Dot has no screen, so it runs Kiosk Satellite headless as a voice satellite, intercom and speaker. Every image is built from one shared base, [R0rt1z2](https://github.com/R0rt1z2)'s LineageOS 18.1 port for the MT8163 Echos in [amazon-oss](https://github.com/amazon-oss), with a Kiosk Satellite Edition layer on top.

| Device | Codename | Status | Guide |
| --- | --- | --- | --- |
| Echo Dot (2nd gen) | `biscuit` | Ready | [Echo Dot](docs/echo-dot.md) |
| Echo Show 5 (2nd gen) | `cronos` | Ready | [Echo Show 5](docs/echo-show-5.md) |
| Echo Show 8 (1st gen) | `crown` | In progress | |

Each image leaves out what Kiosk Satellite never uses on that device, installs Kiosk Satellite on its first boot with every permission it needs and installs new releases of the image from this repository. The device's guide lists everything its image does.

## Installing

1. **Unlock the device and install TWRP.** [Unlocking](docs/unlocking.md) covers each device. [Getting into TWRP](docs/twrp.md) covers starting TWRP and checking its version.
2. **Install the image** with the steps in the device's guide.

The images come from the [releases](https://github.com/jxlarrea/lineageos-echo-kiosk-satellite/releases). Each release is for one device, with the device in its title and its codename in its tag, such as **Echo Dot (2nd gen) 2026.10.6** tagged `biscuit-2026.10.6`. The device's guide links its releases.

### Coming from the CM14.1 Echo Dot image

The Echo Dot used to have a LineageOS 14.1 image in [lineageos-biscuit-kiosk-satellite](https://github.com/jxlarrea/lineageos-biscuit-kiosk-satellite). Its last release, 2026.10.5, does not update to this image over the air. [Moving from the CM14.1 image](docs/echo-dot.md#moving-from-the-cm141-image) covers the move by hand, with Kiosk Satellite's settings kept.

## Updating

Once a device runs its image, the image installs new releases over the air. Home Assistant shows a new release on the device's **Firmware** update entity. Kiosk Satellite shows it in its remote admin. The device's guide covers the details.

## Building

See [Building](docs/building.md). Builds run in Docker from one LineageOS 18.1 tree for every device.

## Credits

- [R0rt1z2](https://github.com/R0rt1z2), for the LineageOS 18.1 port for the MT8163 Echos in [amazon-oss](https://github.com/amazon-oss) that every image builds on, the Echo Show 5 (2nd gen) included. Also for amonet on the Echo Dot and the Echo Shows with its TWRP.
- [hkfuertes](https://github.com/hkfuertes), for the [CM14.1 port](https://github.com/hkfuertes/amazon_device_biscuit) of the Echo Dot. BiscuitService, the light ring daemon, the audio route and the light sensor HAL came from it.
- [meiser79](https://github.com/meiser79), who brought the Echo Dot up on LineageOS 18.1 first. [Their port](https://github.com/meiser79/android_device_amazon_biscuit) guided the Echo Dot image.
- xyz and k4y0z for amonet and its first Echo Dot port. k4y0z also made `fastbrick` with R0rt1z2.
- [bboe](https://github.com/bboe) for [firebreak](https://github.com/bboe/firebreak).

[Credits](docs/credits.md) records what each one's work brought to this repository.
