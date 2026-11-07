/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package androidx.camera.extensions.impl;

import android.hardware.camera2.TotalCaptureResult;
import android.media.Image;

import java.util.concurrent.Executor;
/**
 * Processing a single {@link Image} and {@link TotalCaptureResult} to produce an output to a
 * stream.
 *
 * @since 1.0
 * @hide
 */
public interface PreviewImageProcessorImpl extends ProcessorImpl {
    /**
     * Processes the requested image capture.
     *
     * <p> The result of the processing step should be written to the {@link android.view.Surface}
     * that was received by {@link ProcessorImpl#onOutputSurface(android.view.Surface, int)}.
     *
     * @param image The image to process. This will be invalid after the method completes so no
     *              reference to it should be kept.
     * @param result The metadata associated with the image to process.
     * @hide
     */
    void process(Image image, TotalCaptureResult result);

    /**
     * Processes the requested image capture.
     *
     * <p> The result of the processing step should be written to the {@link android.view.Surface}
     * that was received by {@link ProcessorImpl#onOutputSurface(android.view.Surface, int)}.
     *
     * @param image          The {@link ImageFormat#YUV_420_888} format image to process. This will
     *                       be invalid after the method completes so no reference to it should be
     *                       kept.
     * @param result         The metadata associated with the image to process.
     * @param resultCallback Capture result callback to be called once the capture result
     *                       values are ready.
     * @param executor       The executor to run the callback on. If null then the callback will
     *                       run on any arbitrary executor.
     * @since 1.3
     */
    void process(Image image, TotalCaptureResult result, ProcessResultImpl resultCallback,
            Executor executor);
}

