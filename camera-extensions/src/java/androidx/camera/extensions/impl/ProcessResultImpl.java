/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package androidx.camera.extensions.impl;

import android.hardware.camera2.CaptureResult;
import android.util.Pair;

import java.util.List;

/**
 * Allows clients to receive information about the capture result values of processed frames.
 *
 */
public interface ProcessResultImpl {
    /**
     * Capture result callback that needs to be called when the process capture results are
     * ready as part of frame post-processing.
     *
     * @param shutterTimestamp     The shutter time stamp of the processed frame.
     * @param result               Key value pairs for all supported capture results. Do note that
     *                             if results 'android.jpeg.quality' and 'android.jpeg.orientation'
     *                             are present in the process capture input results, then the values
     *                             must also be passed as part of this callback. Both Camera2 and
     *                             CameraX guarantee that those two settings and results are always
     *                             supported and applied by the corresponding framework.
     * @since 1.3
     */
    void onCaptureCompleted(long shutterTimestamp, List<Pair<CaptureResult.Key, Object>> result);


    /**
     * Capture progress callback that needs to be called when the process capture is
     * ongoing and includes the estimated progress of the processing.
     *
     * <p>Extensions must ensure that they always call this callback with monotonically increasing
     * values.</p>
     *
     * <p>Extensions are allowed to trigger this callback multiple times but at the minimum the
     * callback is expected to be called once when processing is done with value 100.</p>
     *
     * @param progress             Value between 0 and 100.
     * @since 1.4
     */
    void onCaptureProcessProgressed(int progress);
}
