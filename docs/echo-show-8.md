# Echo Show 8 (1st gen)

A LineageOS 18.1 (Android 11) image for the 1st generation Echo Show 8 (`crown`), tuned to run [Kiosk Satellite](https://github.com/jxlarrea/kiosk-satellite) on its screen for Home Assistant. It is [R0rt1z2](https://github.com/R0rt1z2)'s LineageOS 18.1 port for the Echo Show 8 in [amazon-oss](https://github.com/amazon-oss), which brings the screen, camera and Bluetooth, with the Kiosk Satellite Edition layer on top.

To move a Show from R0rt1z2's own image, see [Moving from R0rt1z2's image](#moving-from-r0rt1z2s-image).

## What the image does

- **Amazon's far-field microphone processing for Kiosk Satellite.** Kiosk Satellite hears the Show through the processing Amazon's own wake word used on Fire OS: echo cancellation and a beamformer over all four microphones, with the Show's own copy of the speaker signal. Wake word detection holds up over loud music and over the assistant's own voice. See [Volume](#volume) for the levels that work best.
- **Amazon's wake words.** The image includes Amazon's wake word engine with its models for Alexa, Echo, Computer, Amazon and Ziggy in 17 locales, the same as the Echo Dot image. Choose **Amazon** as the wake word engine in Kiosk Satellite. It runs on the Show and sends nothing to Amazon. Kiosk Satellite 2026.10.41 and later offer it on the Show.
- **Kiosk Satellite on first boot.** Once the Show is on Wi-Fi, the latest Kiosk Satellite release is downloaded, installed, given every permission it needs and made the device owner, so its updates install without a confirmation. Kiosk Satellite then makes itself the home launcher and reports the Show's real Wi-Fi MAC address to Home Assistant.
- **No on-screen onboarding.** There is no setup wizard and no lock screen. adb works over USB from the first boot, so a computer only has to be allowed on the screen once. Allowed computers do not expire. Android's screen timeout is set to never, so only Kiosk Satellite dims the screen or turns it off.
- **Set up from adb.** `echo_service` joins Wi-Fi, sets the time and time zone from your computer and changes the Show's settings without touching the screen. See [Device commands](#device-commands).
- **Privacy and the screen on separate presses.** Press the button briefly to turn the screen on or off. Hold it for about a second and a half to turn privacy on or off. See [The button](#the-button).
- **Bluetooth both ways.** Phones can play to the Show as a Bluetooth speaker. The Show's audio can also go to Bluetooth headphones and speakers. See [Bluetooth](#bluetooth).
- **System updates.** EchoService finds, downloads and installs new releases of this image. Kiosk Satellite shows them in its remote admin and as a **Firmware** update in Home Assistant.
- **Steady Wi-Fi.** Android keeps the access point it joined instead of moving between mesh nodes or scanning while connected. It picks one by signal strength, preferring 5 GHz. The image also connects to a network in WPA2/WPA3 transition mode with WPA2, including one that also offers WPA2 with SHA256.
- **More free memory.** With Kiosk Satellite on screen for over an hour, about 1.1 GB of the Show's 2 GB stays available.

## Before you start

- **An unlocked Show with TWRP.** The image needs amonet 2 and its TWRP 3.7.0_9-0. Follow [Unlocking the Echo Show 8](unlocking.md#echo-show-8-1st-gen), then [Getting into TWRP](twrp.md#echo-show-8-1st-gen).
- **adb** on your computer and a USB cable that carries data.
- **Your Wi-Fi name and password.**

## Installing

1. Download the `.zip` file of the newest [Echo Show 8 release](https://github.com/jxlarrea/lineageos-echo-kiosk-satellite/releases?q=crown), the release titled **Echo Show 8 (1st gen)** with the highest version.
2. Start TWRP:

   ```sh
   adb reboot recovery
   ```

   Run `adb devices -l` until it lists the Show as `recovery`. Then check the TWRP version:

   ```sh
   adb shell getprop ro.twrp.version
   ```

   It must print `3.7.0_9-0`. If it prints anything else, stop here and see [Getting into TWRP](twrp.md#echo-show-8-1st-gen).

3. Erase the Show and install the image. Replace `<image>.zip` with the file you downloaded:

   ```sh
   adb shell twrp wipe cache
   adb shell twrp wipe data
   adb push <image>.zip /sdcard/update.zip
   adb shell twrp install /sdcard/update.zip
   ```

   The zip goes to `/sdcard`, the Show's internal storage, because it is larger than the memory TWRP keeps `/tmp` in. `twrp wipe data` leaves `/sdcard` as it is.

   If the install prints an error or `assert failed`, stop here and do not reboot. TWRP is still running, so nothing is lost yet. Otherwise remove the zip and reboot:

   ```sh
   adb shell rm /sdcard/update.zip
   adb reboot
   ```

4. **Allow your computer.** The first boot takes about a minute and a half. Then Android asks on the screen whether to allow USB debugging. Check **Always allow from this computer** and tap **Allow**. It has booted once this prints `1`:

   ```sh
   adb shell getprop sys.boot_completed
   ```

## After installing

1. **Set the time and time zone** from your computer. Pick the line for your computer:

   ```sh
   # Linux
   adb shell echo_service time sync $(date +%s) $(timedatectl show -p Timezone --value)
   # macOS
   adb shell echo_service time sync $(date +%s) $(readlink /etc/localtime | sed 's#.*zoneinfo/##')
   ```

   On Windows, in PowerShell:

   ```powershell
   adb shell echo_service time sync ([DateTimeOffset]::UtcNow.ToUnixTimeSeconds()) "'$((Get-TimeZone).Id)'"
   ```

   Once the Show is online, Android keeps the time from the network.

2. **Join Wi-Fi.** The second command shows the Show's IP address:

   ```sh
   adb shell "echo_service wifi connect '<ssid>' '<password>'"
   adb shell echo_service wifi
   ```

   Keep the double quotes around the whole command. Without them, `adb shell` splits a Wi-Fi name or password with spaces into separate words before it reaches the Show. If the name or password has a single quote, a backslash or other characters your computer's shell treats specially, run the command from a shell on the Show instead, as the [Echo Dot guide](echo-dot.md#after-installing) shows for the Dot.

   If your router offers separate 2.4 GHz and 5 GHz networks, join the 5 GHz one. In testing, the Show answered about four times faster on 5 GHz than on 2.4 GHz from the same router.

3. **Wait for Kiosk Satellite.** Once the Show is on Wi-Fi, Kiosk Satellite is downloaded and installed automatically. Follow the progress with:

   ```sh
   adb logcat -s ks-install
   ```

   It prints `downloading`, `installed Kiosk Satellite`, `Kiosk Satellite is the device owner` and `permissions granted`. This takes a couple of minutes. Kiosk Satellite then opens on the screen.

4. **Set up Kiosk Satellite.** Go through its setup wizard on the screen or at `http://<show ip>:2324` in a browser: an admin password, your Home Assistant connection and the wake word. Then add the Show in Home Assistant under **Settings > Devices & services**, where Home Assistant lists it as discovered.

EchoService names the Show `echo-show-8-ks` and the last three characters of its serial, `echo-show-8-ks-62e` for example, so your router lists it under its own name. Android reports it as an **Echo Show 8 1st Gen (KS)**.

## Moving from R0rt1z2's image

To move a Show from R0rt1z2's LineageOS 18.1 image to this one, install it by hand. Step 2 of [Installing](#installing) checks the TWRP version.

1. **Back up Kiosk Satellite.** In its remote admin, use **Export configuration** and keep the file.
2. **Install the image** with the steps in [Installing](#installing), `twrp wipe data` included.
3. **Set the time, join Wi-Fi and wait for Kiosk Satellite** as in [After installing](#after-installing).
4. **Restore the backup.** In the setup wizard, choose **Restore from configuration file** and pick the file you exported.

A Show that was unlocked with amonet 1 needs amonet 2 first. [Updating from amonet 1](unlocking.md#updating-from-amonet-1-1) covers it.

## Updating

EchoService installs new releases of this image. Home Assistant shows a new release on the Show's **Firmware** update entity. Kiosk Satellite shows it under **About > System** in its remote admin and on the Overview. Install it from either place.

EchoService downloads the release and checks it against GitHub's checksum and the release signature. Then it restarts the Show into TWRP, which installs the release and starts Android again. The Show has one copy of the system, so this takes it offline for a few minutes, once. Kiosk Satellite and its settings stay.

EchoService checks for a new release a minute after every boot. Kiosk Satellite asks it to check again twice a day. [System updates](#system-updates) covers the same from adb.

## Settings

### Volume

Keep the Show's media volume at 50% and Kiosk Satellite's media player and assistant volumes at 100%. The Show 8's speakers are much louder than the Echo Show 5's. 50% already fills a room. Above it, music drowns out a voice from across the room and the wake word misses more often. A fresh install sets these levels: media volume 8 of 15, which Kiosk Satellite shows as 50%, with Kiosk Satellite's own volumes at their defaults.

```sh
adb shell echo_service volume
adb shell echo_service volume music 8
```

To check that all four microphones work:

1. Turn off **Enable Voice Satellite** in Kiosk Satellite's Voice Satellite settings, since Kiosk Satellite holds the microphones while it listens.
2. The test needs `adb root` to open the microphones. Allow it and print the level of each channel over 4 seconds:

   ```sh
   adb shell echo_service adb root on
   adb root
   adb shell echo_mic_test 4
   ```

3. Turn **Enable Voice Satellite** back on and switch root off again:

   ```sh
   adb unroot
   adb shell echo_service adb root off
   ```

Channels 0 to 3 are the microphones and should read within a few dB of each other. Channels 4 and 5 are the speaker and read very low when nothing plays.

### The button

The Show's power button is also its privacy button. Whatever Android does, the privacy chip turns privacy on when the button is released after a hold of about a second and a half. Any press turns privacy off again. So the image keeps the two apart:

- **A short press** turns the screen on and off.
- **Holding it for about a second and a half** turns privacy on and off. The red light shows when it is on. Android does nothing on a hold.
- **While privacy is on,** a press only turns privacy off and leaves the screen as it is.

`echo_service privacy` says whether privacy is on. To get Android's usual button back, where a short press turns the screen off and holding it opens the power menu, run the first command. The second goes back:

```sh
adb shell echo_service button menu
adb shell echo_service button press
```

With `menu`, a hold still turns privacy on or off as well.

### Screen

- **The screen timeout is never,** so only Kiosk Satellite dims the screen or turns it off. `echo_service screen timeout <seconds>` sets one.
- **No clock on a sleeping screen.** LineageOS's ambient display is off, so the screen shows nothing while it is off. `echo_service screen ambient on` brings the clock back.
- **No lock screen.** `echo_service lockscreen on` turns the swipe lock screen on.

### Bluetooth

Bluetooth is on. Android 11 can either take audio from phones or play to headphones and speakers, not both at once, so EchoService sets the Show up for one or the other. The last choice stays across reboots. A fresh Show takes audio from phones.

- **As a Bluetooth speaker.** Make the Show discoverable for five minutes, then pair your phone with **Echo Show 8** and accept the pairing on the Show's screen:

  ```sh
  adb shell echo_service bt pair
  ```

- **With Bluetooth headphones or a speaker.** Put them in pairing mode, then run the command below. EchoService pairs the Show with the first ones it finds or with the first whose name has the text you give. Then the Show's audio plays through them. With a name, ones paired before reconnect without pairing mode:

  ```sh
  adb shell echo_service bt connect
  adb shell echo_service bt connect '<name>'
  ```

  Without a name, EchoService takes any headphones or speaker nearby that is in pairing mode, so give a name where neighbors have Bluetooth audio.

Switching between the two restarts Bluetooth, which drops what was connected. `adb logcat -s EchoBluetooth` shows how a `bt connect` went. Kiosk Satellite's Bluetooth proxy for Home Assistant keeps scanning in both.

### Network

- **adb works over Wi-Fi** on port 5555, as on R0rt1z2's image. A computer still has to be allowed on the Show's screen once before it can connect. To turn it off, run the first command. The second turns it back on:

  ```sh
  adb shell echo_service adb wifi off
  adb shell echo_service adb wifi on
  ```

- **Allowed computers do not expire.** Android otherwise forgets a computer after 7 days without a connection, which would need a tap on the screen again. `echo_service adb keys expire on` brings the 7 days back.
- **adb root is off,** as LineageOS 18.1 ships it. To use `adb root`, allow it first:

  ```sh
  adb shell echo_service adb root on
  adb root
  ```

  `adb unroot` goes back to a normal shell. `echo_service adb root off` stops `adb root` from working again. A root shell can change anything on the Show, so turn it off when you are done.

### Kiosk Satellite installer

On every boot the image makes sure Kiosk Satellite is installed and has its permissions. To run the Show without Kiosk Satellite, turn that off:

```sh
adb shell setprop persist.echo.ks_install 0
```

## Device commands

`echo_service` sets up and controls the Show from `adb shell`. Kiosk Satellite handles most of this once it runs, so these are mainly for setup and troubleshooting. `echo_service` alone prints every command.

```sh
adb shell echo_service status
adb shell "echo_service wifi connect '<ssid>' '<password>'"
adb shell echo_service wifi on|off
adb shell echo_service bt pair
adb shell echo_service bt connect '<name>'
adb shell echo_service bt on|off
adb shell echo_service time sync '<seconds>' '<zone>'
adb shell echo_service time zone '<zone>'
adb shell echo_service time auto on|off
adb shell echo_service adb wifi on|off
adb shell echo_service adb root on|off
adb shell echo_service adb keys expire on|off
adb shell echo_service lockscreen on|off
adb shell echo_service privacy
adb shell echo_service button press|menu
adb shell echo_service locale '<tag>'
adb shell echo_service screen on|off
adb shell echo_service screen brightness '<0..255>'|auto
adb shell echo_service screen timeout '<seconds>'|never
adb shell echo_service screen stay-awake on|off
adb shell echo_service screen ambient on|off
adb shell echo_service volume '<stream>' '<level>'
adb shell echo_service hostname '<name>'
adb shell echo_service ks status|install|update|restart
adb shell echo_service logs
```

`echo_service logs` packs the Show's logs into one file to attach to an issue and prints the `adb pull` command that copies it to your computer. Run `adb root` first to include the kernel's messages.

### System updates

```sh
adb shell echo_service update status
adb shell echo_service update check
adb shell echo_service update install
```

`update status` prints the installed release, the newest one and what the update is doing. `update check` asks GitHub for a newer release right away.

`update install` downloads the newest release to `/data` and checks it against GitHub's checksum and the release signature. Then it restarts the Show into TWRP, which installs the release and starts Android again. Kiosk Satellite and its settings stay. If the install fails while TWRP writes the system, TWRP stays on screen instead of starting a system that may not boot. Install the release by hand from there with the steps in [Installing](#installing), leaving out `twrp wipe data`.

If an install fails, `update status` says why. TWRP's side of the install is in `/data/system/echo-ota/twrp.log`.

## What the image leaves out

The image is a debloated LineageOS installation, which leaves more free memory for Kiosk Satellite.

Bloat removed:

- **Phone features:** telephony, contacts and the Bluetooth profiles for phone books, messages, file transfer and tethering.
- **Apps a kiosk does not use:** calendar, contacts, the clock, music, the gallery, the calculator, the browser, the camera app, the recorder, the LineageOS updater, AudioFX, printing, backup and the wallet.
- **Background services:** the notification assistant, autofill, system tracing and the metrics, incident, storage statistics and credential services.
