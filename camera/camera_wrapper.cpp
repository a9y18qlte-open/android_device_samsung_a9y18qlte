/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Camera HAL module wrapper, loaded by the Samsung camera provider as
 * camera.unihal.default (SEC_CAMERA_HARDWARE_MODULE_ID).
 *
 * Loads the stock camera HAL (camera.sdm660.so), forwards every module
 * method to it and adjusts the static metadata of the main cameras:
 *
 * - PRIVATE_REPROCESSING and YUV_REPROCESSING are removed, which disables ZSL
 *   while keeping the FULL / LEVEL_3 hardware level and manual controls.
 * - YUV (format 35) stream configurations above 4032x3024 are clamped to
 *   4032x3024: the SDM660 ISP cannot stream YUV at 24MP. JPEG stays at full
 *   resolution.
 *
 * Samsung's camera_module_t has set_torch_mode_strength() right after
 * notify_device_state_change(), where AOSP has reserved[0].
 */

#define LOG_TAG "CameraWrapper"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>

#include <android-base/file.h>
#include <cutils/properties.h>
#include <hardware/camera_common.h>
#include <hardware/hardware.h>
#include <log/log.h>
#include <system/camera_metadata.h>

#define STOCK_HAL_PATH "/vendor/lib/hw/camera.sdm660.so"

// Set by the FlashControl flashlight HAL (FlashControl/).
#define TORCH_LEVEL_PROP "persist.ext.flashlight.last_brightness"
#define TORCH_MAX_LEVEL 5
#define TORCH_NODE "/sys/class/camera/flash/rear_flash"

typedef int (*set_torch_mode_strength_t)(const char* camera_id, bool enabled, int32_t strength);

static camera_module_t* gStock = nullptr;
static pthread_once_t gLoadOnce = PTHREAD_ONCE_INIT;
static bool gTorchByStrength = false;
static const camera_module_callbacks_t* gCallbacks = nullptr;

static void load_stock_hal() {
    void* handle = dlopen(STOCK_HAL_PATH, RTLD_NOW);
    if (!handle) {
        ALOGE("Could not load %s: %s", STOCK_HAL_PATH, dlerror());
        return;
    }

    gStock = static_cast<camera_module_t*>(dlsym(handle, HAL_MODULE_INFO_SYM_AS_STR));
    if (!gStock) {
        ALOGE("Could not find %s in %s", HAL_MODULE_INFO_SYM_AS_STR, STOCK_HAL_PATH);
        dlclose(handle);
        return;
    }

    ALOGI("Loaded stock camera HAL \"%s\" (module API %x)", gStock->common.name,
          gStock->common.module_api_version);
}

static bool stock() {
    pthread_once(&gLoadOnce, load_stock_hal);
    return gStock != nullptr;
}

static void fixup_static_metadata(int camera_id, camera_metadata_t* meta) {
    camera_metadata_entry_t entry;

    if (find_camera_metadata_entry(meta, ANDROID_REQUEST_AVAILABLE_CAPABILITIES, &entry) == 0) {
        for (size_t i = 0; i < entry.count; i++) {
            uint8_t cap = entry.data.u8[i];
            if (cap == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_PRIVATE_REPROCESSING ||
                cap == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_YUV_REPROCESSING) {
                ALOGI("Camera %d: removing capability %u (reprocessing/ZSL)", camera_id, cap);
                entry.data.u8[i] = ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE;
            }
        }
    }

    if (find_camera_metadata_entry(meta, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry) ==
        0) {
        for (size_t i = 0; i + 3 < entry.count; i += 4) {
            int32_t format = entry.data.i32[i];
            int32_t width = entry.data.i32[i + 1];
            int32_t height = entry.data.i32[i + 2];
            if (format == HAL_PIXEL_FORMAT_YCbCr_420_888 && (width > 4032 || height > 3024)) {
                entry.data.i32[i + 1] = 4032;
                entry.data.i32[i + 2] = 3024;
            }
        }
    }
}

static int camera_get_number_of_cameras() {
    if (!stock()) return 0;
    return gStock->get_number_of_cameras();
}

