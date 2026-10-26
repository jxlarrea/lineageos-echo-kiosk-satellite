# Echo Dot (2nd gen)

A LineageOS 18.1 (Android 11) image for the 2nd generation Echo Dot (`biscuit`), tuned to run [Kiosk Satellite](https://github.com/jxlarrea/kiosk-satellite) as a voice satellite, intercom and speaker for Home Assistant. The Dot has no screen, so the image leaves out everything that draws one and runs Kiosk Satellite headless.

It replaces the LineageOS 14.1 image in [lineageos-biscuit-kiosk-satellite](https://github.com/jxlarrea/lineageos-biscuit-kiosk-satellite). Its last release was 2026.10.5. To move a Dot from it, see [Moving from the CM14.1 image](#moving-from-the-cm141-image).

## What the image does

- **Wake words over loud music.** The Dot has seven microphones. The image uses all of them to remove the speaker's own sound from what Kiosk Satellite hears, about 18 to 20 dB less music, so the wake word works with music at full volume.
- **Amazon's wake words.** The image includes the wake word engine the Dot shipped with, with its models for Alexa, Echo, Computer, Amazon and Ziggy in 17 locales. Choose **Amazon** as the wake word engine in Kiosk Satellite. It runs on the Dot and sends nothing to Amazon. A plain "stop" also stops a ringing timer or a reply.
- **A louder microphone and speaker.** The microphone preamp is 36 dB instead of Fire OS's 20, so voices and intercom calls come through at a normal level. The image drives the speaker 3 dB louder than the Fire OS drivers alone, without distortion at full volume.
- **Kiosk Satellite on first boot.** Once the Dot is on Wi-Fi, the latest Kiosk Satellite release is downloaded, installed, given every permission it needs and made the device owner, so its updates install without a confirmation. No adb grants needed.
- **System updates.** BiscuitService finds, downloads and installs new releases of this image. Kiosk Satellite shows them in its remote admin and as a **Firmware** update in Home Assistant.
- **The buttons.** BiscuitService sends Action button presses to Kiosk Satellite as a broadcast, even while the Dot is asleep, with no accessibility service. Kiosk Satellite can take over the volume buttons for a Music Assistant group or a player it follows.
- **The light ring.** The image plays Kiosk Satellite's animations on the light ring without flickering and returns to them after the volume overlay.
- **The line out jack.** With something plugged in, the image plays audio through the jack at full level and keeps the Dot's speaker off. When you unplug it, the image switches audio back to the speaker.
- **Bluetooth both ways.** Phones can play to the Dot as a Bluetooth speaker. The Dot's audio can also go to Bluetooth headphones and speakers. See [Bluetooth](#bluetooth).
- **5 GHz Wi-Fi**, the DFS channels 52 to 144 included, with Wi-Fi power saving off. See [Network](#network).
- **WPA2/WPA3 networks.** The image connects to a network in WPA2/WPA3 transition mode with WPA2, including one that also offers WPA2 with SHA256. On stock LineageOS 18.1, Android tries WPA3 or WPA2 with SHA256 there, which the Dot's Wi-Fi cannot do, so the connection never succeeds.
- **More free memory.** With Kiosk Satellite connected and Amazon's wake word engine listening, about 235 MB of the Dot's 480 MB stays available.

## Before you start

- **An unlocked Dot with TWRP.** The image needs amonet v2 and its TWRP 3.7.0_9-0. Follow [Unlocking the Echo Dot](unlocking.md#echo-dot-2nd-gen), then [Getting into TWRP](twrp.md). A Dot that runs 2026.10.5 already has both.
- **adb** on your computer and a micro USB cable that carries data.
- **Your Wi-Fi name and password.**

## Installing

1. Download the `.zip` file of the newest [Echo Dot release](https://github.com/jxlarrea/lineageos-echo-kiosk-satellite/releases?q=biscuit), the release titled **Echo Dot (2nd gen)** with the highest version.
2. Start TWRP:

   ```sh
   adb reboot recovery
   ```

   Run `adb devices -l` until it lists the Dot as `recovery`, which takes about half a minute. Then check the TWRP version:

   ```sh
   adb shell getprop ro.twrp.version
   ```

   It must print `3.7.0_9-0`. If it prints anything else, stop here and see [Getting into TWRP](twrp.md#checking-the-version).

   Then check the active slot:

   ```sh
   adb shell bcbtool get_active
   ```

   It must print `a`, because the bootloader always starts slot A's kernel and the first copy of the image has to bring it. If it prints `b`, run `adb shell bcbtool set_active a` and `adb reboot recovery`, then check again.

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

4. Check that it boots. The first boot takes about a minute. On the way, its USB connection to your computer drops and comes back a few times, so leave it plugged in for at least three minutes before deciding something is wrong. It has booted once this prints `1`:

   ```sh
   adb shell getprop sys.boot_completed
   ```

   If it keeps restarting, stop here and see [If the Dot keeps restarting](twrp.md#if-the-dot-keeps-restarting).

5. Install the image into the Dot's other slot too, so both slots hold a copy that boots. Start TWRP:

   ```sh
   adb reboot recovery
   ```

   Run `adb devices -l` until it lists the Dot as `recovery`. Switch to the other slot:

   ```sh
   adb shell bcbtool get_active
   adb shell bcbtool set_active b
   adb reboot recovery
   ```

   If `get_active` printed `b`, use `set_active a` instead. Run `adb devices -l` until it lists the Dot as `recovery` again. Check that the switch took, then install. Leave out `twrp wipe data` this time, so Kiosk Satellite and its settings stay:

   ```sh
   adb shell bcbtool get_active
   adb shell twrp install /sdcard/update.zip
   ```

   `get_active` must print the slot you switched to. If it does not or the install prints an error or `assert failed`, do not reboot. Run `adb shell bcbtool set_active` with the slot you started from to go back to the copy that boots, then reboot. Otherwise finish:

   ```sh
   adb shell rm /sdcard/update.zip
   adb reboot
   ```

6. Check that this slot boots too, the same way as in step 4. If it keeps restarting, unplug the Dot, hold the **+** button and plug it back in to start TWRP. Then run `adb shell bcbtool set_active` with the slot you started from and `adb reboot`. That boots the copy that worked.

## After installing

1. **Join Wi-Fi.** The second command shows the Dot's IP address:

   ```sh
   adb shell "biscuit_service wifi connect '<ssid>' '<password>'"
   adb shell ip -4 addr show wlan0
   ```

   Keep the double quotes around the whole command. Without them, `adb shell` splits a Wi-Fi name or password with spaces into separate words before it reaches the Dot.

   If the name or password has a single quote, a backslash or other characters your computer's shell treats specially, run the command from a shell on the Dot instead. There, only the single quotes around each value matter:

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

3. **Set up Kiosk Satellite.** Open `http://<dot ip>:2324` in a browser. In the setup wizard, set an admin password, your Home Assistant connection and the wake word. Then add the Dot in Home Assistant under **Settings > Devices & services**, where Home Assistant lists it as discovered. The [Echo Dot guide](https://github.com/jxlarrea/kiosk-satellite/blob/main/docs/echo-dot.md) covers the buttons, the light ring and the other Kiosk Satellite features on the Dot.

BiscuitService names the Dot `echo-dot-ks` and the last three characters of its serial, `echo-dot-ks-he4` for example, so your router lists it under its own name. Android reports it as an **Amzn Echo Dot (KS)**.

## Moving from the CM14.1 image

A Dot on 2026.10.5 or an earlier CM14.1 release cannot update to this image over the air. The move from Android 7 to Android 11 needs an erased data partition, which an update does not do. Install it by hand. It takes about ten minutes. Kiosk Satellite's settings come back from a backup.

1. **Back up Kiosk Satellite.** In its remote admin, use **Export configuration** and keep the file.
2. **Install the image** with the steps in [Installing](#installing), `twrp wipe data` included. The Dot already has amonet v2 and its TWRP.
3. **Join Wi-Fi and wait for Kiosk Satellite** as in [After installing](#after-installing).
4. **Restore the backup.** In the setup wizard, choose **Restore from configuration file** and pick the file you exported.

Kiosk Satellite makes a new TLS certificate, since the old one cannot leave the Android 7 image. If Home Assistant or another client trusted the old one, it asks to trust the new one once.

Both slots get this image, so 2026.10.5 is no longer on the Dot afterwards. To go back to it, install it from TWRP the same way, with its own steps.

## Updating

BiscuitService installs new releases of this image. Home Assistant shows a new release on the Dot's **Firmware** update entity. Kiosk Satellite shows it under **About > System** in its remote admin and on the Overview. Install it from either place.

BiscuitService downloads the release and checks it against GitHub's checksum and the release signature. Then it has TWRP install it into slot A and, once slot A boots, into slot B. Each install restarts the Dot and takes it offline for about a minute. Kiosk Satellite and its settings stay.

BiscuitService checks for a new release a minute after every boot. Kiosk Satellite asks it to check again twice a day. [Device commands](#device-commands) covers the same from adb.

## Settings

### Voice

To hear only one microphone, for example to compare, turn the speaker removal off and restart the Dot:

```sh
adb shell setprop persist.biscuit.mic_beam 0
```

Kiosk Satellite hears microphone 0. If Kiosk Satellite barely hears the wake word, one of the Dot's microphones may be weak. Stop Kiosk Satellite, since it holds the microphones while it listens. Then print the level of each one over 4 seconds:

```sh
adb shell am force-stop me.jxl.kiosk_satellite
adb shell biscuit_mic_test 4
```

Channels 0 to 6 are the microphones and should read within a few dB of each other. Channels 7 and 8 are the speaker and read 0 when nothing plays. If microphone 0 reads far below the rest, pick another microphone and restart the Dot. The speaker removal keeps working with any of them:

```sh
adb shell setprop persist.biscuit.mic_beam_out 6
```

### Speaker

The image drives the speaker 3 dB above the Fire OS drivers. To change it, set a gain in dB from -6 to 29. It applies within a second, with no restart:

```sh
adb shell setprop persist.biscuit.spk_gain_db 3
```

At 6 dB the speaker distorted at full volume.

The image also has a high-pass filter and a limiter for the speaker, both off. Over loud music the wake word did slightly worse with them on and high notes sounded harsher. To try them, set a cutoff in Hz or turn on the limiter and restart the Dot:

```sh
adb shell setprop persist.biscuit.spk_hpf_hz 150
adb shell setprop persist.biscuit.spk_limiter 1
```

### Bluetooth

Bluetooth is on. Android 11 can either take audio from phones or play to headphones and speakers, not both at once, so BiscuitService sets the Dot up for one or the other. The last choice stays across reboots. A fresh Dot starts out as a speaker.

- **As a Bluetooth speaker.** Put the Dot in pairing mode for five minutes, then pair your phone with **Echo Dot**. BiscuitService confirms the pairing on the Dot's side:

  ```sh
  adb shell biscuit_service bt pair
  ```

- **With Bluetooth headphones or a speaker.** Put them in pairing mode, then run the command below. BiscuitService pairs the Dot with the first ones it finds or with the first whose name has the text you give. Then the Dot's audio plays through them. With a name, ones paired before reconnect without pairing mode:

  ```sh
  adb shell biscuit_service bt connect
  adb shell biscuit_service bt connect '<name>'
  ```

  Without a name, BiscuitService takes any headphones or speaker nearby that is in pairing mode, so give a name where neighbors have Bluetooth audio.

Switching between the two restarts Bluetooth, which drops what was connected. `adb logcat -s BiscuitService` shows how a `bt connect` went.

### Network

- **Wi-Fi power saving is off.** With it on, some messages between kiosks got lost or delayed. Kiosks use those messages to decide which one answers a wake word. The Dot is always plugged in, so it costs little. To turn it back on, run this and restart the Dot:

  ```sh
  adb shell setprop persist.biscuit.wifi_powersave 1
  ```

- **5 GHz.** The image connects to networks on every 5 GHz channel the US allows, the DFS channels 52 to 144 included, since a Dot has no SIM to learn its country from. It uses the 20 MHz primary channel of a wider network.
- **WPA2/WPA3.** The image connects to a network in WPA2/WPA3 transition mode with WPA2. Its Wi-Fi cannot do WPA3, so it cannot join a network that only offers WPA3. Use WPA2 or WPA2/WPA3 for the Dot's network.
- **Protected management frames.** The Dot's Wi-Fi cannot do protected management frames (PMF, 802.11w), so it cannot join a network that requires them. Set PMF to optional or off for the Dot's network. Most routers keep it optional in WPA2/WPA3 transition mode.
- **adb works over Wi-Fi** on port 5555 without a password, so a Dot without a screen can always be reached. Anyone on your network can control the Dot this way, so keep it on a network you trust.
- **adb root is off,** as LineageOS 18.1 ships it. Nothing in this guide needs it: `biscuit_service` and the other commands run from a normal `adb shell`. To use `adb root`, allow it first. LineageOS keeps this switch in Developer options, which the Dot has no screen for, so `biscuit_service` sets it:

  ```sh
  adb shell biscuit_service adb root on
  adb root
  ```

  `adb unroot` goes back to a normal shell. `biscuit_service adb root off` stops `adb root` from working again. A root shell can change anything on the Dot, so turn it off when you are done.

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
adb shell biscuit_service bt pair
adb shell biscuit_service bt connect '<name>'
adb shell biscuit_service bt off
adb shell biscuit_service volume up
adb shell biscuit_service volume down
adb shell biscuit_service volume set '<0..15>'
adb shell biscuit_service mute on|off|toggle
adb shell biscuit_service mic mute|unmute|toggle
adb shell biscuit_service countdown set '<remaining-ms>' '<total-ms>'
adb shell biscuit_service countdown clear
adb shell biscuit_service adb root on|off
adb shell biscuit_service status
```

### System updates

```sh
adb shell biscuit_service update status
adb shell biscuit_service update check
adb shell biscuit_service update install
```

`update status` prints the installed release, the newest one and what the update is doing. `update check` asks GitHub for a newer release right away.

`update install` downloads the newest release to `/cache` and checks it against GitHub's checksum and the release signature. Then it restarts the Dot into TWRP, which installs the release into slot A and starts Android again. Two minutes after Android boots from slot A, BiscuitService does the same for slot B and makes slot A active again. Each slot takes the Dot offline for about a minute. Kiosk Satellite and its settings stay.

If an install fails, `update status` says why. TWRP's side of each install is in `/cache/biscuit-ota/twrp.log`.

## What the image leaves out

A speaker with no screen does not need most of Android. Leaving it out frees memory and storage for Kiosk Satellite:

- **Everything that draws on a screen:** the status bar, lock screen, wallpapers, boot animation and screen color tuning.
- **The web browser engine (WebView).** You cannot sign in to a Wi-Fi network that needs a login page.
- **Phone features.** The Dot cannot act as a Bluetooth headset for calls.
- **Apps a speaker does not use:** calendar, contacts, email, gallery, browsers, camera, music, keyboard, file manager, recorder, the LineageOS updater, weather, text to speech, printing and backup.
