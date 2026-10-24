#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

DEVICE_PATH := device/amazon/biscuit

# Bluetooth (MT8163 connsys, libbt-vendor from hardware/mediatek/mt66xx)
BOARD_HAVE_BLUETOOTH := true
BOARD_MTK_BT_CHIP := MTK_CONSYS_MT8163
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := $(DEVICE_PATH)/bluetooth

# Bootloader
TARGET_BOOTLOADER_BOARD_NAME := biscuit

# Kernel
TARGET_KERNEL_CONFIG := biscuit_defconfig

# Partitions (amonet v2: boot_a/b and system_a/b)
BOARD_FLASH_BLOCK_SIZE := 131072
BOARD_BOOTIMAGE_PARTITION_SIZE := 16777216
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 16777216
BOARD_CACHEIMAGE_PARTITION_SIZE := 822083584
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 805306368
BOARD_USERDATAIMAGE_PARTITION_SIZE := 1325383168

# Properties
TARGET_SYSTEM_PROP += $(DEVICE_PATH)/system.prop
TARGET_VENDOR_PROP += $(DEVICE_PATH)/vendor.prop

# Wi-Fi (MT8163 connsys through /dev/wmtWifi, the HAL from
# hardware/mediatek/mt66xx)
BOARD_WLAN_DEVICE := MediaTek
BOARD_MEDIATEK_COMBO_CHIP := mt66xx
BOARD_HOSTAPD_PRIVATE_LIB := lib_driver_cmd_mt66xx
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_mt66xx
WIFI_DRIVER_FW_PATH_PARAM := "/dev/wmtWifi"
WIFI_DRIVER_FW_PATH_STA := "STA"
WIFI_DRIVER_FW_PATH_AP := "AP"
WIFI_DRIVER_FW_PATH_P2P := "P2P"
WIFI_DRIVER_STATE_CTRL_PARAM := "/dev/wmtWifi"
WIFI_DRIVER_STATE_ON := "1"
WIFI_DRIVER_STATE_OFF := "0"

# Inherit from mt8163-common
include device/amazon/mt8163-common/BoardConfigCommon.mk

# HIDL (after mt8163-common's manifest)
DEVICE_MANIFEST_FILE += $(DEVICE_PATH)/manifest_biscuit.xml

# Blobs the Dot cannot use
# mt8163-common copies the Echo Shows' camera stack, its feature files and
# the Mali GPU driver. The Dot has no camera and its kernel no GPU driver: SwiftShader, gralloc.default
# and hwcomposer.biscuit draw instead. The product's copy list is only final
# once the board config is read, so they are dropped here. The small GPU
# helper libraries stay, since MediaTek's codec blobs link them.
BISCUIT_UNUSED_BLOBS := \
    android.hardware.camera.front.xml \
    android.hardware.drm@1.1-service.widevine \
    android.hardware.drm@1.1-service.widevine.rc \
    kisd \
    kisd.rc \
    lib_uree_mtk_modular_drm.so \
    lib_uree_mtk_video_secure_al.so \
    libGdmaScalerPipe.so \
    libhevce_sb.ca7.android.so \
    libjpeg-alpha_vendor.so \
    libJpgDecPipe.so \
    libJpgEncPipe.so \
    libmhalImageCodec.so \
    libmp4enc_sa.ca7.so \
    libMtkOmxCore.so \
    libMtkOmxVdecEx.so \
    libMtkOmxVenc.so \
    liboemcrypto.so \
    libstagefrighthw.so \
    libSwJpgCodec.so \
    liburee_meta_drmkeyinstall_v2.so \
    libvc1dec_sa.ca7.so \
    libvcodec_oal.so \
    libvcodec_utility.so \
    libvcodecdrv.so \
    libvp8dec_sa.ca7.so \
    libvp8enc_sa.ca7.so \
    libvp9dec_sa.ca7.so \
    libwvhidl.so \
    mtk_omx_core.cfg \
    rpmb_svc \
    rpmb_svc.rc \
    securetime \
    securetime.rc \
    android.hardware.camera.provider@2.4-impl-mediatek.so \
    android.hardware.camera.xml \
    camerahalserver \
    camerahalserver.rc \
    egl.cfg \
    gralloc.mt8163.so \
    hwcomposer.mt8163.so \
    lib3am.so \
    libcam.camadapter.so \
    libcam.campipe.so \
    libcam.camshot.so \
    libcam.client.so \
    libcam.device1.so \
    libcam.exif.so \
    libcam.exif.v3.so \
    libcam.hal3a.v3.dng.so \
    libcam.hal3a.v3.so \
    libcam.halsensor.so \
    libcam.iopipe.so \
    libcam.iopipe_FrmB.so \
    libcam.metadataprovider.so \
    libcam.paramsmgr.so \
    libcam.utils.sensorlistener.so \
    libcam.utils.so \
    libcam1_utils.so \
    libcam3_hwnode.so \
    libcam3_hwpipeline.so \
    libcam3_pipeline.so \
    libcam3_utils.so \
    libcam_hwutils.so \
    libcam_mmp.so \
    libcam_platform.so \
    libcam_utils.so \
    libcamalgo.so \
    libcamdrv.so \
    libcamdrv_FrmB.so \
    libccap.so \
    libccci_util.so \
    libfeatureio.so \
    libfeatureiodrv.so \
    libGLES_mali.so \
    libhdrproc.so \
    libimageio.so \
    libimageio_FrmB.so \
    libimageio_plat_drv.so \
    libimageio_plat_drv_FrmB.so \
    libispfeaturem.so \
    libmtkcam_debugutils.so \
    libmtkcam_device1.so \
    libmtkcam_device3.so \
    libmtkcam_device3_app.so \
    libmtkcam_fwkutils.so \
    libmtkcam_grallocutils.so \
    libmtkcam_imgbuf.so \
    libmtkcam_metadata.so \
    libmtkcam_stdutils.so \
    libmtkcam_sysutils.so \
    libmtkcam_ulog.so \
    libOpenCL.so \
    vendor.mediatek.hardware.camera.ccap@1.0-impl.so \
    vendor.mediatek.hardware.camera.ccap@1.0.so \
    vendor.mediatek.hardware.camera.device@1.1.so
PRODUCT_COPY_FILES := $(foreach f,$(PRODUCT_COPY_FILES),$(if \
    $(filter $(BISCUIT_UNUSED_BLOBS),$(notdir $(call word-colon,2,$(f)))),,$(f)))

# Boot image
# The Dot has 512 MB, so the ramdisk goes to the stock Fire OS 6 address,
# 0x44000000, instead of mt8163-common's. It must stay below the
# ram_console at 0x44400000.
BOARD_RAMDISK_OFFSET := 0x03f88000

# Recovery
# The recovery partition holds amonet v2's TWRP. The build still makes a
# recovery image, which the OTA package needs, but nothing installs it:
# init.target.biscuit.rc disables vendor_flash_recovery.
# The OTA tools write through TWRP's links to the slot it booted on,
# configs/recovery.fstab. The runtime fstab selects the slot itself.
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/configs/recovery.fstab

# Releasetools (instead of mt8163-common's)
TARGET_RELEASETOOLS_EXTENSIONS := $(DEVICE_PATH)/releasetools
