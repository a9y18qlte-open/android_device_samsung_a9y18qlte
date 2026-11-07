/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package androidx.camera.extensions.impl;

import android.hardware.camera2.CaptureRequest;
import android.util.Pair;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

final class SettableCaptureStage implements CaptureStageImpl {
    private final int mId;

    private Map<CaptureRequest.Key, Object> mCaptureRequestKeyValueMap = new HashMap<>();

    /**
     * Constructor for a {@link CaptureStageImpl} with specific identifier.
     *
     * <p>After this {@link CaptureStageImpl} is applied to a single capture operation,
     * developers can
     * retrieve the {@link android.media.Image} object with the identifier.
     *
     * @param id The identifier for the {@link CaptureStageImpl}.
     */
    SettableCaptureStage(int id) {
        mId = id;
    }

    /**
     * @hide
     */
    /** Returns the identifier for the {@link CaptureStageImpl}. */
    @Override
    public int getId() {
        return mId;
    }

    /**
     * @hide
     */
    @Override
    public List<Pair<CaptureRequest.Key, Object>> getParameters() {
        List<Pair<CaptureRequest.Key, Object>> parameters = new ArrayList<>();

        for (Map.Entry<CaptureRequest.Key, Object> entry : mCaptureRequestKeyValueMap.entrySet()) {
            parameters.add(Pair.create(entry.getKey(), entry.getValue()));
        }

        return parameters;
    }

    /**
     * Adds necessary {@link CaptureRequest.Key} settings into the {@link CaptureStageImpl} object.
     */
    <T> void addCaptureRequestParameters(CaptureRequest.Key<T> key, T value) {
        mCaptureRequestKeyValueMap.put(key, value);
    }
}
