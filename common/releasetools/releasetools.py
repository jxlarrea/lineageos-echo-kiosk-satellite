# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

"""OTA hooks for the Echo Shows.

mt8163-common's hooks link the by-name directory the OTA script writes
through. TARGET_RELEASETOOLS_EXTENSIONS takes one directory, so these do the
same and also add the release the package holds, which system updates check
before they install it: the system image is block based, so its build.prop
cannot be read from the zip.
"""

import common

BLOCK_DEVICE_DIR = "/dev/block/platform/soc/11230000.mmc/"
BY_NAME_SYMLINK = "/dev/block/platform/soc/11230000.mmc/by-name"
BY_NAME_TARGET = "/dev/block/platform/soc/by-name/"

# Read by EchoService's SystemUpdate.java.
_RELEASE_PROP = "META-INF/com/kiosk-satellite/release.prop"


def _setup_block_device_paths(info):
    info.script.AppendExtra('run_program("/sbin/mkdir", "-p", "{}");'.format(BLOCK_DEVICE_DIR))
    info.script.AppendExtra('run_program("/sbin/ln", "-sf", "{}", "{}");'.format(BY_NAME_TARGET, BY_NAME_SYMLINK))


def FullOTA_Assertions(info):
    _setup_block_device_paths(info)


def IncrementalOTA_Assertions(info):
    _setup_block_device_paths(info)


def FullOTA_InstallEnd(info):
    build_prop = info.info_dict["build.prop"]
    release = build_prop.GetProp("ro.echo.release")
    if not release:
        print("No ro.echo.release, so the package cannot be a system update")
        return
    # Android 11's system build.prop has no ro.product.device, only
    # ro.product.system.device.
    common.ZipWriteStr(info.output_zip, _RELEASE_PROP,
                       "ro.product.device=%s\nro.echo.release=%s\n"
                       % (build_prop.GetProp("ro.product.system.device"), release))
