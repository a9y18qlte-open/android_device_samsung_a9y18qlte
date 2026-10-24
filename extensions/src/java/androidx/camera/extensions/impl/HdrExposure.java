/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package androidx.camera.extensions.impl;

import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.CaptureResult;
import android.hardware.camera2.TotalCaptureResult;
import android.util.Range;

/**
 * The exposure bracket of the HDR extension.
 *
 * <p>The preview runs with auto exposure, and its results give the exposure the bracket is
 * centred on; the still capture stages then set each frame's exposure by hand. Exposure
 * time changes first, so that the gain (and the noise) stays that of the metered frame, up to
 * a handheld limit; past it the gain makes up the rest.
 */
final class HdrExposure {
    /** Exposure value offsets of the bracket, in stops. */
    static final float[] EV = {-2f, 0f, 2f};
    /** Index of the metered frame in {@link #EV}, which the others are aligned to. */
    static final int REFERENCE = 1;

    private static final long MAX_HANDHELD_NS = 66_000_000L;
    private static final long MIN_FRAME_NS = 33_333_333L;

    private static final Object sLock = new Object();
    private static long sExposureNs;
    private static int sSensitivity;

    private HdrExposure() {
    }

    /** Remembers the metered exposure of a preview frame. */
    static void onPreviewResult(TotalCaptureResult result) {
        Long t = result.get(CaptureResult.SENSOR_EXPOSURE_TIME);
        Integer iso = result.get(CaptureResult.SENSOR_SENSITIVITY);
        Integer ae = result.get(CaptureResult.CONTROL_AE_MODE);
        if (t == null || iso == null || ae == null || ae == CaptureResult.CONTROL_AE_MODE_OFF) {
            return;
        }
        synchronized (sLock) {
            sExposureNs = t;
            sSensitivity = iso;
        }
    }

    static void reset() {
        synchronized (sLock) {
            sExposureNs = 0;
            sSensitivity = 0;
        }
    }

    /**
     * The settings of each frame of the bracket, or null while no preview frame has been
     * metered.
     */
    static SettableCaptureStage[] stages(CameraCharacteristics chars, int firstId) {
        long t0;
        int iso0;
        synchronized (sLock) {
            t0 = sExposureNs;
            iso0 = sSensitivity;
        }
        if (t0 <= 0 || iso0 <= 0 || chars == null) {
            return null;
        }
        Range<Long> tRange = chars.get(CameraCharacteristics.SENSOR_INFO_EXPOSURE_TIME_RANGE);
        Range<Integer> isoRange = chars.get(CameraCharacteristics.SENSOR_INFO_SENSITIVITY_RANGE);
        if (tRange == null || isoRange == null) {
            return null;
        }
        long tMax = Math.min(tRange.getUpper(), Math.max(MAX_HANDHELD_NS, t0));

        SettableCaptureStage[] stages = new SettableCaptureStage[EV.length];
        for (int i = 0; i < EV.length; i++) {
            double total = (double) t0 * iso0 * Math.pow(2, EV[i]);
            long t;
            int iso;
            if (EV[i] < 0) {
                // shorter exposure, gain as metered (lower it only past the shortest time)
                iso = iso0;
                t = Math.round(total / iso);
                if (t < tRange.getLower()) {
                    t = tRange.getLower();
                    iso = (int) Math.round(total / t);
                }
            } else {
                t = Math.min(tMax, Math.round(total / iso0));
                iso = (int) Math.round(total / t);
            }
            iso = isoRange.clamp(iso);
            t = tRange.clamp(t);

            SettableCaptureStage s = new SettableCaptureStage(firstId + i);
            s.addCaptureRequestParameters(CaptureRequest.CONTROL_AE_MODE,
                    CaptureRequest.CONTROL_AE_MODE_OFF);
            s.addCaptureRequestParameters(CaptureRequest.SENSOR_EXPOSURE_TIME, t);
            s.addCaptureRequestParameters(CaptureRequest.SENSOR_SENSITIVITY, iso);
            s.addCaptureRequestParameters(CaptureRequest.SENSOR_FRAME_DURATION,
                    Math.max(t, MIN_FRAME_NS));
            s.addCaptureRequestParameters(CaptureRequest.CONTROL_AWB_LOCK, true);
            s.addCaptureRequestParameters(CaptureRequest.FLASH_MODE,
                    CaptureRequest.FLASH_MODE_OFF);
            stages[i] = s;
        }
        return stages;
    }
}
