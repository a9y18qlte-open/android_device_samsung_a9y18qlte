/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "android.hardware.sensors@1.0-service.a9y18qlte"

#include "Sensors.h"

#include <hardware_legacy/power.h>
#include <log/log.h>
#include <utils/SystemClock.h>

#include <algorithm>

namespace android::hardware::sensors::V1_0::a9y18qlte {

using std::chrono::steady_clock;

namespace {

constexpr char kAodSensorName[] = "AOD Brightness";
constexpr char kAodSensorStringType[] = "org.lineageos.sensor.aod_brightness";
constexpr SensorType kAodSensorType = SensorType::DEVICE_PRIVATE_BASE;

// Hysteresis, so light around a single threshold doesn't flip the level.
constexpr float kDarkLux = 5.0f;
constexpr float kBrightLux = 20.0f;

// Light sensor rate while only the always-on display uses it (SENSOR_DELAY_NORMAL).
constexpr int64_t kAodLightPeriodNs = 200000000;

// How long to wait for a light sensor reading before falling back to bright.
constexpr auto kFallbackTimeout = std::chrono::seconds(1);

constexpr int32_t kHalPollMaxCount = 128;

// Held while wake-up events wait in the queue for the framework.
constexpr char kEventsWakeLock[] = "sensors_hal_events";
// Held while waiting for the first light sensor reading.
constexpr char kFallbackWakeLock[] = "sensors_hal_aod_fallback";

}  // namespace

Sensors::Sensors(const sp<ISensors>& hal) : mHal(hal) {
    mHal->getSensorsList([this](const hidl_vec<SensorInfo>& list) { mSensors = list; });

    int32_t maxHandle = 0;
    const SensorInfo* light = nullptr;
    for (const auto& sensor : mSensors) {
        mFlags[sensor.sensorHandle] = sensor.flags;
        maxHandle = std::max(maxHandle, sensor.sensorHandle);
        if (!light && sensor.type == SensorType::LIGHT) {
            light = &sensor;
        }
    }

    // Without a light sensor, the AOD brightness sensor always falls back to bright.
    if (light) {
        mLightHandle = light->sensorHandle;
    } else {
        ALOGW("No light sensor, the AOD brightness sensor reports bright");
    }
    mAodHandle = maxHandle + 1;

    SensorInfo aod = {};
    aod.sensorHandle = mAodHandle;
    aod.name = kAodSensorName;
    aod.vendor = "The LineageOS Project";
    aod.version = 1;
    aod.type = kAodSensorType;
    aod.typeAsString = kAodSensorStringType;
    aod.maxRange = kBright;
    aod.resolution = 1.0f;
    aod.power = light ? light->power : 0.0f;
    aod.minDelay = 0;
    aod.maxDelay = light ? light->maxDelay : 0;
    aod.flags = static_cast<uint32_t>(SensorFlagBits::ON_CHANGE_MODE);
    mFlags[mAodHandle] = aod.flags;
    mSensors.push_back(aod);

    mPollThread = std::thread(&Sensors::pollHal, this);
}

Return<void> Sensors::getSensorsList(getSensorsList_cb _hidl_cb) {
    _hidl_cb(mSensors);
    return Void();
}

Return<Result> Sensors::setOperationMode(OperationMode mode) {
    return mHal->setOperationMode(mode);
}

Return<Result> Sensors::activate(int32_t sensorHandle, bool enabled) {
    std::lock_guard<std::mutex> lock(mLock);

    if (sensorHandle == mLightHandle) {
        mLightRequested = enabled;
        return updateLight();
    }
    if (sensorHandle != mAodHandle) {
        return mHal->activate(sensorHandle, enabled);
    }

    if (enabled == mAodEnabled) {
        return Result::OK;
    }
    mAodEnabled = enabled;
    mLevel = kUnknown;
    stopFallback();

    Result result = updateLight();
    if (enabled) {
        if (mLux) {
            // The light sensor already runs: report its last reading right away.
            Event event = {};
            event.u.scalar = *mLux;
            event.timestamp = elapsedRealtimeNano();
            onLight(event);
        } else {
            mFallbackDeadline = steady_clock::now() + kFallbackTimeout;
            acquire_wake_lock(PARTIAL_WAKE_LOCK, kFallbackWakeLock);
        }
    }
    mCond.notify_all();
    return result;
}

Return<void> Sensors::poll(int32_t maxCount, poll_cb _hidl_cb) {
    std::unique_lock<std::mutex> lock(mLock);

    // A single client polls, so a new poll means an earlier one is stale: left
    // blocked by a system_server that died. Make it return, which frees its
    // binder thread. The framework polls for 0 events when it connects.
    uint64_t generation = ++mPollGeneration;
    mCond.notify_all();
    if (maxCount <= 0) {
        lock.unlock();
        _hidl_cb(Result::BAD_VALUE, {}, {});
        return Void();
    }

    // The framework took the events from the last poll, wake-up ones included.
    if (mWakeLockHeld && !wakeUpQueued()) {
        release_wake_lock(kEventsWakeLock);
        mWakeLockHeld = false;
    }

    while (mEvents.empty() && mDynamicSensors.empty() && generation == mPollGeneration) {
        if (!mFallbackDeadline) {
            mCond.wait(lock);
        } else if (mCond.wait_until(lock, *mFallbackDeadline) == std::cv_status::timeout &&
                   mFallbackDeadline && steady_clock::now() >= *mFallbackDeadline) {
            ALOGI("No light sensor reading, reporting bright");
            stopFallback();
            setLevel(kBright, elapsedRealtimeNano());
        }
    }
    if (generation != mPollGeneration) {
        lock.unlock();
        _hidl_cb(Result::OK, {}, {});
        return Void();
    }

    size_t count = std::min(mEvents.size(), static_cast<size_t>(maxCount));
    std::vector<Event> events(mEvents.begin(), mEvents.begin() + count);
    mEvents.erase(mEvents.begin(), mEvents.begin() + count);
    std::vector<SensorInfo> dynamicSensors;
    dynamicSensors.swap(mDynamicSensors);

    lock.unlock();
    _hidl_cb(Result::OK, events, dynamicSensors);
    return Void();
}

Return<Result> Sensors::batch(int32_t sensorHandle, int64_t samplingPeriodNs,
                              int64_t maxReportLatencyNs) {
    std::lock_guard<std::mutex> lock(mLock);

    if (sensorHandle == mLightHandle) {
        mLightPeriodNs = samplingPeriodNs;
        mLightLatencyNs = maxReportLatencyNs;
        if (!mLightRequested && !mAodEnabled) {
            return mHal->batch(sensorHandle, samplingPeriodNs, maxReportLatencyNs);
        }
        return updateLight();
    }
    if (sensorHandle == mAodHandle && mAodHandle >= 0) {
        return Result::OK;
    }
    return mHal->batch(sensorHandle, samplingPeriodNs, maxReportLatencyNs);
}

Return<Result> Sensors::flush(int32_t sensorHandle) {
    if (sensorHandle != mAodHandle || mAodHandle < 0) {
        return mHal->flush(sensorHandle);
    }

    std::lock_guard<std::mutex> lock(mLock);
    if (!mAodEnabled) {
        return Result::BAD_VALUE;
    }
    Event event = {};
    event.timestamp = 0;
    event.sensorHandle = mAodHandle;
    event.sensorType = SensorType::META_DATA;
    event.u.meta.what = MetaDataEventType::META_DATA_FLUSH_COMPLETE;
    mEvents.push_back(event);
    mCond.notify_all();
    return Result::OK;
}

Return<Result> Sensors::injectSensorData(const Event& event) {
    return mHal->injectSensorData(event);
}

Return<void> Sensors::registerDirectChannel(const SharedMemInfo& mem,
                                            registerDirectChannel_cb _hidl_cb) {
    return mHal->registerDirectChannel(mem, _hidl_cb);
}

Return<Result> Sensors::unregisterDirectChannel(int32_t channelHandle) {
    return mHal->unregisterDirectChannel(channelHandle);
}

Return<void> Sensors::configDirectReport(int32_t sensorHandle, int32_t channelHandle,
                                         RateLevel rate, configDirectReport_cb _hidl_cb) {
    if (sensorHandle == mAodHandle && mAodHandle >= 0) {
        _hidl_cb(Result::INVALID_OPERATION, -1);
        return Void();
    }
    return mHal->configDirectReport(sensorHandle, channelHandle, rate, _hidl_cb);
}

/*
 * Samsung's HAL blocks in poll until it has events. Poll it from a thread of our
 * own, so the fallback can still report while the light sensor stays silent.
 */
void Sensors::pollHal() {
    for (;;) {
        Result halResult = Result::OK;
        mHal->poll(kHalPollMaxCount, [&](Result result, const hidl_vec<Event>& events,
                                         const hidl_vec<SensorInfo>& dynamicSensors) {
            halResult = result;
            if (result != Result::OK) {
                return;
            }

            std::lock_guard<std::mutex> lock(mLock);
            for (const auto& event : events) {
                if (event.sensorHandle == mLightHandle && event.sensorType == SensorType::LIGHT) {
                    mLux = event.u.scalar;
                    onLight(event);
                    if (!mLightRequested) {
                        continue;
                    }
                }
                if (isWakeUp(event) && !mWakeLockHeld) {
                    acquire_wake_lock(PARTIAL_WAKE_LOCK, kEventsWakeLock);
                    mWakeLockHeld = true;
                }
                mEvents.push_back(event);
            }
            for (const auto& sensor : dynamicSensors) {
                mFlags[sensor.sensorHandle] = sensor.flags;
                mDynamicSensors.push_back(sensor);
            }
            mCond.notify_all();
        });

        if (halResult != Result::OK) {
            ALOGE("HAL poll failed: %d", static_cast<int>(halResult));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// Called with mLock held.
void Sensors::onLight(const Event& event) {
    if (!mAodEnabled) {
        return;
    }
    stopFallback();

    float lux = event.u.scalar;
    Level level = mLevel;
    if (lux <= kDarkLux) {
        level = kDark;
    } else if (lux >= kBrightLux || level == kUnknown) {
        level = kBright;
    }
    setLevel(level, event.timestamp);
}

// Called with mLock held.
void Sensors::setLevel(Level level, int64_t timestamp) {
    if (level == mLevel) {
        return;
    }
    mLevel = level;

    Event event = {};
    event.timestamp = timestamp;
    event.sensorHandle = mAodHandle;
    event.sensorType = kAodSensorType;
    event.u.data[0] = level;
    mEvents.push_back(event);
    mCond.notify_all();
}

// Called with mLock held.
void Sensors::stopFallback() {
    if (mFallbackDeadline) {
        mFallbackDeadline.reset();
        release_wake_lock(kFallbackWakeLock);
    }
}

// Enable the light sensor for the framework or the always-on display. Called with mLock held.
Result Sensors::updateLight() {
    if (mLightHandle < 0) {
        return Result::OK;
    }
    bool enable = mLightRequested || mAodEnabled;

    if (enable) {
        int64_t periodNs = mLightRequested ? mLightPeriodNs : kAodLightPeriodNs;
        int64_t latencyNs = mLightRequested ? mLightLatencyNs : 0;
        if (mAodEnabled) {
            periodNs = std::min(periodNs, kAodLightPeriodNs);
            latencyNs = 0;
        }
        mHal->batch(mLightHandle, periodNs, latencyNs);
    }

    if (enable == mLightEnabled) {
        return Result::OK;
    }
    Result result = mHal->activate(mLightHandle, enable);
    if (result == Result::OK) {
        mLightEnabled = enable;
        if (!enable) {
            mLux.reset();
        }
    }
    return result;
}

bool Sensors::isWakeUp(const Event& event) const {
    auto it = mFlags.find(event.sensorHandle);
    return it != mFlags.end() && (it->second & static_cast<uint32_t>(SensorFlagBits::WAKE_UP));
}

// Called with mLock held.
bool Sensors::wakeUpQueued() const {
    return std::any_of(mEvents.begin(), mEvents.end(),
                       [this](const Event& event) { return isWakeUp(event); });
}

}  // namespace android::hardware::sensors::V1_0::a9y18qlte
