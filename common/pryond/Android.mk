LOCAL_PATH := $(call my-dir)

# Some of Amazon's wake word models load this library by its full path in
# /system/lib. The link points it at /data/pryon/lib, where the user puts it.
# It points nowhere until then.
include $(CLEAR_VARS)
LOCAL_MODULE := pryond-tvm-link
LOCAL_MODULE_CLASS := FAKE
LOCAL_MODULE_TAGS := optional
include $(BUILD_SYSTEM)/base_rules.mk
# The link is an installed file of its own, so every system image build
# makes it. As a side effect of the module it was only made when something
# else happened to build the module, which the Echo Show 8 (2nd gen)'s build
# never did.
PRYOND_TVM_LINK := $(TARGET_OUT)/lib/libtvm_modelops_ww_v5_cnet.so
$(PRYOND_TVM_LINK):
	$(hide) mkdir -p $(dir $@)
	$(hide) ln -sf /data/pryon/lib/libtvm_modelops_ww_v5_cnet.so $@

ALL_DEFAULT_INSTALLED_MODULES += $(PRYOND_TVM_LINK)

$(LOCAL_BUILT_MODULE): $(PRYOND_TVM_LINK)
	$(hide) mkdir -p $(dir $@) && touch $@
