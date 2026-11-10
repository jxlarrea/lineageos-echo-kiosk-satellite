#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

DEVICE_PATH := device/amazon/athena

# The MT8183 is a 64-bit chip that Fire OS runs with a 32-bit userspace.
# Amazon's kernel source only builds it as arm64.
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := cortex-a53
TARGET_USES_64_BIT_BINDER := true

TARGET_NO_BOOTLOADER := true
TARGET_BOOTLOADER_BOARD_NAME := athena
TARGET_BOARD_PLATFORM := mt8183

# Kernel: Amazon's Fire OS 7.5.7.1 source release (Linux 4.4.146).
TARGET_KERNEL_ARCH := arm64
TARGET_KERNEL_SOURCE := kernel/amazon/mt8183
TARGET_KERNEL_CONFIG := athena_defconfig
TARGET_KERNEL_CLANG_COMPILE := false
TARGET_KERNEL_CROSS_COMPILE_PREFIX := $(shell pwd)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-
BOARD_KERNEL_IMAGE_NAME := Image.gz-dtb

# The bootloader loops at the logo on a boot image without a ramdisk, though
# it tells the kernel to skip it (skip_initramfs) and mount system as the
# root. So the boot image carries an empty one.
BOARD_KERNEL_CMDLINE := bootopt=64S3,32N2,64N2
BOARD_KERNEL_CMDLINE += androidboot.selinux=permissive
BOARD_KERNEL_BASE := 0x40000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_OFFSET := 0x00080000
BOARD_RAMDISK_OFFSET := 0x15000000
BOARD_KERNEL_TAGS_OFFSET := 0x14000000
BOARD_SECOND_OFFSET := 0x00f00000
BOARD_BOOTIMG_HEADER_VERSION := 1
BOARD_MKBOOTIMG_ARGS += --kernel_offset $(BOARD_KERNEL_OFFSET)
BOARD_MKBOOTIMG_ARGS += --ramdisk_offset $(BOARD_RAMDISK_OFFSET)
BOARD_MKBOOTIMG_ARGS += --tags_offset $(BOARD_KERNEL_TAGS_OFFSET)
BOARD_MKBOOTIMG_ARGS += --second_offset $(BOARD_SECOND_OFFSET)
BOARD_MKBOOTIMG_ARGS += --header_version $(BOARD_BOOTIMG_HEADER_VERSION)
BOARD_MKBOOTIMG_ARGS += --ramdisk $(DEVICE_PATH)/prebuilt/empty-ramdisk.gz

# Partitions. System is the root, and vendor stays Fire OS's.
BOARD_BUILD_SYSTEM_ROOT_IMAGE := true
BOARD_FLASH_BLOCK_SIZE := 131072
BOARD_BOOTIMAGE_PARTITION_SIZE := 16777216
BOARD_CACHEIMAGE_PARTITION_SIZE := 268435456
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 3254779904
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := ext4
TARGET_COPY_OUT_VENDOR := vendor
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_NO_RECOVERY := true
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/configs/fstab.mt8183

# Wi-Fi: Fire OS's own wpa_supplicant on the vendor partition links a HIDL
# library Amazon added to its Android 9 VNDK, which Android's VNDK 28 does not
# have. So the image builds wpa_supplicant into system, with MediaTek's
# driver commands, and its init script overrides the vendor's service.
WPA_SUPPLICANT_VERSION := VER_0_8_X
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_mtk
BOARD_WLAN_DEVICE := MediaTek
BOARD_MEDIATEK_COMBO_CHIP := mt76xx
WPA_SUPPLICANT_IN_SYSTEM := true

# Properties
TARGET_PRODUCT_PROP += $(DEVICE_PATH)/product.prop

# Treble
BOARD_VNDK_VERSION := current

# SELinux: Fire OS's vendor partition brings its own policy, which already
# labels fuseblk. LineageOS's label for it would conflict.
TARGET_USES_PREBUILT_VENDOR_SEPOLICY := true
TARGET_HAS_FUSEBLK_SEPOLICY_ON_VENDOR := true
PRODUCT_ENFORCE_VINTF_MANIFEST_OVERRIDE := true
