/*
 * Copyright (C) 2016 The Android Open Source Project
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

#define LOG_TAG "android.hardware.sensors@1.0-service"

#include <android/hardware/sensors/1.0/ISensors.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

#include "Sensors.h"

using android::sp;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::sensors::V1_0::ISensors;
using android::hardware::sensors::V1_0::a9y18qlte::Sensors;

int main() {
    /* Sensors framework service needs at least two threads.
     * One thread blocks on a "poll"
     * The second thread is needed for all other HAL methods.
     */
    configureRpcThreadpool(2, true /* callerWillJoin */);

    // Samsung's HAL, loaded into this process
    sp<ISensors> hal = ISensors::getService("default", true /* getStub */);
    if (hal == nullptr) {
        ALOGE("Could not load the sensors HAL");
        return 1;
    }

    sp<ISensors> sensors = new Sensors(hal);
    if (sensors->registerAsService() != android::OK) {
        ALOGE("Could not register the sensors HAL");
        return 1;
    }

    joinRpcThreadpool();
    return 1;
}
