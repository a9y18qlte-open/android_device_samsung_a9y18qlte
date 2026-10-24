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
import android.graphics.ImageFormat;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.CaptureResult;
import android.hardware.camera2.params.Capability;
import android.hardware.camera2.params.SessionConfiguration;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.util.Pair;
import android.util.Range;
import android.util.Size;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Bokeh still capture on a9y18qlte.
 *
 * <p>The camera HAL renders the bokeh itself (depth sensor + Samsung's dual camera refocus
 * engine) when a request uses CONTROL_EXTENDED_SCENE_MODE_BOKEH_STILL_CAPTURE, so this
 * extender only sets that mode; the JPEG comes straight from the HAL.
 *
 * @hide
 */
public final class BokehImageCaptureExtenderImpl implements ImageCaptureExtenderImpl {
    private static final int DEFAULT_STAGE_ID = 0;
    private static final int SESSION_STAGE_ID = 101;

    private CameraCharacteristics mCharacteristics;

    /**
     * @hide
     */
    public BokehImageCaptureExtenderImpl() {
    }

    static boolean isBokehAvailable(CameraCharacteristics characteristics) {
        if (characteristics == null) {
            return false;
        }
        Capability[] caps = characteristics.get(
                CameraCharacteristics.CONTROL_AVAILABLE_EXTENDED_SCENE_MODE_CAPABILITIES);
        if (caps == null) {
            return false;
        }
        for (Capability cap : caps) {
            if (cap.getMode() == CaptureRequest.CONTROL_EXTENDED_SCENE_MODE_BOKEH_STILL_CAPTURE) {
                return true;
            }
        }
        return false;
    }

    /**
     * The output sizes of the formats that fit the bokeh mode's maximum stream size
     * (CONTROL_AVAILABLE_EXTENDED_SCENE_MODE_MAX_SIZES), or null for no limit. The HAL
     * renders bokeh stills in the sensor's binned mode, which that size covers; larger
     * sizes would only upscale it, and their sessions remosaic other stills.
     */
    static List<Pair<Integer, Size[]>> bokehResolutions(CameraCharacteristics characteristics,
            int... formats) {
        if (characteristics == null) {
            return null;
        }
        Size max = null;
        Capability[] caps = characteristics.get(
                CameraCharacteristics.CONTROL_AVAILABLE_EXTENDED_SCENE_MODE_CAPABILITIES);
        for (Capability cap : caps != null ? caps : new Capability[0]) {
            if (cap.getMode() == CaptureRequest.CONTROL_EXTENDED_SCENE_MODE_BOKEH_STILL_CAPTURE) {
                max = cap.getMaxStreamingSize();
            }
        }
        StreamConfigurationMap map =
                characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
        if (max == null || max.getWidth() <= 0 || max.getHeight() <= 0 || map == null) {
            return null;
        }
        List<Pair<Integer, Size[]>> resolutions = new ArrayList<>();
        for (int format : formats) {
            Size[] sizes = map.getOutputSizes(format);
            if (sizes == null) {
                continue;
            }
            List<Size> fitting = new ArrayList<>();
            for (Size size : sizes) {
                if (size.getWidth() <= max.getWidth() && size.getHeight() <= max.getHeight()) {
                    fitting.add(size);
                }
            }
            resolutions.add(new Pair<>(format, fitting.toArray(new Size[0])));
        }
        return resolutions;
    }

    static SettableCaptureStage bokehStage(int id) {
        SettableCaptureStage stage = new SettableCaptureStage(id);
        stage.addCaptureRequestParameters(CaptureRequest.CONTROL_MODE,
                CaptureRequest.CONTROL_MODE_USE_EXTENDED_SCENE_MODE);
        stage.addCaptureRequestParameters(CaptureRequest.CONTROL_EXTENDED_SCENE_MODE,
                CaptureRequest.CONTROL_EXTENDED_SCENE_MODE_BOKEH_STILL_CAPTURE);
        return stage;
    }

