/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package androidx.camera.extensions.impl;

import android.hardware.camera2.TotalCaptureResult;
import android.util.Size;
import android.view.Surface;

class RequestUpdateProcessorImpls {
    private static final RequestUpdateProcessorImpl sNoUpdateProcessor =
            new RequestUpdateProcessorImpl() {
                @Override
                public CaptureStageImpl process(TotalCaptureResult result) {
                    return null;
                }

                @Override
                public void onOutputSurface(Surface surface, int imageFormat) {}

                @Override
                public void onResolutionUpdate(Size size) {}

                @Override
                public void onImageFormatUpdate(int imageFormat) {}
            };

    static RequestUpdateProcessorImpl noUpdateProcessor() {
        return sNoUpdateProcessor;
    }

    private RequestUpdateProcessorImpls() {
    }
}
