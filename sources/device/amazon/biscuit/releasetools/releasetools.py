# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

"""OTA hooks for the Echo Dot (2nd gen) on amonet v2 TWRP.

The OTA writes /dev/block/current-system and current-boot (recovery.fstab),
which TWRP links to the slot it booted on. Before anything is written, the
package checks that it runs in amonet v2's TWRP and that both links point
at the partitions of the slot amonet reports as active.
"""

_AMONET_SLOT_ASSERTION = (
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
    'abort("This package installs only from amonet v2 TWRP on the Echo Dot (2nd gen), '
    'booted on the active slot."););'
)


def _replace_device_assertion(info):
    """TWRP's updater environment cannot be relied on for ro.product.device,
    and the slot check is stricter: the partition numbers are the Dot's."""
    for index, line in enumerate(info.script.script):
        if line.startswith('assert(getprop("ro.product.device")'):
            info.script.script[index] = _AMONET_SLOT_ASSERTION
            return
    raise ValueError("no device assertion in the OTA script")


def FullOTA_Assertions(info):
    _replace_device_assertion(info)


def IncrementalOTA_Assertions(info):
    raise ValueError("the Echo Dot only takes full OTAs")