    static List<CaptureRequest.Key> requestKeys() {
        return Arrays.asList(
                CaptureRequest.CONTROL_AF_MODE,
                CaptureRequest.CONTROL_AF_TRIGGER,
                CaptureRequest.CONTROL_AF_REGIONS,
                CaptureRequest.CONTROL_AE_REGIONS,
                CaptureRequest.CONTROL_AE_PRECAPTURE_TRIGGER,
                CaptureRequest.CONTROL_AE_EXPOSURE_COMPENSATION,
                CaptureRequest.FLASH_MODE,
                CaptureRequest.JPEG_QUALITY,
                CaptureRequest.JPEG_ORIENTATION);
    }

    static List<CaptureResult.Key> resultKeys() {
        return Arrays.asList(
                CaptureResult.CONTROL_AF_MODE,
                CaptureResult.CONTROL_AF_STATE,
                CaptureResult.CONTROL_AF_TRIGGER,
                CaptureResult.CONTROL_AF_REGIONS,
                CaptureResult.CONTROL_AE_STATE,
                CaptureResult.CONTROL_AE_REGIONS,
                CaptureResult.CONTROL_AE_PRECAPTURE_TRIGGER,
                CaptureResult.CONTROL_AE_EXPOSURE_COMPENSATION,
                CaptureResult.FLASH_MODE,
                CaptureResult.FLASH_STATE,
                CaptureResult.JPEG_QUALITY,
                CaptureResult.JPEG_ORIENTATION,
                CaptureResult.SENSOR_TIMESTAMP);
    }

    /**
     * @hide
     */
    @Override
    public void init(String cameraId, CameraCharacteristics cameraCharacteristics) {
        mCharacteristics = cameraCharacteristics;
    }

    /**
     * @hide
     */
    @Override
    public boolean isExtensionAvailable(String cameraId,
            CameraCharacteristics cameraCharacteristics) {
        return isBokehAvailable(cameraCharacteristics);
    }

    /**
     * @hide
     */
    @Override
    public List<CaptureStageImpl> getCaptureStages() {
        List<CaptureStageImpl> stages = new ArrayList<>();
        stages.add(bokehStage(DEFAULT_STAGE_ID));
        return stages;
    }

    /**
     * @hide
     */
    @Override
    public CaptureProcessorImpl getCaptureProcessor() {
        // the HAL output is final
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
        // session parameters: the HAL starts the depth camera with the session
        return bokehStage(SESSION_STAGE_ID);
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
    public int getMaxCaptureStage() {
        return 1;
    }

    /**
     * @hide
     */
    @Override
    public List<Pair<Integer, Size[]>> getSupportedResolutions() {
        return bokehResolutions(mCharacteristics, ImageFormat.JPEG, ImageFormat.YUV_420_888);
    }

    /**
     * @hide
     */
    @Override
    public List<Pair<Integer, Size[]>> getSupportedPostviewResolutions(Size captureSize) {
        return null;
    }

    /**
     * @hide
     */
    @Override
    public Range<Long> getEstimatedCaptureLatencyRange(Size captureOutputSize) {
        return new Range<>(1500L, 3500L);
    }

    /**
     * @hide
     */
    @Override
    public List<CaptureRequest.Key> getAvailableCaptureRequestKeys() {
        return requestKeys();
    }

    /**
     * @hide
     */
    @Override
    public List<CaptureResult.Key> getAvailableCaptureResultKeys() {
        return resultKeys();
    }

    /**
     * @hide
     */
    @Override
    public int onSessionType() {
        return SessionConfiguration.SESSION_REGULAR;
    }

    /**
     * @hide
     */
    @Override
    public boolean isCaptureProcessProgressAvailable() {
        return false;
    }

    /**
     * @hide
     */
    @Override
    public Pair<Long, Long> getRealtimeCaptureLatency() {
        return null;
    }

    /**
     * @hide
     */
    @Override
    public boolean isPostviewAvailable() {
        return false;
    }
}
