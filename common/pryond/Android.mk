LOCAL_PATH := $(call my-dir)

# Some of Amazon's wake word models load this library by its full path in
# /system/lib. The link points it at /data/pryon/lib, where the user puts it.
# It points nowhere until then.
include $(CLEAR_VARS)
LOCAL_MODULE := pryond-tvm-link
LOCAL_MODULE_CLASS := FAKE
LOCAL_MODULE_TAGS := optional
include $(BUILD_SYSTEM)/base_rules.mk
$(LOCAL_BUILT_MODULE):
	$(hide) mkdir -p $(TARGET_OUT)/lib
	$(hide) ln -sf /data/pryon/lib/libtvm_modelops_ww_v5_cnet.so $(TARGET_OUT)/lib/libtvm_modelops_ww_v5_cnet.so
	$(hide) mkdir -p $(dir $@) && touch $@
