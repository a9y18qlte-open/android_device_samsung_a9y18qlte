/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package androidx.camera.extensions.impl;

import android.hardware.camera2.CaptureRequest;
import android.util.Pair;

import java.util.List;

/**
 * The set of parameters that defines a single capture that will be sent to the camera.
 *
 * @since 1.0
     * @hide
 */
public interface CaptureStageImpl {
    /** Returns the identifier for the {@link CaptureStageImpl}. */
    /**
     * @hide
     */
    int getId();

    /**
     * Returns the set of {@link CaptureRequest.Key} and the corresponding values that will be
     * set for a single {@link CaptureRequest}.
     */
    /**
     * @hide
     */
    List<Pair<CaptureRequest.Key, Object>> getParameters();
}
