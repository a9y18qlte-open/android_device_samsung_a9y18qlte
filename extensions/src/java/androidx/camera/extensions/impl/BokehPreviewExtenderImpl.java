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
import android.hardware.camera2.params.SessionConfiguration;
import android.util.Pair;
import android.util.Size;

import java.util.List;

/**
 * Preview for the bokeh extension on a9y18qlte: preview requests carry the bokeh extended
 * scene mode so the HAL keeps the depth camera running; the preview itself is not blurred.
 *
 * @hide
 */
public final class BokehPreviewExtenderImpl implements PreviewExtenderImpl {
    private static final int DEFAULT_STAGE_ID = 0;
    private static final int SESSION_STAGE_ID = 101;

    /**
     * @hide
     */
    public BokehPreviewExtenderImpl() {}

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
        return BokehImageCaptureExtenderImpl.isBokehAvailable(cameraCharacteristics);
    }

    /**
     * @hide
     */
    @Override
    public CaptureStageImpl getCaptureStage() {
        return BokehImageCaptureExtenderImpl.bokehStage(DEFAULT_STAGE_ID);
    }

    /**
     * @hide
     */
    @Override
    public ProcessorType getProcessorType() {
        return ProcessorType.PROCESSOR_TYPE_NONE;
    }

    /**
     * @hide
     */
    @Override
    public ProcessorImpl getProcessor() {
        return null;
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
    }

    /**
     * @hide
     */
    @Override
    public void onDeInit() {
    }

    /**
     * @hide
     */
    @Override
    public CaptureStageImpl onPresetSession() {
        return BokehImageCaptureExtenderImpl.bokehStage(SESSION_STAGE_ID);
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
