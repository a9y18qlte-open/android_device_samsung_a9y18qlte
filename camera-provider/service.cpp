/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "android.hardware.camera.provider-service.a9y18qlte"

#include "CameraProvider.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

using ::android::hardware::camera::provider::implementation::CameraProvider;

namespace {
// Default recommended RPC thread count for camera provider implementations
const int HWBINDER_THREAD_COUNT = 6;
}  // namespace

int main() {
    ALOGI("CameraProvider: samsung service is starting.");

    ABinderProcess_setThreadPoolMaxThreadCount(HWBINDER_THREAD_COUNT);
    ABinderProcess_startThreadPool();

    std::shared_ptr<CameraProvider> defaultProvider = ndk::SharedRefBase::make<CameraProvider>();
    const std::string serviceName = std::string(CameraProvider::descriptor) + "/internal/0";

    binder_exception_t ret =
            AServiceManager_addService(defaultProvider->asBinder().get(), serviceName.c_str());
    LOG_ALWAYS_FATAL_IF(ret != EX_NONE, "Error while registering camera provider service: %d", ret);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
