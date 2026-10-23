/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <android/hardware/sensors/1.0/ISensors.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

// Not V1_0::implementation: Samsung's HAL, loaded into this process, uses it.
namespace android::hardware::sensors::V1_0::a9y18qlte {

/*
 * Samsung's sensors HAL, plus an always-on display brightness sensor for
 * SystemUI (doze_brightness_sensor_type), derived from the light sensor:
 *   1 - dark: show the always-on display at its dim level
 *   2 - bright: show it at its bright level
 * If the light sensor reports nothing soon after this sensor is enabled, it
 * reports bright, so the always-on display stays readable.
 */
class Sensors : public ISensors {
  public:
    explicit Sensors(const sp<ISensors>& hal);

    Return<void> getSensorsList(getSensorsList_cb _hidl_cb) override;
    Return<Result> setOperationMode(OperationMode mode) override;
    Return<Result> activate(int32_t sensorHandle, bool enabled) override;
    Return<void> poll(int32_t maxCount, poll_cb _hidl_cb) override;
    Return<Result> batch(int32_t sensorHandle, int64_t samplingPeriodNs,
                         int64_t maxReportLatencyNs) override;
    Return<Result> flush(int32_t sensorHandle) override;
    Return<Result> injectSensorData(const Event& event) override;
    Return<void> registerDirectChannel(const SharedMemInfo& mem,
                                       registerDirectChannel_cb _hidl_cb) override;
    Return<Result> unregisterDirectChannel(int32_t channelHandle) override;
    Return<void> configDirectReport(int32_t sensorHandle, int32_t channelHandle, RateLevel rate,
                                    configDirectReport_cb _hidl_cb) override;

  private:
    enum Level : int { kUnknown = 0, kDark = 1, kBright = 2 };

    void pollHal();
    void onLight(const Event& event);
    void setLevel(Level level, int64_t timestamp);
    void stopFallback();
    Result updateLight();
    bool isWakeUp(const Event& event) const;
    bool wakeUpQueued() const;

    sp<ISensors> mHal;
    std::vector<SensorInfo> mSensors;
    std::unordered_map<int32_t, uint32_t> mFlags;
    int32_t mLightHandle = -1;
    int32_t mAodHandle = -1;

    std::mutex mLock;
    std::condition_variable mCond;
    uint64_t mPollGeneration = 0;
    std::deque<Event> mEvents;
    std::vector<SensorInfo> mDynamicSensors;
    bool mWakeLockHeld = false;

    // The light sensor as the framework configured it.
    bool mLightRequested = false;
    int64_t mLightPeriodNs = 0;
    int64_t mLightLatencyNs = 0;
    // The light sensor as enabled in the HAL, for the framework or for us.
    bool mLightEnabled = false;
    std::optional<float> mLux;

    bool mAodEnabled = false;
    Level mLevel = kUnknown;
    std::optional<std::chrono::steady_clock::time_point> mFallbackDeadline;

    std::thread mPollThread;
};

}  // namespace android::hardware::sensors::V1_0::a9y18qlte
