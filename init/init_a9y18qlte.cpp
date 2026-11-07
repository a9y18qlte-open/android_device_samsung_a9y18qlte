/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string>

#include <android-base/properties.h>
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>

#include "vendor_init.h"

using android::base::GetProperty;

static void property_override(const std::string& name, const std::string& value) {
    auto* pi = const_cast<prop_info*>(__system_property_find(name.c_str()));
    if (pi != nullptr) {
        __system_property_update(pi, value.c_str(), value.size());
    } else {
        __system_property_add(name.c_str(), name.size(), value.c_str(), value.size());
    }
}

void vendor_load_properties() {
    const std::string bootloader = GetProperty("ro.boot.bootloader", "");
    const bool is_a9200 = bootloader.rfind("A9200", 0) == 0;
    const bool is_a920n = bootloader.rfind("A920N", 0) == 0;

    const char* sku = is_a9200 ? "nfc_ese" : "nfc";
    property_override("ro.boot.product.hardware.sku", sku);
    property_override("ro.boot.product.vendor.sku", sku);

    if (!is_a9200 && !is_a920n) {
        return;
    }

    const char* model;
    const char* name;
    const char* fingerprint;
    const char* description;
    const char* omcnw_code;
    if (is_a9200) {
        model = "SM-A9200";
        name = "a9y18qltezh";
        fingerprint =
                "samsung/a9y18qltezh/a9y18qltechn:10/QP1A.190711.020/A9200ZHU5CZB1:user/"
                "release-keys";
        description = "a9y18qltezh-user 10 QP1A.190711.020 A9200ZHU5CZB1 release-keys";
        omcnw_code = "CHN";
    } else {
        model = "SM-A920N";
        name = "a9y18qltekx";
        fingerprint =
                "samsung/a9y18qltekx/a9y18qltekx:10/QP1A.190711.020/A920NKSU5CVG1:user/"
                "release-keys";
        description = "a9y18qltekx-user 10 QP1A.190711.020 A920NKSU5CVG1 release-keys";
        omcnw_code = "KOR";
    }

    for (const auto* partition : {"", "odm.", "product.", "system.", "system_ext.", "vendor."}) {
        property_override(std::string("ro.product.") + partition + "model", model);
        property_override(std::string("ro.product.") + partition + "name", name);
        property_override(std::string("ro.") + partition + "build.fingerprint", fingerprint);
    }
    property_override("ro.build.description", description);

    property_override("ro.csc.omcnw_code", omcnw_code);

    if (is_a9200) {
        property_override("ro.vendor.sec.radio.def_network", "9,1");
        property_override("telephony.lteOnCdmaDevice", "1");
    } else {
        property_override("persist.radio.multisim.config", "ss");
    }
}
