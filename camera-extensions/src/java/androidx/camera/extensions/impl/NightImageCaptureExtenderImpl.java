/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package androidx.camera.extensions.impl;

import android.content.Context;
import android.graphics.ImageFormat;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.CaptureResult;
import android.hardware.camera2.TotalCaptureResult;
import android.hardware.camera2.params.SessionConfiguration;
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
 * Night still capture on a9y18qlte: a burst of frames with auto exposure locked
 * ({@link NightExposure})
 * merged by {@link NightMerge}.
 *
 * @hide
 */
public final class NightImageCaptureExtenderImpl implements ImageCaptureExtenderImpl {
    private static final String TAG = "NightImageCaptureExtender";
    private static final int FIRST_STAGE_ID = 0;

    private CameraCharacteristics mCharacteristics;

    /**
     * @hide
     */
    public NightImageCaptureExtenderImpl() {
    }

    static List<CaptureResult.Key> resultKeys() {
        List<CaptureResult.Key> keys = new ArrayList<>(HdrImageCaptureExtenderImpl.resultKeys());
        keys.add(CaptureResult.EXTENSION_NIGHT_MODE_INDICATOR);
        return keys;
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
        return HdrImageCaptureExtenderImpl.isHdrAvailable(cameraCharacteristics);
    }

    /**
     * @hide
     */
    @Override
    public List<CaptureStageImpl> getCaptureStages() {
        return new ArrayList<>(Arrays.asList(
                NightExposure.stages(mCharacteristics, FIRST_STAGE_ID)));
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
                Pair<Image, TotalCaptureResult> ref =
                        results.get(FIRST_STAGE_ID + NightExposure.REFERENCE);
                Long shutter = ref != null
                        ? ref.second.get(CaptureResult.SENSOR_TIMESTAMP) : null;
                if (resultCallback != null && shutter != null) {
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
                int n = NightExposure.FRAMES;
                HdrFusion.Frame[] frames = new HdrFusion.Frame[n];
                long timestamp = 0;
                try {
                    for (int i = 0; i < n; i++) {
                        Pair<Image, TotalCaptureResult> r = results.get(FIRST_STAGE_ID + i);
                        if (r == null || r.first == null) {
                            Log.w(TAG, "Burst frame " + i + " missing");
                            return;
                        }
                        frames[i] = HdrFusion.fromImage(r.first);
                        Log.d(TAG, "Frame " + i + ": exposure "
                                + r.second.get(CaptureResult.SENSOR_EXPOSURE_TIME) + " ns, iso "
                                + r.second.get(CaptureResult.SENSOR_SENSITIVITY) + ", af mode "
                                + r.second.get(CaptureResult.CONTROL_AF_MODE) + " state "
                                + r.second.get(CaptureResult.CONTROL_AF_STATE) + ", focus "
                                + r.second.get(CaptureResult.LENS_FOCUS_DISTANCE));
                        if (i == NightExposure.REFERENCE) {
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
                HdrFusion.Frame merged = NightMerge.merge(frames);
                Log.d(TAG, "Merged " + n + " frames of " + merged.width + "x" + merged.height
                        + " in " + (System.currentTimeMillis() - start) + " ms");
                synchronized (mLock) {
                    if (mImageWriter == null) {
                        return;
                    }
                    Image out = mImageWriter.dequeueInputImage();
                    HdrFusion.toImage(merged, out);
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
        return NightExposure.FRAMES;
    }

    /**
     * @hide
     */
    @Override
    public List<Pair<Integer, Size[]>> getSupportedResolutions() {
        return HdrImageCaptureExtenderImpl.hdrResolutions(mCharacteristics, ImageFormat.JPEG,
                ImageFormat.YUV_420_888);
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
        return new Range<>(1500L, 4500L);
    }

    /**
     * @hide
     */
    @Override
    public List<CaptureRequest.Key> getAvailableCaptureRequestKeys() {
        return HdrImageCaptureExtenderImpl.requestKeys();
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
