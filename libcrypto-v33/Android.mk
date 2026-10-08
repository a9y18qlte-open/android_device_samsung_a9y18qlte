# Android 13 (VNDK v33) BoringSSL for Samsung's keymaster and gatekeeper blobs.
# Their ASN.1 templates use the old ASN1_TEMPLATE/ASN1_ITEM layout, which
# Android 14 changed (flags/tag and utype became 32-bit), and they import
# sk_delete, which Android 14 no longer exports. The soname is changed on
# install so the rest of the process keeps the Android 14 libcrypto.
LOCAL_PATH := $(call my-dir)
PATCHELF_V33 := prebuilts/extract-tools/linux-x86/bin/patchelf-0_9

include $(CLEAR_VARS)
LOCAL_MODULE := libcrypto-v33.vendor64
LOCAL_MODULE_STEM := libcrypto-v33
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_MULTILIB := 64
LOCAL_SRC_FILES := ../../../../prebuilts/vndk/v33/arm64/arch-arm64-armv8-a/shared/vndk-core/libcrypto.so
LOCAL_VENDOR_MODULE := true
LOCAL_STRIP_MODULE := false
LOCAL_CHECK_ELF_FILES := false
LOCAL_POST_INSTALL_CMD := $(hide) $(PATCHELF_V33) --set-soname libcrypto-v33.so $(TARGET_OUT_VENDOR)/lib64/libcrypto-v33.so
include $(BUILD_PREBUILT)
