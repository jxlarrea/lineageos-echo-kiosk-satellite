# Echo Dot (3rd gen)

A LineageOS 18.1 (Android 11) image for the 3rd generation Echo Dot (`donut`), tuned to run [Kiosk Satellite](https://github.com/jxlarrea/kiosk-satellite) as a voice satellite, intercom and speaker for Home Assistant. The Dot has no screen, so the image leaves out everything that draws one and runs Kiosk Satellite headless.

It is [jxlarrea](https://github.com/jxlarrea)'s own port, with its own device tree and a kernel built from Amazon's source release in [android_kernel_amazon_mt8167](https://github.com/jxlarrea/android_kernel_amazon_mt8167).

> [!CAUTION]
> The image is only for the Echo Dot (3rd gen) released in 2018, model D9N29T, codename `donut`. Amazon also sold a revised Echo Dot (3rd gen) in 2019 and 2020, model C78MP8, codename `crumpet`. It has different hardware and neither the [unlock](unlocking.md#echo-dot-3rd-gen) nor the image works on it. Check the model number on the bottom of the Dot first.

## What the image does

- **A raw microphone for Kiosk Satellite.** Kiosk Satellite hears one of the four microphones as captured, with nothing removed from it. Its own echo canceller then sees the speaker's echo at a fixed delay. See [Voice](#voice).
- **Its own audio driver layer.** The image drives the speaker, the microphones and the line out jack with an audio HAL written for this Dot. It uses none of Fire OS's audio libraries.
- **Kiosk Satellite on first boot.** Once the Dot is on Wi-Fi, the latest Kiosk Satellite release is downloaded, installed, given every permission it needs and made the device owner, so its updates install without a confirmation. No adb grants needed.
- **System updates.** BiscuitService finds, downloads and installs new releases of this image. Kiosk Satellite shows them in its remote admin and as a **Firmware** update in Home Assistant.
- **Functional buttons.** BiscuitService sends Action button presses to Kiosk Satellite as a broadcast, with no accessibility service. Kiosk Satellite can take over the volume buttons for a Music Assistant group or a player it follows.
- **A privacy button that cuts the microphones.** The button is a hardware switch. BiscuitService follows it, so Kiosk Satellite always shows the real state. See [Privacy button](#privacy-button).
- **Functional light ring.** The image plays Kiosk Satellite's animations on the light ring and returns to them after the volume overlay.
- **Functional line out jack.** With something plugged in, the image plays audio through the jack and keeps the Dot's speaker off. When you unplug it, the image switches audio back to the speaker.
- **A light sensor.** Kiosk Satellite gets the room's light level in lux.
- **Steady Wi-Fi.** Android keeps the access point it joined instead of moving between mesh nodes or scanning while connected, with Wi-Fi power saving off. See [Network](#network).
- **More free memory.** With Kiosk Satellite connected, about 235 MB of the Dot's 490 MB stays available.

## Before you start

- **An unlocked Dot with TWRP.** The image needs the unlock in [Unlocking the Echo Dot (3rd gen)](unlocking.md#echo-dot-3rd-gen) and its TWRP 3.7.0_9-0. [Getting into TWRP](twrp.md#echo-dot-3rd-gen) covers starting TWRP.
- **adb** on your computer and a USB connection to the Dot. The Dot has no USB data port on the outside, so this is the connection the unlock leaves you with.
- **Your Wi-Fi name and password.**

## Installing

1. Download the `.zip` file of the newest [Echo Dot (3rd gen) release](https://github.com/jxlarrea/lineageos-echo-kiosk-satellite/releases?q=donut), the release titled **Echo Dot (3rd gen)** with the highest version.
2. Start TWRP:

   ```sh
   adb reboot recovery
   ```

   Run `adb devices -l` until it lists the Dot as `recovery`, which takes about half a minute. Then check the TWRP version:

   ```sh
   adb shell getprop ro.twrp.version
   ```

   It must print `3.7.0_9-0`. If it prints anything else, stop here.

   Then check the active slot:

   ```sh
   adb shell bcbtool get_active
   ```

   Remember which slot it prints. The steps below install into that slot first.

3. Erase the Dot and install the image. Replace `<image>.zip` with the file you downloaded:

   ```sh
   adb shell twrp wipe cache
   adb shell twrp wipe data
   adb push <image>.zip /sdcard/update.zip
   adb shell twrp install /sdcard/update.zip
   ```

   If the install prints an error or `assert failed`, stop here and do not reboot. TWRP is still running, so nothing is lost yet. Otherwise reboot:

   ```sh
   adb reboot
   ```

4. Check that it boots. Run `adb devices -l` after about half a minute.

   If it lists the Dot as `recovery` again, run `adb reboot` once more. After a stay in TWRP, the Dot's bootloader often starts TWRP a second time before it starts Android. See [Leaving TWRP](twrp.md#leaving-twrp).

   The first boot takes about a minute. It has booted once this prints `1`:

   ```sh
   adb shell getprop sys.boot_completed
   ```

5. Install the image into the Dot's other slot too, so both slots hold a copy that boots. Start TWRP:

   ```sh
   adb reboot recovery
   ```

   Run `adb devices -l` until it lists the Dot as `recovery`. Switch to the other slot. If `get_active` printed `b` in step 2, use `set_active a` instead:

   ```sh
   adb shell bcbtool set_active b
   adb reboot recovery
   ```

   Run `adb devices -l` until it lists the Dot as `recovery` again. Check that the switch took, then install. Leave out `twrp wipe data` this time, so Kiosk Satellite and its settings stay:

   ```sh
   adb shell bcbtool get_active
   adb shell twrp install /sdcard/update.zip
   ```

   `get_active` must print the slot you switched to. If it does not or the install prints an error or `assert failed`, do not reboot. Run `adb shell bcbtool set_active` with the slot you started from to go back to the copy that boots, then reboot. Otherwise finish:

   ```sh
   adb shell rm /sdcard/update.zip
   adb reboot
   ```

6. Check that this slot boots too, the same way as in step 4. The bootloader starts the active slot, so Android now runs from the slot you just installed into. Either slot works from here on.

## After installing

1. **Join Wi-Fi.** The second command shows the Dot's IP address:

   ```sh
   adb shell "biscuit_service wifi connect '<ssid>' '<password>'"
   adb shell ip -4 addr show wlan0
   ```

   Keep the double quotes around the whole command. Without them, `adb shell` splits a Wi-Fi name or password with spaces into separate words before it reaches the Dot.

   If the name or password has a `!`, a `$`, a single quote, a backslash or other characters your computer's shell treats specially, run the command from a shell on the Dot instead. On the Dot, only the single quotes around each value matter:

   ```sh
   adb shell
   biscuit_service wifi connect '<ssid>' '<password>'
   exit
   ```

   If the password itself has a single quote, end the quoted part, add `\'` and start it again: `'it'\''s a password'`. If the connection still fails, `adb logcat -d -s BiscuitService wpa_supplicant` shows why.

2. **Wait for Kiosk Satellite.** Once the Dot is on Wi-Fi, Kiosk Satellite is downloaded and installed automatically. Follow the progress with:

   ```sh
   adb logcat -s ks-install
   ```

   It prints `downloading`, `installed Kiosk Satellite` and `Kiosk Satellite is the device owner`. This takes a couple of minutes.

3. **Set up Kiosk Satellite.** Open `http://<dot ip>:2324` in a browser. In the setup wizard, set an admin password, your Home Assistant connection and the wake word. Then add the Dot in Home Assistant under **Settings > Devices & services**, where Home Assistant lists it as discovered.

Android reports the Dot as an **Echo Dot 3rd Gen (KS)**.

## Updating

BiscuitService installs new releases of this image. Home Assistant shows a new release on the Dot's **Firmware** update entity. Kiosk Satellite shows it under **About > System** in its remote admin and on the Overview. Install it from either place.

BiscuitService downloads the release and checks it against GitHub's checksum and the release signature. Then it has TWRP install it into slot A and, once slot A boots, into slot B. Each install restarts the Dot and takes it offline for a couple of minutes. Kiosk Satellite and its settings stay.

BiscuitService checks for a new release a minute after every boot. Kiosk Satellite asks it to check again twice a day. [Updates from your own server](offline-updates.md) covers a Dot on a network without Internet access. [Device commands](#device-commands) covers the same from adb.

An update never writes the partitions that hold TWRP or the bootloader. It writes the system and boot partitions of one slot at a time.

## Settings

### Voice

Kiosk Satellite hears microphone 0 as captured. The Dot has four microphones, numbered 0 to 3. To hand Kiosk Satellite another one, set it and restart the Dot:

```sh
adb shell setprop persist.donut.mic 2
```

### Privacy button

The privacy button is a hardware switch. Pressing it cuts all four microphones and lights the button's own LED, with or without Android. BiscuitService sets Android's microphone mute to match, so Kiosk Satellite and Home Assistant show the real state.

While privacy is on, nothing unmutes the microphones but the button. Kiosk Satellite, Home Assistant and `biscuit_service mute off` can mute them, but they cannot unmute them until you press the button again.

### Mute timeout

To keep the microphones muted unless they were just unmuted, set a timeout. Once the microphones have been unmuted for that many seconds, BiscuitService mutes them again, however they were unmuted. This one mutes them again after 2 minutes:

```sh
adb shell biscuit_service mute timeout 120
```

`biscuit_service status` shows how long until the next mute. `biscuit_service mute timeout off` stops it. The timeout stays across restarts and updates. Its countdown starts again at every boot. The countdown does not wait for the assistant, so it can cut off a conversation that runs past it.

### Line out

Plug a speaker or an amplifier into the Dot's jack and the image plays through it within half a second, with the Dot's own speaker off. Unplug it and the Dot's speaker plays again. Nothing needs to be set.

### Time

- **Network time.** Android sets the clock over NTP from `time.android.com` at every boot and once a day after that. To use another NTP server, such as your router or a time server on your network, set its name or IP address:

  ```sh
  adb shell settings put global ntp_server 192.168.1.1
  ```

  Wait a few seconds, then restart the Dot with `adb reboot`. Android saves the setting a moment after it changes, so a restart right away can lose it. To go back to `time.android.com`, run:

  ```sh
  adb shell settings delete global ntp_server
  ```

- **Time zone.** The Dot starts on GMT, since it has no mobile network to learn the time zone from. Kiosk Satellite's clock times, such as its alarms, follow it. Turn off automatic time zone, which has nothing to go by on the Dot. Then set the zone with a name from the [tz database](https://en.wikipedia.org/wiki/List_of_tz_database_time_zones):

  ```sh
  adb shell settings put global auto_time_zone 0
  adb shell service call alarm 3 s16 'America/New_York'
  ```

  It prints `Result: Parcel(00000000    '....')` when the zone is set. `adb shell date` then shows the time in the new zone.

### Network

- **Wi-Fi power saving is off.** With it on, some messages between kiosks got lost or delayed on the Echo Dot (2nd gen). Kiosks use those messages to decide which one answers a wake word. The Dot is always plugged in, so it costs little. To turn it back on, run this and restart the Dot:

  ```sh
  adb shell setprop persist.biscuit.wifi_powersave 1
  ```

- **adb works over Wi-Fi** on port 5555 without a password, so a Dot without a screen can always be reached. Anyone on your network can control the Dot this way, so keep it on a network you trust.

### Kiosk Satellite installer

On every boot the image makes sure Kiosk Satellite is installed and has its permissions. To run the Dot without Kiosk Satellite, turn that off:

```sh
adb shell setprop persist.biscuit.ks_install 0
```

## Device commands

`biscuit_service` controls the Dot's Wi-Fi, Bluetooth, volume, microphone and light ring from `adb shell`. Kiosk Satellite handles most of this once it runs, so these are mainly for setup and troubleshooting.

```sh
adb shell biscuit_service wifi on
adb shell "biscuit_service wifi connect '<ssid>' '<password>'"
adb shell biscuit_service wifi off
adb shell biscuit_service bt on|off
adb shell biscuit_service volume up
adb shell biscuit_service volume down
adb shell biscuit_service volume set '<0..15>'
adb shell biscuit_service mute on|off|toggle
adb shell biscuit_service mute timeout '<seconds>'|off
adb shell biscuit_service countdown set '<remaining-ms>' '<total-ms>'
adb shell biscuit_service countdown clear
adb shell biscuit_service status
```

### System updates

```sh
adb shell biscuit_service update status
adb shell biscuit_service update check
adb shell biscuit_service update install
```

`update status` prints the installed release, the newest one and what the update is doing. `update check` asks GitHub for a newer release right away.

`update install` downloads the newest release to `/data/biscuit-ota` and checks it against GitHub's checksum and the release signature. Then it restarts the Dot into TWRP, which installs the release into slot A and starts Android again. Two minutes after Android boots from slot A, BiscuitService does the same for slot B and makes slot A active again. Kiosk Satellite and its settings stay.

If an install fails, `update status` says why. TWRP's side of each install is in `/data/biscuit-ota/twrp.log`.

## What the image leaves out

A speaker with no screen does not need most of Android. Leaving it out frees memory and storage for Kiosk Satellite:

- **Everything that draws on a screen:** the status bar, lock screen, wallpapers, boot animation and screen color tuning.
- **The web browser engine (WebView).** You cannot sign in to a Wi-Fi network that needs a login page.
- **Phone features.** The Dot cannot act as a Bluetooth headset for calls.
- **Apps a speaker does not use:** calendar, contacts, email, gallery, browsers, camera, music, keyboard, file manager, recorder, the LineageOS updater, weather, text to speech, printing and backup.
