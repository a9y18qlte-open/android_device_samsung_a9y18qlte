#include <hardware/camera.h>
#include <system/camera_metadata.h>
#include <dlfcn.h>
#include <string.h>
#include <log/log.h>

#undef LOG_TAG
#define LOG_TAG "CameraMetadataShim"

static int (*real_hw_get_module)(const char *id, const struct hw_module_t **module) = nullptr;
static int (*real_hw_get_module_by_class)(const char *class_id, const char *inst, const struct hw_module_t **module) = nullptr;
static int (*real_get_camera_info)(int, struct camera_info*) = nullptr;

static int shim_get_camera_info(int cameraId, struct camera_info *info) {
    if (!real_get_camera_info) {
        ALOGE("shim_get_camera_info: real_get_camera_info is NULL!");
        return -1;
    }

    int res = real_get_camera_info(cameraId, info);
    if (res != 0 || !info || !info->static_camera_characteristics) {
        return res;
    }

    // RULE 1: Never touch aux cameras (IDs 50, 52, 54).
    // Leave them completely untouched so OpenCamera has 100% stock behavior.
    if (cameraId != 0 && cameraId != 1) {
        return res;
    }

    camera_metadata_t *meta = const_cast<camera_metadata_t*>(info->static_camera_characteristics);
    camera_metadata_entry_t entry;

    // RULE 2: Remove PRIVATE_REPROCESSING (4) and YUV_REPROCESSING (7) from capabilities
    // This disables ZSL while preserving FULL / LEVEL_3 status and all manual controls.
    if (find_camera_metadata_entry(meta, ANDROID_REQUEST_AVAILABLE_CAPABILITIES, &entry) == 0) {
        for (size_t i = 0; i < entry.count; i++) {
            uint8_t cap = entry.data.u8[i];
            if (cap == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_PRIVATE_REPROCESSING ||
                cap == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_YUV_REPROCESSING) {
                ALOGI("Camera %d: removing capability %u (reprocessing/ZSL)", cameraId, cap);
                entry.data.u8[i] = ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE;
            }
        }
    }

    // RULE 3: Clamp oversize YUV (format 35) configurations to 4032x3024 (12MP) in-place.
    // The Qualcomm SDM660 ISP hardware cannot stream YUV at 24MP (5664x4248),
    // but handles 4032x3024 YUV effortlessly. JPEG (33) remains at full 24MP (5664x4248).
    if (find_camera_metadata_entry(meta, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry) == 0) {
        for (size_t i = 0; i + 3 < entry.count; i += 4) {
            int32_t format = entry.data.i32[i];
            int32_t width  = entry.data.i32[i + 1];
            int32_t height = entry.data.i32[i + 2];
            if (format == 35 && (width > 4032 || height > 3024)) {
                entry.data.i32[i + 1] = 4032;
                entry.data.i32[i + 2] = 3024;
            }
        }
    }

    return res;
}

static void hook_camera_module(const struct hw_module_t *module) {
    if (!module) return;
    camera_module_t *cam_module = const_cast<camera_module_t*>(reinterpret_cast<const camera_module_t*>(module));
    if (cam_module->get_camera_info && cam_module->get_camera_info != shim_get_camera_info) {
        ALOGI("Hooking cam_module->get_camera_info (%p -> %p)", cam_module->get_camera_info, shim_get_camera_info);
        real_get_camera_info = cam_module->get_camera_info;
        cam_module->get_camera_info = shim_get_camera_info;
    }
}

extern "C" int hw_get_module(const char *id, const struct hw_module_t **module) {
    if (!real_hw_get_module) {
        void *libhw = dlopen("libhardware.so", RTLD_NOW);
        if (libhw) {
            real_hw_get_module = reinterpret_cast<int (*)(const char*, const struct hw_module_t**)>(
                dlsym(libhw, "hw_get_module"));
        }
        if (!real_hw_get_module) {
            real_hw_get_module = reinterpret_cast<int (*)(const char*, const struct hw_module_t**)>(
                dlsym(RTLD_NEXT, "hw_get_module"));
        }
    }

    if (!real_hw_get_module) {
        ALOGE("Failed to find real hw_get_module via dlopen/dlsym!");
        return -1;
    }

    int res = real_hw_get_module(id, module);
    if (res == 0 && module && *module && id && strcmp(id, "camera") == 0) {
        hook_camera_module(*module);
    }
    return res;
}

extern "C" int hw_get_module_by_class(const char *class_id, const char *inst,
                                     const struct hw_module_t **module) {
    if (!real_hw_get_module_by_class) {
        void *libhw = dlopen("libhardware.so", RTLD_NOW);
        if (libhw) {
            real_hw_get_module_by_class = reinterpret_cast<int (*)(const char*, const char*, const struct hw_module_t**)>(
                dlsym(libhw, "hw_get_module_by_class"));
        }
        if (!real_hw_get_module_by_class) {
            real_hw_get_module_by_class = reinterpret_cast<int (*)(const char*, const char*, const struct hw_module_t**)>(
                dlsym(RTLD_NEXT, "hw_get_module_by_class"));
        }
    }

    if (!real_hw_get_module_by_class) {
        ALOGE("Failed to find real hw_get_module_by_class via dlopen/dlsym!");
        return -1;
    }

    int res = real_hw_get_module_by_class(class_id, inst, module);
    if (res == 0 && module && *module && class_id && strcmp(class_id, "camera") == 0) {
        hook_camera_module(*module);
    }
    return res;
}

__attribute__((constructor))
static void shim_init() {
    ALOGI("libcamera_metadata_shim initialized in PID %d", getpid());
}
