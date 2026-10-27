# Getting into TWRP

Every image installs from the TWRP that comes with the device's amonet. [Unlocking](unlocking.md) installs both. This page covers the Echo Dot (2nd gen), the Echo Show 5 (2nd gen) and the Echo Show 8 (1st gen).

## Echo Dot (2nd gen)

The image installs from TWRP 3.7.0_9-0, the recovery that comes with amonet v2.

### Starting TWRP

If the Dot is running, connect it over USB and run:

```sh
adb reboot recovery
```

Within about half a minute, `adb devices -l` lists the Dot as `recovery`. Check with `adb devices -l` rather than `adb wait-for-recovery`, which can hang on the Dot.

If the Dot does not boot, unplug it, hold the **+** button and plug it back in. That starts TWRP, since amonet v2 has no fastboot mode. If `adb devices -l` does not list TWRP either, see [If the Dot keeps restarting](#if-the-dot-keeps-restarting).

TWRP has no screen on the Dot. Everything runs over `adb shell`. The `twrp` command does what its buttons would on a phone, such as `twrp install` and `twrp wipe`.

### Checking the version

```sh
adb shell getprop ro.twrp.version
```

It should print `3.7.0_9-0`. If it prints `3.2.3` or `3.7.0_9-bboe2`, the Dot is on amonet v1, which the image does not support. [A Dot on amonet v1](unlocking.md#a-dot-on-amonet-v1) covers the update.

### The two slots

The Dot has two slots, A and B. The [installation](echo-dot.md#installing) installs the image into both. The bootloader always starts slot A's kernel, whichever slot is active. The active slot only picks the system partition the image runs from. So slot A must always hold the image. A Dot whose slot A holds something else does not boot from slot B either. These commands show and switch the active slot from TWRP:

```sh
adb shell bcbtool get_active
adb shell bcbtool set_active b
```

After switching, reboot into TWRP and run `get_active` again before installing. The installer checks that TWRP points at the active slot and stops with `assert failed` if it does not.

### If the Dot keeps restarting

The image never writes TWRP, the bootloader or the partition table, so a Dot that keeps restarting after an install can almost always be recovered. Your computer sees each restart as a USB device that connects and then disconnects.

1. **Wait.** The first boot takes about a minute. The USB connection drops and comes back a few times along the way. Give it three minutes.
2. **Check the power.** The Dot runs on the micro USB cable. TWRP draws little, but Android starts Wi-Fi, audio and all four cores. A weak computer port or unpowered hub may not keep up. Plug the Dot into its own power adapter. If it then stops restarting, use a stronger port or a powered hub for adb.
3. **Start TWRP with the + button.** Unplug the Dot, hold the **+** button and plug it back in. Within about half a minute, `adb devices -l` lists the Dot as `recovery`. Both slots hold the image after the [installation](echo-dot.md#installing), so switching slots runs the other copy of the system partition with slot A's kernel. Run `adb shell bcbtool get_active`, then `adb shell bcbtool set_active` with the other slot and `adb reboot`. If that does not help either, switch to slot A, reboot into TWRP and install again from step 2 of the installation.
4. **Catch adb during a restart.** If `adb devices -l` lists the Dot for a moment during each cycle, Android is starting and then failing. Run `adb reboot recovery` while it is listed to get back to TWRP.
5. **Use the Preloader's download mode.** If `adb devices -l` never lists TWRP, unplug the Dot, hold only the **mute** button and plug it back in. Your computer then sees a `PWNED MT8163 Preloader`. amonet v2 uses this mode to repair a Dot that no longer boots. Follow the Important Notes in the [amonet thread on XDA](https://xdaforums.com/t/unlock-root-twrp-unbrick-amazon-echo-dot-2nd-gen-2016-biscuit.4761416/) for the rest of the procedure. If the Dot shows no light at all, see the test point section of firebreak's [README](https://github.com/bboe/firebreak#run-it).

If you ask for help, include what both `twrp install` runs and `bcbtool get_active` printed. Say whether adb ever shows the Dot during a restart.

## Echo Show 5 (2nd gen)

The image installs from TWRP 3.7.0_9-0, the recovery that comes with amonet 2.

### Starting TWRP

If the Show is running, connect it over USB and run:

```sh
adb reboot recovery
```

Within about half a minute, `adb devices -l` lists the Show as `recovery`. TWRP has a touch screen on the Show, but the guide runs everything over `adb shell`. The `twrp` command does what its buttons do, such as `twrp install` and `twrp wipe`.

If the Show does not boot, R0rt1z2's amonet package has a `boot-recovery.sh` that asks the bootloader over USB to start TWRP. R0rt1z2's [XDA thread](https://xdaforums.com/t/rom-unofficial-11-cronos-lineageos-18-1-for-the-amazon-echo-show-5-2021.4772598/) covers it.

### Checking the version

```sh
adb shell getprop ro.twrp.version
```

It must print `3.7.0_9-0`. A Show that prints `3.2.3-0` is still on amonet 1. [Updating from amonet 1](unlocking.md#updating-from-amonet-1) covers the update.

### One slot

The Show has one copy of each partition, with no slots. An install replaces the system that was there, so if it fails, only TWRP starts until an install succeeds. TWRP's `wipe data` keeps the internal storage in `/sdcard`.

## Echo Show 8 (1st gen)

The image installs from TWRP 3.7.0_9-0, the recovery that comes with amonet 2.

### Starting TWRP

If the Show is running, connect it over USB and run:

```sh
adb reboot recovery
```

Within about half a minute, `adb devices -l` lists the Show as `recovery`. TWRP has a touch screen on the Show, but the guide runs everything over `adb shell`. The `twrp` command does what its buttons do, such as `twrp install` and `twrp wipe`. TWRP has no adb over Wi-Fi, so connect the Show over USB even if you use adb over Wi-Fi in Android.

If the Show does not boot, R0rt1z2's [XDA thread](https://xdaforums.com/t/unlock-root-twrp-unbrick-amazon-echo-show-8-1st-gen-2019-crown.4766687/) covers starting TWRP and unbricking it over USB.

### Checking the version

```sh
adb shell getprop ro.twrp.version
```

It must print `3.7.0_9-0`. A Show that prints `3.2.3-0` is still on amonet 1. [Updating from amonet 1](unlocking.md#updating-from-amonet-1-1) covers the update.

### One slot

The Show has one copy of each partition, with no slots. An install replaces the system that was there, so if it fails, only TWRP starts until an install succeeds. TWRP's `wipe data` keeps the internal storage in `/sdcard`.
