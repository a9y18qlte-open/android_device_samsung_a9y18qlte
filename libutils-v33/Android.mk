# Android 13 (VNDK v33) libutils for Qualcomm's pm-service blob. Android 14's
# RefBase aborts when an object is referenced from the stack ("RefBase used
# with stack pointer argument"), which pm-service does in main(). The soname is
# changed on install so only the blob that is patched to need it gets it.
LOCAL_PATH := $(call my-dir)
PATCHELF_V33 := prebuilts/extract-tools/linux-x86/bin/patchelf-0_9

include $(CLEAR_VARS)
LOCAL_MODULE := libutils-v33.vendor64
LOCAL_MODULE_STEM := libutils-v33
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_MULTILIB := 64
LOCAL_SRC_FILES := ../../../../prebuilts/vndk/v33/arm64/arch-arm64-armv8-a/shared/vndk-sp/libutils.so
LOCAL_VENDOR_MODULE := true
LOCAL_STRIP_MODULE := false
LOCAL_CHECK_ELF_FILES := false
LOCAL_POST_INSTALL_CMD := $(hide) $(PATCHELF_V33) --set-soname libutils-v33.so $(TARGET_OUT_VENDOR)/lib64/libutils-v33.so
include $(BUILD_PREBUILT)