static int camera_get_camera_info(int camera_id, struct camera_info* info) {
    if (!stock()) return -ENODEV;

    int res = gStock->get_camera_info(camera_id, info);
    if (res != 0 || !info || !info->static_camera_characteristics) return res;

    // Only the main cameras; the aux cameras (50, 52, 54) keep stock metadata.
    if (camera_id == 0 || camera_id == 1) {
        fixup_static_metadata(camera_id, const_cast<camera_metadata_t*>(
                                                 info->static_camera_characteristics));
    }
    return res;
}

static int camera_set_callbacks(const camera_module_callbacks_t* callbacks) {
    if (!stock() || !gStock->set_callbacks) return -ENOSYS;
    gCallbacks = callbacks;
    return gStock->set_callbacks(callbacks);
}

static void camera_get_vendor_tag_ops(vendor_tag_ops_t* ops) {
    if (stock() && gStock->get_vendor_tag_ops) gStock->get_vendor_tag_ops(ops);
}

static int camera_open_legacy(const struct hw_module_t* /* module */, const char* id,
                              uint32_t halVersion, struct hw_device_t** device) {
    if (!stock() || !gStock->open_legacy) return -ENOSYS;
    return gStock->open_legacy(&gStock->common, id, halVersion, device);
}

static int camera_set_torch_mode(const char* camera_id, bool enabled) {
    if (!stock() || !gStock->set_torch_mode) return -ENOSYS;

    // Turn the torch on at the brightness picked in FlashControl: the plain
    // set_torch_mode() lights it at the default current first, which shows as
    // a flash before FlashControl lowers it.
    auto set_strength = reinterpret_cast<set_torch_mode_strength_t>(gStock->reserved[0]);
    if (set_strength) {
        if (enabled) {
            int32_t level = property_get_int32(TORCH_LEVEL_PROP, 0);
            if (level >= 1 && level <= TORCH_MAX_LEVEL) {
                int res = set_strength(camera_id, true, level);
                if (res == 0) gTorchByStrength = true;
                return res;
            }
        } else if (gTorchByStrength) {
            // Neither set_torch_mode_strength(false) nor set_torch_mode(false)
            // turn off a torch lit by strength: switch the LED off directly and
            // report it like the stock HAL does for its own torch.
            gTorchByStrength = false;
            if (!android::base::WriteStringToFile("0", TORCH_NODE)) return -errno;
            if (gCallbacks && gCallbacks->torch_mode_status_change) {
                gCallbacks->torch_mode_status_change(gCallbacks, camera_id,
                                                     TORCH_MODE_STATUS_AVAILABLE_OFF);
            }
            return 0;
        }
    }
    return gStock->set_torch_mode(camera_id, enabled);
}

static int camera_set_torch_mode_strength(const char* camera_id, bool enabled, int32_t strength) {
    if (!stock()) return -ENOSYS;
    auto fn = reinterpret_cast<set_torch_mode_strength_t>(gStock->reserved[0]);
    if (!fn) return -ENOSYS;
    return fn(camera_id, enabled, strength);
}

static int camera_device_open(const struct hw_module_t* /* module */, const char* id,
                              struct hw_device_t** device) {
    if (!stock()) return -ENODEV;
    return gStock->common.methods->open(&gStock->common, id, device);
}

static struct hw_module_methods_t camera_module_methods = {
        .open = camera_device_open,
};

camera_module_t HAL_MODULE_INFO_SYM __attribute__((visibility("default"))) = {
        .common =
                {
                        .tag = HARDWARE_MODULE_TAG,
                        .module_api_version = CAMERA_MODULE_API_VERSION_2_4,
                        .hal_api_version = HARDWARE_HAL_API_VERSION,
                        .id = "camera.unihal",
                        .name = "a9y18qlte camera wrapper",
                        .author = "The LineageOS Project",
                        .methods = &camera_module_methods,
                        .dso = nullptr,
                        .reserved = {0},
                },
        .get_number_of_cameras = camera_get_number_of_cameras,
        .get_camera_info = camera_get_camera_info,
        .set_callbacks = camera_set_callbacks,
        .get_vendor_tag_ops = camera_get_vendor_tag_ops,
        .open_legacy = camera_open_legacy,
        .set_torch_mode = camera_set_torch_mode,
        .init = nullptr,
        .get_physical_camera_info = nullptr,
        .is_stream_combination_supported = nullptr,
        .notify_device_state_change = nullptr,
        .reserved = {reinterpret_cast<void*>(camera_set_torch_mode_strength), nullptr},
};
