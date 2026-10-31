#
# The Kiosk Satellite Edition layer's board settings, for every Echo.
# mt8163-common's BoardConfigCommon.mk includes it (patches/common).
#

# The kernel is named after the image and its release, such as
# 4.9.337-kiosksatellite-cronos-2026.10.6, with build #1 every time. Without
# it the version has the git state setlocalversion reads, which only ever
# says -dirty since the layer's kernel patches are uncommitted changes, and
# the count of links in the build's KERNEL_OBJ. Each device's defconfig
# turns CONFIG_LOCALVERSION_AUTO off (patches/<device>/kernel).
ifneq ($(BISCUIT_RELEASE),)
TARGET_KERNEL_ADDITIONAL_FLAGS += LOCALVERSION=-kiosksatellite-$(TARGET_DEVICE)-$(BISCUIT_RELEASE)
else
TARGET_KERNEL_ADDITIONAL_FLAGS += LOCALVERSION=-kiosksatellite-$(TARGET_DEVICE)
endif
TARGET_KERNEL_ADDITIONAL_FLAGS += KBUILD_BUILD_VERSION=1
