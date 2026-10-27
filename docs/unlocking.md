# Unlocking

Every image installs from TWRP, which needs an unlocked bootloader. Each Echo has its own way to unlock, with a port of amonet, an exploit of MediaTek's boot chain. This page covers each device this repository has an image for. Once a device is unlocked, [Getting into TWRP](twrp.md) covers starting TWRP. The device's guide covers the installation.

## Echo Dot (2nd gen)

Use [firebreak](https://github.com/bboe/firebreak) to unlock the Echo Dot (2nd gen) over USB. It installs amonet v2.0.1 and its TWRP 3.7.0_9-0 in the same run, which is what the image needs. The image also installs on amonet v2.0.0, so a Dot that runs the CM14.1 image 2026.10.5 is ready as it is.

> [!IMPORTANT]
> Do not set up the Dot in the Alexa app before you unlock it. If it gets on Wi-Fi, it can update to a Fire OS build firebreak does not support.

### What you need

- The Echo Dot (2nd gen), its power adapter and a micro USB cable that carries data.
- A computer with `adb` and `fastboot` from Android's platform-tools and [uv](https://docs.astral.sh/uv/), which runs firebreak.

### Run firebreak

Pick your system and run its commands. They install the tools and start firebreak, which takes the Dot to rooted Fire OS 6 on amonet v2.0.1.

**Linux** (Debian and Ubuntu). Run firebreak without `sudo`:

```sh
sudo apt install adb fastboot curl
curl -LsSf https://astral.sh/uv/install.sh | sh
uvx firebreak@latest unlock
```

**macOS** (with [Homebrew](https://brew.sh)):

```sh
brew install --cask android-platform-tools uv
uvx firebreak@latest unlock
```

**Windows.** First install the fastboot and MediaTek drivers from an administrator PowerShell, as the Windows section of firebreak's [README](https://github.com/bboe/firebreak#windows) shows. Then open a new PowerShell, not as administrator:

```powershell
winget install Google.PlatformTools
winget install astral-sh.uv
uvx --managed-python firebreak@latest unlock
```

firebreak prints what to do with the Dot at each step. The first step is the fastboot gesture: unplug the USB cable, press and hold the Action button (the one with a dot), plug the cable back in and let go when the light ring turns green. If the ring stays solid blue, the USB connection is probably bad. Unplug it and try again or use another port.

The run takes several minutes. Do not unplug the Dot while it works. If it stops, run the same command again and it picks up where it left off. It ends with the Dot on amonet v2.0.1 with TWRP 3.7.0_9-0, running a rooted Fire OS 6. On the way, firebreak downloads amonet's files from the amonet XDA thread. If that fails, it opens your browser so you can download them yourself.

If the Dot shows no light at all, its test point has to be shorted. firebreak's [README](https://github.com/bboe/firebreak#run-it) covers that and how to ask for help if a run fails.

### A Dot on amonet v1

A Dot unlocked earlier with amonet v1 has TWRP 3.2.3 or 3.7.0_9-bboe2, which the image does not support. Use firebreak to update from amonet v1 to amonet v2:

```sh
uvx firebreak@latest unlock
```

The update rewrites the Dot's bootloader, so never unplug the Dot while it runs.

## Echo Show 5 (2nd gen)

Use [R0rt1z2](https://github.com/R0rt1z2)'s amonet 2 package for the Echo Show 5 (2nd gen), linked from R0rt1z2's [XDA thread](https://xdaforums.com/t/rom-unofficial-11-cronos-lineageos-18-1-for-the-amazon-echo-show-5-2021.4772598/). Its `fastbrick` script unlocks the Show over USB and writes TWRP 3.7.0_9-0, which is what the image needs. A Show that runs R0rt1z2's LineageOS 18.1 image v0.5 or later is already on amonet 2 and ready as it is.

### What you need

- The Echo Show 5 (2nd gen), its power adapter and a USB cable that carries data.
- A computer with Linux, macOS or Windows. The package brings its own `fastboot`.

### Run fastbrick

1. Download R0rt1z2's package for the Echo Show 5 (2nd gen) from the XDA thread and extract it.
2. Put the Show in fastboot mode. Unplug its power, then hold **Volume Down**, **Volume Up** and the **privacy button** together, plug the power back in and keep holding until the fastboot screen shows. Connect the Show to your computer.
3. From the extracted folder, run `./fastbrick.sh` on Linux and macOS. On Windows, run `fastbrick.bat`.

`fastbrick` rewrites the Show's bootloader, so never unplug the Show while it runs. If it says the bootloader is already unlocked, the Show is on amonet 2 already. On the Echo Show 5 it writes TWRP itself and restarts into it, so the package's `fastboot-step.sh` is not needed. If the Show is in TWRP when `fastbrick` ends, skip that step.

### Updating from amonet 1

A Show unlocked earlier with amonet 1 has TWRP 3.2.3, which the image does not support. Run `fastbrick` the same way to update from amonet 1 to amonet 2. The system that was on the Show does not boot on amonet 2, so install the image right after.

## Echo Show 8 (1st gen)

Use [R0rt1z2](https://github.com/R0rt1z2)'s amonet 2 package for the Echo Show 8 (1st gen), linked from R0rt1z2's [XDA thread](https://xdaforums.com/t/unlock-root-twrp-unbrick-amazon-echo-show-8-1st-gen-2019-crown.4766687/). It unlocks the Show over USB the same way as on the Echo Show 5 and writes TWRP 3.7.0_9-0, which is what the image needs.

### What you need

- The Echo Show 8 (1st gen), its power adapter and a USB cable that carries data.
- A computer with Linux, macOS or Windows. The package brings its own `fastboot`.

### Run fastbrick

1. Download R0rt1z2's package for the Echo Show 8 (1st gen) from the XDA thread and extract it.
2. Put the Show in fastboot mode. Unplug its power, then hold **Volume Down**, **Volume Up** and the **privacy button** together, plug the power back in and keep holding until the fastboot screen shows. Connect the Show to your computer.
3. From the extracted folder, run `./fastbrick.sh` on Linux and macOS. On Windows, run `fastbrick.bat`.

`fastbrick` rewrites the Show's bootloader, so never unplug the Show while it runs. If it says the bootloader is already unlocked, the Show is on amonet 2 already. If the Show is not in TWRP when `fastbrick` ends, follow the next step in the package's instructions on the XDA thread.

### Updating from amonet 1

A Show unlocked earlier with amonet 1 has TWRP 3.2.3, which the image does not support. Run `fastbrick` the same way to update from amonet 1 to amonet 2. The system that was on the Show does not boot on amonet 2, so install the image right after.

## Next

The device is ready for its image. [Getting into TWRP](twrp.md) covers starting TWRP and checking its version. The installation is in the device's guide, such as the [Echo Dot guide](echo-dot.md#installing).
