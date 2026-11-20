/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package androidx.camera.extensions.impl;

import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CaptureRequest;

/**
 * The burst of the night extension: {@link #FRAMES} frames with the preview's auto exposure
 * locked, in Low Light Boost where the camera has it. Auto exposure adds digital gain that the
 * results do not report, so manual settings computed from them come out darker.
 */
final class NightExposure {
    static final int FRAMES = 6;
    static final int REFERENCE = 0;

    private NightExposure() {
    }

    static boolean hasLowLightBoost(CameraCharacteristics chars) {
        int[] modes = chars != null
                ? chars.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_MODES) : null;
        if (modes != null) {
            for (int mode : modes) {
                if (mode == CaptureRequest.CONTROL_AE_MODE_ON_LOW_LIGHT_BOOST_BRIGHTNESS_PRIORITY) {
                    return true;
                }
            }
        }
        return false;
    }

    static SettableCaptureStage[] stages(CameraCharacteristics chars, int firstId) {
        int aeMode = hasLowLightBoost(chars)
                ? CaptureRequest.CONTROL_AE_MODE_ON_LOW_LIGHT_BOOST_BRIGHTNESS_PRIORITY
                : CaptureRequest.CONTROL_AE_MODE_ON;
        SettableCaptureStage[] stages = new SettableCaptureStage[FRAMES];
        for (int i = 0; i < FRAMES; i++) {
            SettableCaptureStage s = new SettableCaptureStage(firstId + i);
            s.addCaptureRequestParameters(CaptureRequest.CONTROL_AE_MODE, aeMode);
            s.addCaptureRequestParameters(CaptureRequest.CONTROL_AE_LOCK, true);
            s.addCaptureRequestParameters(CaptureRequest.CONTROL_AWB_LOCK, true);
            s.addCaptureRequestParameters(CaptureRequest.FLASH_MODE,
                    CaptureRequest.FLASH_MODE_OFF);
            stages[i] = s;
        }
        return stages;
    }
}
