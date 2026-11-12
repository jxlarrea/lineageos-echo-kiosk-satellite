#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

DEVICE_PATH := device/amazon/donut

# The MT8167B is a 64-bit chip that Fire OS 6 runs with a 32-bit userspace
# on a 64-bit kernel. Its Cortex-A35 cores take the Cortex-A53 tuning.
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := cortex-a53
TARGET_USES_64_BIT_BINDER := true

# Bluetooth (MT7668, libbt-vendor from hardware/mediatek/mt76xx)
BOARD_HAVE_BLUETOOTH := true
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := $(DEVICE_PATH)/bluetooth

TARGET_NO_BOOTLOADER := true
TARGET_BOOTLOADER_BOARD_NAME := donut
TARGET_BOARD_PLATFORM := mt8167
MALLOC_SVELTE := true

# Kernel: Amazon's Fire OS 6.5.7.1 source release (Linux 4.4.22), with the
# upstream files it leaves out and what Android 11 needs.
TARGET_KERNEL_ARCH := arm64
TARGET_KERNEL_SOURCE := kernel/amazon/mt8167
TARGET_KERNEL_CONFIG := lineage_donut_defconfig
TARGET_KERNEL_CLANG_COMPILE := false
TARGET_KERNEL_CROSS_COMPILE_PREFIX := $(shell pwd)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-
BOARD_KERNEL_IMAGE_NAME := Image.gz-dtb

# The Wi-Fi and Bluetooth drivers are kernel modules, built from Amazon's
# sources in the kernel tree (drivers/amazon). init.mt8167.rc loads them.
NEED_KERNEL_MODULE_SYSTEM := true

# Boot image, at Fire OS 6's addresses. The bootloader drops what an image
# adds to the command line, so the kernel config carries the image's own
# options (CONFIG_CMDLINE in lineage_donut_defconfig).
BOARD_KERNEL_CMDLINE := bootopt=64S3,32N2,64N2
BOARD_KERNEL_BASE := 0x40000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_OFFSET := 0x00080000
BOARD_RAMDISK_OFFSET := 0x04000000
BOARD_KERNEL_TAGS_OFFSET := 0x0e000000
BOARD_SECOND_OFFSET := 0x00f00000
BOARD_BOOTIMG_HEADER_VERSION := 0
BOARD_MKBOOTIMG_ARGS += --kernel_offset $(BOARD_KERNEL_OFFSET)
BOARD_MKBOOTIMG_ARGS += --ramdisk_offset $(BOARD_RAMDISK_OFFSET)
BOARD_MKBOOTIMG_ARGS += --tags_offset $(BOARD_KERNEL_TAGS_OFFSET)
BOARD_MKBOOTIMG_ARGS += --second_offset $(BOARD_SECOND_OFFSET)
BOARD_MKBOOTIMG_ARGS += --header_version $(BOARD_BOOTIMG_HEADER_VERSION)
BOARD_MKBOOTIMG_ARGS += --ramdisk $(DEVICE_PATH)/prebuilt/empty-ramdisk.gz

# Partitions: two slots of boot and system, with no vendor partition. The
# bootloader does not run the boot image's ramdisk on a normal boot. It has
# the kernel mount the slot's system partition as the root, so system is the
# root and the boot image only carries an empty ramdisk.
BOARD_BUILD_SYSTEM_ROOT_IMAGE := true
BOARD_FLASH_BLOCK_SIZE := 131072
BOARD_BOOTIMAGE_PARTITION_SIZE := 16777216
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 33554432
BOARD_CACHEIMAGE_PARTITION_SIZE := 33554432
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1073741824
BOARD_USERDATAIMAGE_PARTITION_SIZE := 1617411584
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
TARGET_COPY_OUT_VENDOR := system/vendor
TARGET_USERIMAGES_USE_EXT4 := true

# Recovery: the Dot keeps the TWRP its unlock put in the swdl partition.
# The build makes a recovery image only because the update zip needs one
# defined.
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/configs/recovery.fstab
TARGET_RECOVERY_PIXEL_FORMAT := RGBX_8888

# Properties. Without a vendor partition they all go in system/build.prop.
TARGET_SYSTEM_PROP += $(DEVICE_PATH)/system.prop $(DEVICE_PATH)/vendor.prop

# Wi-Fi (MT7668, the HAL from hardware/mediatek/mt76xx)
BOARD_WLAN_DEVICE := MediaTek
BOARD_MEDIATEK_COMBO_CHIP := mt76xx
WPA_SUPPLICANT_VERSION := VER_0_8_X
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_HOSTAPD_DRIVER := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_mtk
BOARD_HOSTAPD_PRIVATE_LIB := lib_driver_cmd_mtk
WIFI_HIDL_UNIFIED_SUPPLICANT_SERVICE_RC_ENTRY := true

# HIDL
DEVICE_MANIFEST_FILE += $(DEVICE_PATH)/manifest.xml
PRODUCT_ENFORCE_VINTF_MANIFEST_OVERRIDE := true

# The layer's board settings, which name the kernel after the image and its
# release.
include vendor/kiosk-satellite/config/BoardConfig.mk
