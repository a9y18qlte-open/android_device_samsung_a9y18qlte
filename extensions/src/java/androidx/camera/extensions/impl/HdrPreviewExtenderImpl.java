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

import android.content.Context;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.TotalCaptureResult;
import android.hardware.camera2.params.SessionConfiguration;
import android.util.Pair;
import android.util.Size;
import android.view.Surface;

import java.util.List;

/**
 * Preview for the HDR extension on a9y18qlte: a plain auto exposure preview, whose results
 * meter the still capture bracket ({@link HdrExposure}). The preview itself is not fused.
 *
 * @hide
 */
public final class HdrPreviewExtenderImpl implements PreviewExtenderImpl {
    private static final int DEFAULT_STAGE_ID = 0;

    private static final RequestUpdateProcessorImpl sMeter = new RequestUpdateProcessorImpl() {
        @Override
        public CaptureStageImpl process(TotalCaptureResult result) {
            if (result != null) {
                HdrExposure.onPreviewResult(result);
            }
            return null;
        }

        @Override
        public void onOutputSurface(Surface surface, int imageFormat) {
        }

        @Override
        public void onResolutionUpdate(Size size) {
        }

        @Override
        public void onImageFormatUpdate(int imageFormat) {
        }
    };

    /**
     * @hide
     */
    public HdrPreviewExtenderImpl() {
    }

    /**
     * @hide
     */
    @Override
    public void init(String cameraId, CameraCharacteristics cameraCharacteristics) {
    }

    /**
     * @hide
     */
    @Override
    public boolean isExtensionAvailable(String cameraId,
            CameraCharacteristics cameraCharacteristics) {
        return HdrImageCaptureExtenderImpl.isHdrAvailable(cameraCharacteristics);
    }

    /**
     * @hide
     */
    @Override
    public CaptureStageImpl getCaptureStage() {
        return new SettableCaptureStage(DEFAULT_STAGE_ID);
    }

    /**
     * @hide
     */
    @Override
    public ProcessorType getProcessorType() {
        return ProcessorType.PROCESSOR_TYPE_REQUEST_UPDATE_ONLY;
    }

    /**
     * @hide
     */
    @Override
    public ProcessorImpl getProcessor() {
        return sMeter;
    }

    /**
     * @hide
     */
    @Override
    public List<Pair<Integer, Size[]>> getSupportedResolutions() {
        return null;
    }

    /**
     * @hide
     */
    @Override
    public void onInit(String cameraId, CameraCharacteristics cameraCharacteristics,
            Context context) {
        HdrExposure.reset();
    }

    /**
     * @hide
     */
    @Override
    public void onDeInit() {
        HdrExposure.reset();
    }

    /**
     * @hide
     */
    @Override
    public CaptureStageImpl onPresetSession() {
        return null;
    }

    /**
     * @hide
     */
    @Override
    public CaptureStageImpl onEnableSession() {
        return null;
    }

    /**
     * @hide
     */
    @Override
    public CaptureStageImpl onDisableSession() {
        return null;
    }

    /**
     * @hide
     */
    @Override
    public int onSessionType() {
        return SessionConfiguration.SESSION_REGULAR;
    }
}
