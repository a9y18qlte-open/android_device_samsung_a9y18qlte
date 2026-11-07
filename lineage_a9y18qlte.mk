#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/non_ab_device.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/product_launched_with_o_mr1.mk)

TARGET_BOOT_ANIMATION_RES := 1080

$(call inherit-product, vendor/lineage/config/common_full_phone.mk)
$(call inherit-product, $(LOCAL_PATH)/device.mk)

# In-line GApps integration
ifeq ($(WITH_GAPPS),true)
$(call inherit-product-if-exists, vendor/gapps/arm64/arm64-vendor.mk)
endif

PRODUCT_BRAND := samsung
PRODUCT_DEVICE := a9y18qlte
PRODUCT_MANUFACTURER := samsung
PRODUCT_NAME := lineage_a9y18qlte
PRODUCT_MODEL := SM-A920F

TARGET_VENDOR := samsung
TARGET_VENDOR_PRODUCT_NAME := a9y18qlte
PRODUCT_GMS_CLIENTID_BASE := android-samsung

PRODUCT_BUILD_PROP_OVERRIDES += \
    BuildDesc="a9y18qltexx-user 10 QP1A.190711.020 A920FXXS7CVI9 release-keys" \
    BuildFingerprint=samsung/a9y18qltexx/a9y18qlte:10/QP1A.190711.020/A920FXXS7CVI9:user/release-keys \
    DeviceProduct=a9y18qltexx \
    SystemName=a9y18qltexx
