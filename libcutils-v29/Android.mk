# Android 10 (VNDK v29) libcutils for the Samsung RIL and multiclientd blobs,
# which import strdup8to16() (removed from libcutils in Android 11). The soname
# is changed on install: with its original soname "libcutils.so" the linker
# would hand this old copy to every library of the process that needs libcutils.

LOCAL_PATH := $(call my-dir)
PATCHELF_V29 := prebuilts/extract-tools/linux-x86/bin/patchelf-0_9

include $(CLEAR_VARS)
LOCAL_MODULE := libcutils-v29.vendor64
LOCAL_MODULE_STEM := libcutils-v29
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_MULTILIB := 64
LOCAL_SRC_FILES := ../../../../prebuilts/vndk/v29/arm64/arch-arm64-armv8-a/shared/vndk-sp/libcutils.so
LOCAL_VENDOR_MODULE := true
LOCAL_STRIP_MODULE := false
LOCAL_CHECK_ELF_FILES := false
LOCAL_POST_INSTALL_CMD := $(hide) $(PATCHELF_V29) --set-soname libcutils-v29.so $(TARGET_OUT_VENDOR)/lib64/libcutils-v29.so
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := libcutils-v29.vendor32
LOCAL_MODULE_STEM := libcutils-v29
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_MULTILIB := 32
LOCAL_SRC_FILES := ../../../../prebuilts/vndk/v29/arm64/arch-arm-armv8-a/shared/vndk-sp/libcutils.so
LOCAL_VENDOR_MODULE := true
LOCAL_STRIP_MODULE := false
LOCAL_CHECK_ELF_FILES := false
LOCAL_POST_INSTALL_CMD := $(hide) $(PATCHELF_V29) --set-soname libcutils-v29.so $(TARGET_OUT_VENDOR)/lib/libcutils-v29.so
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := libcutils-v29.system64
LOCAL_MODULE_STEM := libcutils-v29
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_MULTILIB := 64
LOCAL_SRC_FILES := ../../../../prebuilts/vndk/v29/arm64/arch-arm64-armv8-a/shared/vndk-sp/libcutils.so
LOCAL_STRIP_MODULE := false
LOCAL_CHECK_ELF_FILES := false
LOCAL_POST_INSTALL_CMD := $(hide) $(PATCHELF_V29) --set-soname libcutils-v29.so $(TARGET_OUT)/lib64/libcutils-v29.so
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := libcutils-v29.system32
LOCAL_MODULE_STEM := libcutils-v29
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_MULTILIB := 32
LOCAL_SRC_FILES := ../../../../prebuilts/vndk/v29/arm64/arch-arm-armv8-a/shared/vndk-sp/libcutils.so
LOCAL_STRIP_MODULE := false
LOCAL_CHECK_ELF_FILES := false
LOCAL_POST_INSTALL_CMD := $(hide) $(PATCHELF_V29) --set-soname libcutils-v29.so $(TARGET_OUT)/lib/libcutils-v29.so
include $(BUILD_PREBUILT)
