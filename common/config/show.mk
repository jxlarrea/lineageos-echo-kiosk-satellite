#
# The Kiosk Satellite Edition layer every Echo Show inherits on top of
# common.mk. The Echo Dot does not: it has BiscuitService and its own
# ks-install, init and defaults (device/biscuit). stage-tree.sh copies
# common/ to vendor/kiosk-satellite in the tree.
#

LOCAL_PATH := vendor/kiosk-satellite

# EchoService, which sets the Show up from adb (echo_service) and installs
# system updates for Kiosk Satellite.
PRODUCT_PACKAGES += \
    EchoService \
    echo_service \
    echo_mic_test

# Echo cancellation on the microphones (audio/amznaec): the FPGA's copy of
# the speaker signal is the reference, so the wake word works over music.
# Each device sets ro.vendor.amznaec.gain_db for its microphone gain.
PRODUCT_PACKAGES += \
    libamznaec_shim \
    init.kiosk-satellite.audio.rc

# Kiosk Satellite on first boot, with its grants and device owner. curl
# fetches it from GitHub.
PRODUCT_PACKAGES += \
    curl \
    echo-ks-install \
    init.kiosk-satellite.rc

# System updates from this repository's GitHub releases through EchoService
# and TWRP (echo-service/SystemUpdate.java, ota/, releasetools/).
# scripts/build.sh sets BISCUIT_RELEASE from the newest CHANGELOG.md heading
# for the device, with -dev after it while that heading is Unreleased.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/ota/twrp-update.sh:$(TARGET_COPY_OUT_SYSTEM)/etc/echo-ota/twrp-update.sh \
    $(LOCAL_PATH)/ota/release-key.pem:$(TARGET_COPY_OUT_SYSTEM)/etc/echo-ota/release-key.pem

# Kiosk Satellite offers Amazon's wake word engine where this is set
# (biscuit-pryond, common.mk). The Echo Dot sets ro.biscuit.pryon instead.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.echo.pryon=1

# In system/build.prop, where releasetools.py reads the release from.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.echo.ota=1
ifneq ($(BISCUIT_RELEASE),)
PRODUCT_PROPERTY_OVERRIDES += \
    ro.echo.release=$(BISCUIT_RELEASE)
endif

# The device name as the DHCP hostname (rro/NetworkStack). EchoService names
# the Show after ro.echo.hostname, which each device sets.
PRODUCT_PACKAGES += \
    EchoNetworkStackOverlay

# No setup wizard (EchoService overrides it), no lock screen, no Trust
# notifications and none of the Bluetooth profiles a kiosk has nothing for
# (overlay/). They are built into their packages instead of runtime overlays:
# SettingsProvider and LineageSettingsProvider need their defaults when they
# first create their databases, and Bluetooth's manifest enables each
# profile's service from the same values.
DEVICE_PACKAGE_OVERLAYS += \
    $(LOCAL_PATH)/overlay

PRODUCT_ENFORCE_RRO_EXCLUDED_OVERLAYS += \
    $(LOCAL_PATH)/overlay/frameworks/base/core \
    $(LOCAL_PATH)/overlay/frameworks/base/packages/SettingsProvider \
    $(LOCAL_PATH)/overlay/lineage-sdk/packages/LineageSettingsProvider \
    $(LOCAL_PATH)/overlay/packages/apps/Bluetooth

# adb on USB from the first boot, so a Show can be set up without its
# screen. A new computer still has to be allowed on the screen once.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.sys.usb.config=mtp,adb

# Memory, as on the Echo Dot. One cached process and every OOM adjustment a
# full one (patches/cronos/frameworks/base): Android otherwise kept idle apps
# such as Settings, the keyboard and the launcher in memory the kiosk never
# switches to. Shared storage on sdcardfs instead of FUSE, so MediaProvider
# does not run all the time: both properties are needed, since
# StorageManagerService resets persist.sys.fuse from the settings_fuse flag at
# boot. Media metrics clients skip the service init/ leaves out.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.config.max_cached_processes=1 \
    ro.config.oomadj_update_quick=false \
    persist.sys.fuse=false \
    persist.sys.fflag.override.settings_fuse=false \
    persist.media.metrics.enabled=0

# Java heaps keep less free space after each collection than the tablet
# defaults. The device's dalvik-heap.mk sets these in system/build.prop, where
# the first definition stays, so they go in product/build.prop, which Android
# loads later.
PRODUCT_PRODUCT_PROPERTIES += \
    dalvik.vm.heaptargetutilization=0.9 \
    dalvik.vm.heapmaxfree=4m

# The Kiosk Satellite mark as the boot animation, in place of LineageOS's
# (media/, scripts/make-bootanimation.py). Its bars bounce while Android
# boots, then settle and the house fades in around them. There is one zip
# for each Show's screen height, which the device sets.
TARGET_BOOTANIMATION = vendor/kiosk-satellite/media/bootanimation-$(TARGET_SCREEN_HEIGHT).zip

# A plain #444444 wallpaper on a fresh install. WallpaperManager opens the
# file ro.config.wallpaper names before the framework's default wallpaper.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/media/wallpaper.png:$(TARGET_COPY_OUT_SYSTEM)/media/wallpaper.png

PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.config.wallpaper=/system/media/wallpaper.png

# Features the Shows never use, which keeps their services out of
# system_server (permissions/).
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/permissions/kiosk-satellite-show.xml:$(TARGET_COPY_OUT_SYSTEM)/etc/permissions/kiosk-satellite-show.xml
