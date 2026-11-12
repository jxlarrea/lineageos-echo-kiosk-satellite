# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

"""OTA hooks for the Echo Dots: the Echo Dot (2nd gen) on amonet v2's TWRP
and the Echo Dot (3rd gen) on the TWRP its unlock installs. Both are TWRP
3.7.0_9-0 with the same slot tool, links and partition numbers.

The OTA writes /dev/block/current-system and current-boot (recovery.fstab),
which TWRP links to the slot it booted on. Before anything is written, the
package checks that it runs in that TWRP and that both links point at the
partitions of the slot the slot tool reports as active. It writes the boot
and system partitions of that slot and nothing else: never the recovery
partition, which holds TWRP.

The package also carries the release it holds, which system updates check
before they install it: the system image is block based, so its build.prop
cannot be read from the zip.
"""

import common

# Read by BiscuitService's SystemUpdate.java.
_RELEASE_PROP = "META-INF/com/amazon/biscuit/release.prop"

_NAMES = {
    "biscuit": "amonet v2 TWRP on the Echo Dot (2nd gen)",
    "donut": "TWRP 3.7.0_9-0 on the Echo Dot (3rd gen)",
}

_SLOT_ASSERTION = (
    'assert(run_program("/sbin/sh", "-c", '
    '"test -x /sbin/amzn_bcbtool || exit 1; '
    'slot=$(/sbin/amzn_bcbtool get_active); '
    'system=$(readlink -f /dev/block/current-system); '
    'boot=$(readlink -f /dev/block/current-boot); '
    'case $slot in '
    'a) test $system = /dev/block/mmcblk0p13 && test $boot = /dev/block/mmcblk0p10 ;; '
    'b) test $system = /dev/block/mmcblk0p14 && test $boot = /dev/block/mmcblk0p11 ;; '
    '*) exit 1 ;; '
    'esac") == "0" || '
    'abort("This package installs only from %s, '
    'booted on the active slot."););'
)


def _device(info):
    device = info.info_dict["build.prop"].GetProp("ro.product.device")
    if device not in _NAMES:
        raise ValueError("%s is not an Echo Dot" % device)
    return device


def _replace_device_assertion(info):
    """TWRP's updater environment cannot be relied on for ro.product.device,
    and the slot check is stricter: the partition numbers are the Dots'."""
    for index, line in enumerate(info.script.script):
        if line.startswith('assert(getprop("ro.product.device")'):
            info.script.script[index] = _SLOT_ASSERTION % _NAMES[_device(info)]
            return
    raise ValueError("no device assertion in the OTA script")


def FullOTA_Assertions(info):
    _replace_device_assertion(info)


def FullOTA_InstallEnd(info):
    release = info.info_dict["build.prop"].GetProp("ro.biscuit.release")
    if not release:
        print("No ro.biscuit.release, so the package cannot be a system update")
        return
    common.ZipWriteStr(info.output_zip, _RELEASE_PROP,
                       "ro.product.device=%s\nro.biscuit.release=%s\n" % (_device(info), release))


def IncrementalOTA_Assertions(info):
    raise ValueError("the Echo Dots only take full OTAs")
