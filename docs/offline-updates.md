# Updates from your own server

EchoService on the Echo Shows and BiscuitService on the Echo Dot check GitHub for new releases of the image. An Echo on a network without Internet access, such as a VLAN that only reaches Home Assistant, cannot reach GitHub. Point it at a folder on your own web server instead, such as a NAS. EchoService and BiscuitService then check that folder and download new releases from it. Everything else works as before: the **Firmware** update entity in Home Assistant, the update in Kiosk Satellite's remote admin and the checks against the release signature.

Kiosk Satellite updates itself on its own. Its [custom repository](https://github.com/jxlarrea/kiosk-satellite/blob/main/docs/updates.md#custom-repository) does the same for its updates. Its list is `releases.json`, so both can share one folder.

## The folder

| File | What it is |
| --- | --- |
| `echo-releases.json` | GitHub's list of this repository's releases, with each download link changed to point at the folder. |
| `lineageos-echo-kiosk-satellite-<release>-<codename>.zip` | The release for each device you have, under the name GitHub gives it. |
| `lineageos-echo-kiosk-satellite-<release>-<codename>.zip.sig` | The release signature for each zip. A release without it does not install. |

The codenames are `biscuit` for the Echo Dot (2nd gen), `cronos` for the Echo Show 5 (2nd gen) and `crown` for the Echo Show 8 (1st gen).

## Mirror the newest releases

Run this on a computer with Internet access, inside the folder your web server publishes. Set `BASE` to the folder's address as the Echos reach it and `DEVICES` to the codenames you have:

```sh
BASE=http://nas.local/echo
DEVICES="biscuit crown cronos"
REPO=jxlarrea/lineageos-echo-kiosk-satellite
curl -sL "https://api.github.com/repos/$REPO/releases?per_page=100" |
  sed "s#https://github.com/$REPO/releases/download/[^/\"]*/#$BASE/#g" > echo-releases.json
for DEVICE in $DEVICES; do
  TAG=$(grep -m1 -o "\"tag_name\": \"$DEVICE-[^\"]*\"" echo-releases.json | cut -d'"' -f4)
  FILE="lineageos-echo-kiosk-satellite-${TAG#$DEVICE-}-$DEVICE.zip"
  for NAME in "$FILE" "$FILE.sig"; do
    curl -fsLO "https://github.com/$REPO/releases/download/$TAG/$NAME"
  done
done
```

Run it again for each new release. You can delete older zips, since EchoService and BiscuitService only download the newest release for their device.

## Point the Echo at the folder

Run this once for each Echo, with the address of `echo-releases.json` in your folder. On an Echo Show:

```sh
adb shell setprop persist.echo.ota.releases http://nas.local/echo/echo-releases.json
adb shell echo_service update check
adb shell echo_service update status
```

On the Echo Dot:

```sh
adb shell setprop persist.biscuit.ota.releases http://nas.local/echo/echo-releases.json
adb shell biscuit_service update check
adb shell biscuit_service update status
```

`update status` then shows the newest release in the folder. The setting stays across restarts and updates. To go back to GitHub, set it to nothing:

```sh
adb shell setprop persist.echo.ota.releases ""
```

On the Echo Dot, use `persist.biscuit.ota.releases`.

## How it works

- EchoService and BiscuitService download the zip from the link in `echo-releases.json` and check it against the checksum there and the release signature, as they do with GitHub. The signature is made with this repository's key, so only a real release installs, wherever it came from.
- Plain `http://` works.
- While the setting points at your folder, EchoService and BiscuitService never check GitHub. If they cannot reach the folder, the check fails and `update status` says why.
- A folder that only has the installed release or an older one shows the Echo as up to date.
- EchoService and BiscuitService check a minute after every boot. Kiosk Satellite asks them to check again twice a day, as before. To check right away, run `update check`.
- A new install of the image downloads Kiosk Satellite from GitHub on its first boot. On a network without Internet access, install the Kiosk Satellite APK with `adb install` instead. The device's guide covers the Kiosk Satellite installer, such as the [Echo Show 8 guide](echo-show-8.md#kiosk-satellite-installer).
