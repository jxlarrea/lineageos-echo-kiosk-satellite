#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),athena)

# Fire OS's vendor policy names its platform version 28.0.0 where Android
# names it 28.0, in plat_sepolicy_vers.txt and in every versioned type
# (device_28_0_0). init looks for the mapping file by that name, so these are
# Android's 28.0 mapping files with the vendor's suffix.
include $(CLEAR_VARS)
LOCAL_MODULE := athena_28.0.0.cil
LOCAL_MODULE_STEM := 28.0.0.cil
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT)/etc/selinux/mapping
include $(BUILD_SYSTEM)/base_rules.mk
$(LOCAL_BUILT_MODULE): $(TARGET_OUT)/etc/selinux/mapping/28.0.cil
	@mkdir -p $(dir $@)
	sed -e 's/_28_0\([^_0-9a-zA-Z]\)/_28_0_0\1/g' -e 's/_28_0$$/_28_0_0/' $< > $@

include $(CLEAR_VARS)
LOCAL_MODULE := athena_28.0.0.compat.cil
LOCAL_MODULE_STEM := 28.0.0.compat.cil
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT)/etc/selinux/mapping
include $(BUILD_SYSTEM)/base_rules.mk
$(LOCAL_BUILT_MODULE): $(TARGET_OUT)/etc/selinux/mapping/28.0.compat.cil
	@mkdir -p $(dir $@)
	sed -e 's/_28_0\([^_0-9a-zA-Z]\)/_28_0_0\1/g' -e 's/_28_0$$/_28_0_0/' $< > $@

# The framework manifest entry for the image's own wpa_supplicant
include $(CLEAR_VARS)
LOCAL_MODULE := athena_supplicant_manifest
LOCAL_MODULE_STEM := android.hardware.wifi.supplicant.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT)/etc/vintf/manifest
LOCAL_SRC_FILES := configs/supplicant-manifest.xml
include $(BUILD_PREBUILT)

endif
