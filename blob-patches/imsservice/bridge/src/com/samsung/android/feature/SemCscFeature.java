/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.samsung.android.feature;

/**
 * Samsung framework class imsservice reads carrier (CSC) features from; not part of AOSP.
 * Reports every feature as unset, which is what Samsung's own implementation returns for
 * a key the CSC does not define.
 */
public final class SemCscFeature {
    private static final SemCscFeature sInstance = new SemCscFeature();

    private SemCscFeature() {}

    public static SemCscFeature getInstance() {
        return sInstance;
    }

    public boolean getBoolean(String tag) {
        return false;
    }

    public boolean getBoolean(int phoneId, String tag) {
        return false;
    }

    public boolean getBoolean(String tag, boolean defaultValue) {
        return defaultValue;
    }

    public boolean getBoolean(int phoneId, String tag, boolean defaultValue) {
        return defaultValue;
    }

    public String getString(String tag) {
        return "";
    }

    public String getString(int phoneId, String tag) {
        return "";
    }

    public String getString(String tag, String defaultValue) {
        return defaultValue;
    }

    public String getString(int phoneId, String tag, String defaultValue) {
        return defaultValue;
    }

    public int getInt(String tag) {
        return 0;
    }

    public int getInt(int phoneId, String tag) {
        return 0;
    }

    public int getInt(String tag, int defaultValue) {
        return defaultValue;
    }

    public int getInt(int phoneId, String tag, int defaultValue) {
        return defaultValue;
    }
}
