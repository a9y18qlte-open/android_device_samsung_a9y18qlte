/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package androidx.camera.extensions.impl;

import android.content.Context;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.params.SessionConfiguration;
import android.util.Pair;
import android.util.Size;

import java.util.List;

/**
 * Preview for the night extension on a9y18qlte: Low Light Boost auto exposure where the camera
 * has it, which the still capture burst ({@link NightExposure}) then locks.
 *
 * @hide
 */
public final class NightPreviewExtenderImpl implements PreviewExtenderImpl {
    private static final int DEFAULT_STAGE_ID = 0;

    private boolean mLowLightBoost;

    /**
     * @hide
     */
    public NightPreviewExtenderImpl() {
    }

    /**
     * @hide
     */
    @Override
    public void init(String cameraId, CameraCharacteristics cameraCharacteristics) {
        mLowLightBoost = NightExposure.hasLowLightBoost(cameraCharacteristics);
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
        SettableCaptureStage stage = new SettableCaptureStage(DEFAULT_STAGE_ID);
        if (mLowLightBoost) {
            stage.addCaptureRequestParameters(CaptureRequest.CONTROL_AE_MODE,
                    CaptureRequest.CONTROL_AE_MODE_ON_LOW_LIGHT_BOOST_BRIGHTNESS_PRIORITY);
        }
        return stage;
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
        return RequestUpdateProcessorImpls.noUpdateProcessor();
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
