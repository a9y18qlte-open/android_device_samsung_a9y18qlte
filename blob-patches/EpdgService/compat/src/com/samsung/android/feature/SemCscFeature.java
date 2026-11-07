/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.samsung.android.feature;

/** Samsung CSC features; none are set on AOSP. */
public final class SemCscFeature {
    private static final SemCscFeature sInstance = new SemCscFeature();

    private SemCscFeature() {}

    public static SemCscFeature getInstance() {
        return sInstance;
    }

    public boolean getBoolean(String tag, boolean def) {
        return def;
    }

    public boolean getBoolean(int phoneId, String tag) {
        return false;
    }
}
