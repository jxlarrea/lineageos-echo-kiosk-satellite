# adb keys

The Echo Dot runs adb over Wi-Fi on port 5555 with no password, so a device
with no screen can always be reached. That also means anything on the same
network can reach it, and a root shell is two commands away. This page is how
to close that: install your computer's key, then require one.

The command is `biscuit_service` on the Dot and `echo_service` on the Echo
Shows, where both work the same way. The Shows have a screen, so their own
guides cover how adb behaves there.

## Why the key has to be installed first

Android shows a prompt the first time an unknown computer connects. An Echo has
no screen to show it on, which is why the image asks for no key at all.

A key that is already in `/data/misc/adb/adb_keys` is accepted without a prompt,
which is all adbd looks at once key authentication is on. So a device can be
handed a key over the connection it boots with, and require one from then on.

## Installing your key

`adbkey.pub`, next to the private key adb made for you, holds it. The first word
is the key and the rest is a comment naming the computer:

```sh
adb shell biscuit_service adb key add "$(cat ~/.android/adbkey.pub)"
```

It answers `key installed, 1 in all`. Run it once per computer; the image keeps
up to 32. The same key twice is answered `that key is already installed`.

A key that is not a key is refused rather than written, so a mistyped command
cannot leave you with a file adbd will not read:

```
$ adb shell biscuit_service adb key add hello
hello is not a base64 adb public key
```

## Requiring a key

```sh
adb shell biscuit_service adb secure on
```

adbd restarts, so this connection drops. Reconnect and it uses the key you
installed.

`adb secure off` takes that back. It only undoes what `on` did, so on a device
that asks for a key by itself, such as an Echo Show, it changes nothing.

With no key installed this is refused, since it would leave the device
unreachable with no way back but recovery:

```
$ adb shell biscuit_service adb secure on
no key is installed, so adb would stop answering. Install one with `adb key add` first.
```

## Checking

```sh
adb shell biscuit_service adb status
```

ends in `key required no`, or `key required yes, 1 installed`.

## If you lose the key

TWRP's adb needs no key, so it is the way back in. Start TWRP, then clear the
setting and start Android again:

```sh
adb reboot recovery
adb shell rm /data/property/persist.echo.adb_secure
adb reboot
```

That leaves the keys installed but no longer required, so you can install a new
one and turn it back on. Removing `/data/misc/adb/adb_keys` as well forgets
them.

## What this does not do

- **It does not turn off `adb root`.** `adb secure on` decides who may connect,
  not what they may do afterwards. [Rooted debugging](echo-dot.md#network) is a
  separate switch, off by default, and it stays off.
- **It does not help against someone who already has a key.** Every key in
  `adb_keys` reaches the device, so remove a computer's key when the computer
  goes. That needs a root shell, or TWRP.
- **It does not close the port.** adbd still listens on 5555. What changes is
  that it now refuses to talk to anything without a key.

## How it works

`adb secure on` sets `persist.echo.adb_secure`, which init watches and answers
by restarting adbd. adbd reads it as it starts, in
`patches/common/system/core`, and requires a key when it is 1. The property can
only add the requirement, never take one away, so a device that already asks
for a key keeps asking. Unset, which is how every device ships, adbd behaves
exactly as it did before.
