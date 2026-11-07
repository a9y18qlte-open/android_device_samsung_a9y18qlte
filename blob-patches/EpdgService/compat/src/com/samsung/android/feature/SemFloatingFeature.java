/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.samsung.android.feature;

import android.os.SystemProperties;

/**
 * Samsung floating features EpdgService reads. Only the dual IMS mode is used; on AOSP it
 * comes from persist.ril.config.dualims (device vendor.prop), like for imsservice.
 */
public final class SemFloatingFeature {
    private static final SemFloatingFeature sInstance = new SemFloatingFeature();

    private SemFloatingFeature() {}

    public static SemFloatingFeature getInstance() {
        return sInstance;
    }

    public String getString(String tag, String def) {
        if ("SEC_FLOATING_FEATURE_COMMON_CONFIG_DUAL_IMS".equals(tag)) {
            return SystemProperties.get("persist.ril.config.dualims", def);
        }
        return def;
    }
}
