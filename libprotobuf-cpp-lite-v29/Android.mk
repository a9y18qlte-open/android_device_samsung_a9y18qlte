# Android 10 (VNDK v29) libprotobuf-cpp-lite for Widevine libwvhidl.so,
# which references _ZN6google8protobuf8internal13empty_string_E (removed in modern protobuf).
LOCAL_PATH := $(call my-dir)
PATCHELF_V29 := prebuilts/extract-tools/linux-x86/bin/patchelf-0_9

include $(CLEAR_VARS)
LOCAL_MODULE := libprotobuf-cpp-lite-v29.vendor32
LOCAL_MODULE_STEM := libprotobuf-cpp-lite-v29
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_MULTILIB := 32
LOCAL_SRC_FILES := ../../../../prebuilts/vndk/v29/arm64/arch-arm-armv8-a/shared/vndk-core/libprotobuf-cpp-lite.so
LOCAL_VENDOR_MODULE := true
LOCAL_STRIP_MODULE := false
LOCAL_CHECK_ELF_FILES := false
LOCAL_POST_INSTALL_CMD := $(hide) $(PATCHELF_V29) --set-soname libprotobuf-cpp-lite-v29.so $(TARGET_OUT_VENDOR)/lib/libprotobuf-cpp-lite-v29.so
include $(BUILD_PREBUILT)
