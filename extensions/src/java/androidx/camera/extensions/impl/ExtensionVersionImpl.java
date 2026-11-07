/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package androidx.camera.extensions.impl;

import android.util.Log;

/**
 * Implementation for extension version check.
 *
 * <p>This class should be implemented by OEM and deployed to the target devices. 3P developers
 * don't need to implement this, unless this is used for related testing usage.
 *
 * @since 1.0
 * @hide
 */
public class ExtensionVersionImpl {
    private static final String TAG = "ExtenderVersionImpl";
    private static final String VERSION = "1.4.0";

    /**
     * @hide
     */
    public ExtensionVersionImpl() {
    }

    /**
     * Provide the current CameraX extension library version to vendor library and vendor would
     * need to return the supported version for this device. If the returned version is not
     * supported by CameraX library, the Preview and ImageCapture would not be able to enable the
     * specific effects provided by the vendor.
     *
     * <p>CameraX library provides the Semantic Versioning string in a form of
     * MAJOR.MINOR.PATCH-description
     * We will increment the
     * MAJOR version when make incompatible API changes,
     * MINOR version when add functionality in a backwards-compatible manner, and
     * PATCH version when make backwards-compatible bug fixes. And the description can be ignored.
     *
     * <p>Vendor library should provide MAJOR.MINOR.PATCH to CameraX. The MAJOR and MINOR
     * version is used to map to the version of CameraX that it supports, and CameraX extension
     * would only available when MAJOR version is matched with CameraX current version. The PATCH
     * version does not indicate compatibility. The patch version should be incremented whenever
     * the vendor library makes bug fixes or updates to the algorithm.
     *
     * @param version the version of CameraX library formatted as MAJOR.MINOR.PATCH-description.
     * @return the version that vendor supported in this device. The MAJOR.MINOR.PATCH format
     * should be used.
     */
    /**
     * @hide
     */
    public String checkApiVersion(String version) {
        Log.d(TAG, "Extension device library version " + VERSION);
        return VERSION;
    }
}
