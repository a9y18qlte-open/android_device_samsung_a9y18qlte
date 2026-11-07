/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package androidx.camera.extensions.impl;

import android.hardware.camera2.TotalCaptureResult;

/**
 * Processes a {@link TotalCaptureResult} to update a CaptureStage.
 *
 * @since 1.0
 * @hide
 */
public interface RequestUpdateProcessorImpl extends ProcessorImpl {
    /**
     * Process the {@link TotalCaptureResult} to update the {@link CaptureStageImpl}
     *
     * @param result The metadata associated with the image. Can be null if the image and meta have
     *               not been synced.
     * @return The updated parameters used for the repeating requests. If this is {@code null} then
     * the previous parameters will be used.
     */
    CaptureStageImpl process(TotalCaptureResult result);
}
