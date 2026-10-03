#
# Copyright (C) 2022-2026 crDroid Android Project
#
# SPDX-License-Identifier: Apache-2.0
#

$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/product_launched_with_o_mr1.mk)

TARGET_BOOT_ANIMATION_RES := 1080

# Inherit from a9y18qlte device
$(call inherit-product, $(LOCAL_PATH)/device.mk)

# Inherit some common crDroid stuff
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

# In-line GApps integration
ifeq ($(WITH_GAPPS),true)
$(call inherit-product-if-exists, vendor/gapps/arm64/arm64-vendor.mk)
endif

PRODUCT_BRAND := samsung
PRODUCT_DEVICE := a9y18qlte
PRODUCT_MANUFACTURER := samsung
PRODUCT_NAME := crdroid_a9y18qlte
PRODUCT_MODEL := SM-A920F

TARGET_VENDOR := samsung
TARGET_VENDOR_PRODUCT_NAME := a9y18qlte
PRODUCT_GMS_CLIENTID_BASE := android-samsung

PRODUCT_BUILD_PROP_OVERRIDES += \
    PRIVATE_BUILD_DESC="a9y18qltexx-user 10 QP1A.190711.020 A920FXXS7CVI9 release-keys"

BUILD_FINGERPRINT := "samsung/a9y18qltexx/a9y18qlte:10/QP1A.190711.020/A920FXXS7CVI9:user/release-keys"
