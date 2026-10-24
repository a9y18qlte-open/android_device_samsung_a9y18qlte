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
import android.graphics.Rect;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraMetadata;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.CaptureResult;
import android.hardware.camera2.TotalCaptureResult;
import android.hardware.camera2.params.SessionConfiguration;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.Image;
import android.media.ImageWriter;
import android.util.Log;
import android.util.Pair;
import android.util.Range;
import android.util.Size;
import android.view.Surface;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;

/**
 * HDR still capture on a9y18qlte.
 *
 * <p>Takes an exposure bracket ({@link HdrExposure}) and merges it with exposure fusion
 * ({@link HdrFusion}). The camera HAL's own HDR scene mode is not used: its imglib stage
 * never runs on this device.
 *
 * @hide
 */
public final class HdrImageCaptureExtenderImpl implements ImageCaptureExtenderImpl {
    private static final String TAG = "HdrImageCaptureExtender";
    private static final int FIRST_STAGE_ID = 0;

    private CameraCharacteristics mCharacteristics;

    /**
     * @hide
     */
    public HdrImageCaptureExtenderImpl() {
    }

    /** Bracketing needs manual exposure applied frame by frame. */
    static boolean isHdrAvailable(CameraCharacteristics characteristics) {
        if (characteristics == null) {
            return false;
        }
        int[] caps = characteristics.get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES);
        Integer latency = characteristics.get(CameraCharacteristics.SYNC_MAX_LATENCY);
        if (caps == null || latency == null
                || latency != CameraMetadata.SYNC_MAX_LATENCY_PER_FRAME_CONTROL) {
            return false;
        }
        for (int cap : caps) {
            if (cap == CameraMetadata.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR) {
                return true;
            }
        }
        return false;
    }

    /**
     * The output sizes of the formats that fit the active array. On the quad Bayer sensors
     * the active array is the binned size and larger outputs are remosaiced, which would be
     * done for every frame of the bracket.
     */
    static List<Pair<Integer, Size[]>> hdrResolutions(CameraCharacteristics characteristics,
            int... formats) {
        if (characteristics == null) {
            return null;
        }
        Rect active = characteristics.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE);
        StreamConfigurationMap map =
                characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
        if (active == null || map == null) {
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
                if (size.getWidth() <= active.width() && size.getHeight() <= active.height()) {
                    fitting.add(size);
                }
            }
            resolutions.add(new Pair<>(format, fitting.toArray(new Size[0])));
        }
        return resolutions;
    }

    static List<CaptureRequest.Key> requestKeys() {
        return Arrays.asList(
                CaptureRequest.CONTROL_AF_MODE,
                CaptureRequest.CONTROL_AF_TRIGGER,
                CaptureRequest.CONTROL_AF_REGIONS,
                CaptureRequest.CONTROL_AE_REGIONS,
                CaptureRequest.CONTROL_AE_EXPOSURE_COMPENSATION,
                CaptureRequest.JPEG_QUALITY,
                CaptureRequest.JPEG_ORIENTATION);
    }

    static List<CaptureResult.Key> resultKeys() {
        return Arrays.asList(
                CaptureResult.CONTROL_AF_MODE,
                CaptureResult.CONTROL_AF_STATE,
                CaptureResult.CONTROL_AF_TRIGGER,
                CaptureResult.CONTROL_AF_REGIONS,
                CaptureResult.CONTROL_AE_REGIONS,
                CaptureResult.CONTROL_AE_EXPOSURE_COMPENSATION,
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
        return isHdrAvailable(cameraCharacteristics);
    }

    /**
     * @hide
     */
    @Override
    public List<CaptureStageImpl> getCaptureStages() {
        List<CaptureStageImpl> stages = new ArrayList<>();
        SettableCaptureStage[] bracket = HdrExposure.stages(mCharacteristics, FIRST_STAGE_ID);
        for (int i = 0; i < HdrExposure.EV.length; i++) {
            // no metered preview frame yet: plain frames, which the fusion then averages
            stages.add(bracket != null ? bracket[i] : new SettableCaptureStage(FIRST_STAGE_ID + i));
        }
        return stages;
    }

    /**
     * @hide
     */
    @Override
    public CaptureProcessorImpl getCaptureProcessor() {
        return new CaptureProcessorImpl() {
            private final Object mLock = new Object();
            private ImageWriter mImageWriter;

            @Override
            public void onOutputSurface(Surface surface, int imageFormat) {
                synchronized (mLock) {
                    if (mImageWriter != null) {
                        mImageWriter.close();
                    }
                    mImageWriter = ImageWriter.newInstance(surface, 1);
                }
            }

            @Override
            public void onPostviewOutputSurface(Surface surface) {
            }

            @Override
            public void onResolutionUpdate(Size size) {
            }

            @Override
            public void onResolutionUpdate(Size size, Size postviewSize) {
            }

            @Override
            public void onImageFormatUpdate(int imageFormat) {
            }

            @Override
            public void processWithPostview(
                    Map<Integer, Pair<Image, TotalCaptureResult>> results,
                    ProcessResultImpl resultCallback, Executor executor) {
                throw new RuntimeException("The extension doesn't support postview");
            }

            @Override
            public void process(Map<Integer, Pair<Image, TotalCaptureResult>> results,
                    ProcessResultImpl resultCallback, Executor executor) {
                Pair<Image, TotalCaptureResult> first = results.get(FIRST_STAGE_ID);
                Pair<Image, TotalCaptureResult> ref =
                        results.get(FIRST_STAGE_ID + HdrExposure.REFERENCE);
                Long shutter = first != null
                        ? first.second.get(CaptureResult.SENSOR_TIMESTAMP) : null;
                if (resultCallback != null && shutter != null && ref != null) {
                    // the shutter is the first frame's, the settings the metered frame's
                    List<Pair<CaptureResult.Key, Object>> values = new ArrayList<>();
                    for (CaptureResult.Key key : resultKeys()) {
                        Object value = ref.second.get(key);
                        if (value != null) {
                            values.add(new Pair<>(key, value));
                        }
                    }
                    if (executor != null) {
                        executor.execute(() -> resultCallback.onCaptureCompleted(shutter, values));
                    } else {
                        resultCallback.onCaptureCompleted(shutter, values);
                    }
                }
                process(results);
            }

            @Override
            public void process(Map<Integer, Pair<Image, TotalCaptureResult>> results) {
                int n = HdrExposure.EV.length;
                HdrFusion.Frame[] frames = new HdrFusion.Frame[n];
                long timestamp = 0;
                try {
                    for (int i = 0; i < n; i++) {
                        Pair<Image, TotalCaptureResult> r = results.get(FIRST_STAGE_ID + i);
                        if (r == null || r.first == null) {
                            Log.w(TAG, "Bracket frame " + i + " missing");
                            return;
                        }
                        frames[i] = HdrFusion.fromImage(r.first);
                        if (i == 0) {
                            timestamp = r.first.getTimestamp();
                        }
                    }
                } finally {
                    for (Pair<Image, TotalCaptureResult> r : results.values()) {
                        if (r.first != null) {
                            r.first.close();
                        }
                    }
                }

                long start = System.currentTimeMillis();
                HdrFusion.Frame fused = HdrFusion.fuse(frames, HdrExposure.REFERENCE);
                Log.d(TAG, "Fused " + fused.width + "x" + fused.height + " in "
                        + (System.currentTimeMillis() - start) + " ms");
                synchronized (mLock) {
                    if (mImageWriter == null) {
                        return;
                    }
                    Image out = mImageWriter.dequeueInputImage();
                    HdrFusion.toImage(fused, out);
                    out.setTimestamp(timestamp);
                    mImageWriter.queueInputImage(out);
                }
            }
        };
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
    public int getMaxCaptureStage() {
        return HdrExposure.EV.length;
    }

    /**
     * @hide
     */
    @Override
    public List<Pair<Integer, Size[]>> getSupportedResolutions() {
        return hdrResolutions(mCharacteristics, ImageFormat.JPEG, ImageFormat.YUV_420_888);
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
        return new Range<>(1000L, 3500L);
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
