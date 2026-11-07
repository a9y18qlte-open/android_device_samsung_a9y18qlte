/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
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
