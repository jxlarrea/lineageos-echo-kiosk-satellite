#
# The Kiosk Satellite Edition layer every Echo Show inherits. stage-tree.sh
# copies common/ to vendor/kiosk-satellite in the tree.
#

LOCAL_PATH := vendor/kiosk-satellite

# EchoService, which sets the Show up from adb (echo_service) and installs
# system updates for Kiosk Satellite.
PRODUCT_PACKAGES += \
    EchoService \
    echo_service

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

# No setup wizard (EchoService overrides it) and no lock screen (overlay/).
# Built into SettingsProvider instead of a runtime overlay, so it has them
# when it first creates its database.
DEVICE_PACKAGE_OVERLAYS += \
    $(LOCAL_PATH)/overlay

PRODUCT_ENFORCE_RRO_EXCLUDED_OVERLAYS += \
    $(LOCAL_PATH)/overlay/frameworks/base/packages/SettingsProvider

# adb on USB from the first boot, so a Show can be set up without its
# screen. A new computer still has to be allowed on the screen once.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.sys.usb.config=mtp,adb
