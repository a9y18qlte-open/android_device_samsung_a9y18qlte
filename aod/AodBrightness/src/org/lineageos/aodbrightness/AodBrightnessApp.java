/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.aodbrightness;

import android.app.Application;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.hardware.display.AmbientDisplayConfiguration;
import android.os.Handler;
import android.os.UserHandle;
import android.util.Log;

import java.io.FileWriter;
import java.io.IOException;

/**
 * Picks the panel's always-on display brightness from ambient light, as Samsung's
 * AOD service does on stock.
 *
 * While the always-on display shows, the panel runs in its own low-power mode and
 * ignores the backlight; its brightness is one of two levels selected through
 * /sys/class/lcd/panel/alpm. Watch the light sensor while the screen is off and
 * use the dim level in the dark. Without a light sensor, or when it reports
 * nothing, stay at the bright level so the always-on display stays readable.
 */
public class AodBrightnessApp extends Application implements SensorEventListener {
    private static final String TAG = "AodBrightness";

    private static final String ALPM_PATH = "/sys/class/lcd/panel/alpm";
    // Values for the alpm node: the kernel maps them to its AOD_MODE_* modes.
    private static final String HLPM_2NIT = "2";
    private static final String HLPM_60NIT = "4";

    // Hysteresis, so light around a single threshold doesn't flip the level.
    private static final float DARK_LUX = 5f;
    private static final float BRIGHT_LUX = 20f;

    // How long to wait for a first reading before falling back to the bright level.
    private static final long SENSOR_TIMEOUT_MS = 3000;

    private final Handler mHandler = new Handler();

    private AmbientDisplayConfiguration mAmbientConfig;
    private SensorManager mSensorManager;
    private Sensor mLightSensor;
    private boolean mListening;
    private String mMode;

    private final Runnable mSensorTimeout = () -> {
        Log.i(TAG, "No light sensor reading, using the bright level");
        setMode(HLPM_60NIT);
    };

    private final BroadcastReceiver mScreenReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (Intent.ACTION_SCREEN_OFF.equals(intent.getAction())) {
                onScreenOff();
            } else {
                stopListening();
            }
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();

        mAmbientConfig = new AmbientDisplayConfiguration(this);
        mSensorManager = getSystemService(SensorManager.class);
        mLightSensor = mSensorManager.getDefaultSensor(Sensor.TYPE_LIGHT);
        if (mLightSensor == null) {
            Log.i(TAG, "No light sensor, the always-on display stays at the bright level");
        }

        setMode(HLPM_60NIT);

        IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_SCREEN_OFF);
        filter.addAction(Intent.ACTION_SCREEN_ON);
        registerReceiver(mScreenReceiver, filter);
    }

    private void onScreenOff() {
        if (mLightSensor == null || mListening
                || !mAmbientConfig.alwaysOnEnabled(UserHandle.USER_CURRENT)) {
            return;
        }
        mListening = true;
        mSensorManager.registerListener(this, mLightSensor, SensorManager.SENSOR_DELAY_NORMAL);
        mHandler.postDelayed(mSensorTimeout, SENSOR_TIMEOUT_MS);
    }

    private void stopListening() {
        if (!mListening) {
            return;
        }
        mListening = false;
        mHandler.removeCallbacks(mSensorTimeout);
        mSensorManager.unregisterListener(this);
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        mHandler.removeCallbacks(mSensorTimeout);
        float lux = event.values[0];
        if (lux <= DARK_LUX) {
            setMode(HLPM_2NIT);
        } else if (lux >= BRIGHT_LUX) {
            setMode(HLPM_60NIT);
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
    }

    private void setMode(String mode) {
        if (mode.equals(mMode)) {
            return;
        }
        try (FileWriter writer = new FileWriter(ALPM_PATH)) {
            writer.write(mode);
            mMode = mode;
            Log.d(TAG, "alpm=" + mode);
        } catch (IOException e) {
            Log.e(TAG, "Failed to write " + ALPM_PATH, e);
        }
    }
}
