/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package androidx.camera.extensions.impl;

import android.util.Size;
import android.view.Surface;

/**
 * Processes an input image stream and produces an output image stream.
 *
 * @since 1.0
 * @hide
 */
public interface ProcessorImpl {
    /**
     * Updates where the ProcessorImpl should write the output to.
     *
     * @param surface     The {@link Surface} that the ProcessorImpl should write data into.
     * @param imageFormat The format of that the surface expects.
     */
    void onOutputSurface(Surface surface, int imageFormat);

    /**
     * Invoked when CameraX changes the configured output resolution.
     *
     * <p>After this call, {@link CaptureProcessorImpl} should expect any {@link Image} received as
     * input to be at the specified resolution.
     *
     * @param size for the surface.
     */
    void onResolutionUpdate(Size size);

    /**
     * Invoked when CameraX changes the configured input image format.
     *
     * <p>After this call, {@link CaptureProcessorImpl} should expect any {@link Image} received as
     * input to have the specified image format.
     *
     * @param imageFormat for the surface.
     */
    void onImageFormatUpdate(int imageFormat);
}
