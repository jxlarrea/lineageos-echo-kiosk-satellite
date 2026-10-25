# Unlocking

Every image installs from TWRP, which needs an unlocked bootloader. Each Echo unlocks its own way, with a port of amonet, an exploit of MediaTek's boot chain. This page covers each device this repository has an image for. Once a device is unlocked, [Getting into TWRP](twrp.md) covers starting TWRP. The device's guide covers the installation.

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

## Echo Show 8 (1st gen) and Echo Show 5 (2nd gen)

Their images are not ready yet. This section comes with them.

## Next

The device is ready for its image. [Getting into TWRP](twrp.md) covers starting TWRP and checking its version. The installation is in the device's guide, such as the [Echo Dot guide](echo-dot.md#installing).
