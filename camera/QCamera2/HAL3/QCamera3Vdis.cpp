/*
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

#define LOG_TAG "QCamera3Vdis"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include <log/log.h>

#include "QCamera3Vdis.h"

using namespace android;

namespace qcamera {

namespace {

constexpr const char *kPluginName = "VDISplugin";

enum {
    IDX_BUFFER_INFO = 0,
    IDX_CAMERA_INFO = 8,
    IDX_EXTRA_BUFFER_INFO = 0xb,
    IDX_FACE_NUM = 0xf,
    IDX_FACE_INFO = 0x10,
    IDX_FPS_INFO = 0x14,
};

enum {
    BUF_TYPE_INFO = 0,    /* get: last preview offset info */
    BUF_TYPE_OUTPUT = 2,  /* set: output buffer, get: finished output */
    BUF_TYPE_FRAME = 0x99,
};

/* UniPluginBufferData, 0x80 bytes */
struct BufferInfo {
    int32_t camType;
    int32_t type;
    uint32_t inY, inU, inV;
    uint32_t outY, outU, outV;
    int32_t inFd[3];
    int32_t outFd[3];
    int32_t reserved38[2];
    int32_t inW, inH;
    int32_t outW, outH;
    uint32_t index;  /* [23:16] output slot, [15:8] input slot, [7:0] skip/valid */
    int32_t reserved54;
    int32_t inStride;
    int32_t outStride;
    int64_t timestamp;
    int64_t timestampBoot;
    int32_t reserved70[4];
};
static_assert(sizeof(BufferInfo) == 0x80, "BufferInfo is 0x80 bytes");

/* UniPluginExtraBufferInfo, 0x50 bytes */
struct ExtraBufferInfo {
    int32_t reserved0[6];
    int32_t exposureTime;
    uint32_t luxDen;
    int32_t luxNum;
    int32_t reserved24[4];
    uint32_t evDen;
    int32_t evNum;
    int32_t reserved3c[2];
    int32_t orientation;
    float zoomRatio;
    int32_t reserved4c;
};
static_assert(sizeof(ExtraBufferInfo) == 0x50, "ExtraBufferInfo is 0x50 bytes");

struct CameraInfo {
    int32_t cameraType;  /* 0 wide, 1 tele, 0x20 super wide */
    int32_t sensorType;
    int32_t apType;
    int32_t reserved;
};

struct Fps {
    int32_t minNum, minDen;
    int32_t maxDen, maxNum;
};

/* face slots read by VDIS; it never gets faces from us */
constexpr size_t kFaceInfoSize = 0x140;

typedef void *(*uni_plugin_load_t)(const char *);
typedef int (*uni_plugin_set_t)(void *, const char *, int, void *);
typedef int (*uni_plugin_call_t)(void *);
typedef int (*uni_plugin_unload_t)(void **);

struct Plugin {
    void *lib;
    uni_plugin_load_t load;
    uni_plugin_set_t set;
    uni_plugin_set_t get;
    uni_plugin_call_t init;
    uni_plugin_call_t process;
    uni_plugin_call_t deinit;
    uni_plugin_unload_t unload;
    int32_t sensorType;
};

/* stock QCamera2HardwareInterface::getSensorID(): rear_camtype -> VDIS sensor */
int32_t readSensorType()
{
    static const struct {
        const char *name;
        int32_t type;
    } kSensors[] = {
        {"SONY_IMX576", 0x73},
    };
    char name[32] = {};
    FILE *f = fopen("/sys/class/camera/rear/rear_camtype", "r");
    if (f == nullptr) {
        return -1;
    }
    if (fgets(name, sizeof(name), f) == nullptr) {
        name[0] = '\0';
    }
    fclose(f);
    name[strcspn(name, "\n")] = '\0';
    for (const auto &s : kSensors) {
        if (strcmp(name, s.name) == 0) {
            return s.type;
        }
    }
    ALOGW("no VDIS sensor type for rear sensor %s", name);
    return -1;
}

const Plugin *getPlugin()
{
    static Plugin plugin;
    static bool tried;
    if (tried) {
        return plugin.lib != nullptr ? &plugin : nullptr;
    }
    tried = true;

    plugin.sensorType = readSensorType();
    if (plugin.sensorType < 0) {
        return nullptr;
    }
    void *lib = dlopen("libuniplugin.so", RTLD_NOW);
    if (lib == nullptr) {
        ALOGW("no libuniplugin: %s", dlerror());
        return nullptr;
    }
    plugin.load = (uni_plugin_load_t)dlsym(lib, "uni_plugin_load");
    plugin.set = (uni_plugin_set_t)dlsym(lib, "uni_plugin_set");
    plugin.get = (uni_plugin_set_t)dlsym(lib, "uni_plugin_get");
    plugin.init = (uni_plugin_call_t)dlsym(lib, "uni_plugin_init");
    plugin.process = (uni_plugin_call_t)dlsym(lib, "uni_plugin_process");
    plugin.deinit = (uni_plugin_call_t)dlsym(lib, "uni_plugin_deinit");
    plugin.unload = (uni_plugin_unload_t)dlsym(lib, "uni_plugin_unload");
    if (!plugin.load || !plugin.set || !plugin.get || !plugin.init || !plugin.process ||
            !plugin.deinit || !plugin.unload) {
        ALOGE("libuniplugin lacks uni_plugin_* symbols");
        dlclose(lib);
        return nullptr;
    }
    plugin.lib = lib;
    return &plugin;
}

} // namespace

bool QCamera3Vdis::isAvailable()
{
    return getPlugin() != nullptr;
}

bool QCamera3Vdis::getInputSize(int32_t outW, int32_t outH, int32_t &inW, int32_t &inH)
{
    /* the sizes stock records with VDIS, 6/5 of the output */
    if (outW == 1920 && outH == 1080) {
        inW = 2304;
        inH = 1296;
    } else if (outW == 1280 && outH == 720) {
        inW = 1536;
        inH = 864;
    } else {
        return false;
    }
    return true;
}

QCamera3Vdis::QCamera3Vdis()
    : mHandle(nullptr), mInW(0), mInH(0), mInStride(0), mOutW(0), mOutH(0), mOutStride(0)
{
}

QCamera3Vdis::~QCamera3Vdis()
{
    deinit();
}

status_t QCamera3Vdis::init(int32_t inW, int32_t inH, int32_t inStride,
        int32_t outW, int32_t outH, int32_t outStride, int32_t fps)
{
    const Plugin *p = getPlugin();
    if (p == nullptr) {
        return NO_INIT;
    }
    deinit();

    /* uni_plugin_unload() dlcloses the plugin, but binder objects of its
     * libsensorndkbridge die later on binder threads: never unmap it */
    static void *pinned = dlopen("libvdis_interface.so", RTLD_NOW);
    if (pinned == nullptr) {
        ALOGE("cannot load libvdis_interface: %s", dlerror());
        return NO_INIT;
    }

    mHandle = p->load(kPluginName);
    if (mHandle == nullptr) {
        ALOGE("uni_plugin_load(%s) failed", kPluginName);
        return NO_INIT;
    }
    mInW = inW;
    mInH = inH;
    mInStride = inStride;
    mOutW = outW;
    mOutH = outH;
    mOutStride = outStride;

    BufferInfo info = {};
    info.type = BUF_TYPE_FRAME;
    info.inW = inW;
    info.inH = inH;
    info.outW = outW;
    info.outH = outH;
    info.inStride = inStride;
    info.outStride = outStride;
    p->set(mHandle, kPluginName, IDX_BUFFER_INFO, &info);

    ExtraBufferInfo extra = {};
    extra.evDen = 1;
    extra.zoomRatio = 1.0f;
    p->set(mHandle, kPluginName, IDX_EXTRA_BUFFER_INFO, &extra);

    Fps rate = {};
    rate.maxDen = 1;
    rate.maxNum = fps >= 60 ? 60 : 30;
    p->set(mHandle, kPluginName, IDX_FPS_INFO, &rate);

    CameraInfo cam = {0, p->sensorType, 1, 0};
    p->set(mHandle, kPluginName, IDX_CAMERA_INFO, &cam);

    p->init(mHandle);
    ALOGI("VDIS %dx%d -> %dx%d at %d fps", inW, inH, outW, outH, rate.maxNum);
    return NO_ERROR;
}

void QCamera3Vdis::deinit()
{
    if (mHandle == nullptr) {
        return;
    }
    const Plugin *p = getPlugin();
    p->deinit(mHandle);
    if (p->unload(&mHandle) < 0) {
        ALOGE("uni_plugin_unload failed");
    }
    mHandle = nullptr;
}

void QCamera3Vdis::setOutput(uint32_t slot, const Buffer &out)
{
    BufferInfo info = {};
    info.type = BUF_TYPE_OUTPUT;
    info.outY = (uint32_t)(uintptr_t)out.y;
    info.outU = info.outY + out.uvOffset;
    info.outFd[0] = out.fd;
    info.inW = mInW;
    info.inH = mInH;
    info.outW = mOutW;
    info.outH = mOutH;
    info.inStride = mInStride;
    info.outStride = mOutStride;
    info.index = (slot & 0xff) << 16;
    getPlugin()->set(mHandle, kPluginName, IDX_BUFFER_INFO, &info);
}

void QCamera3Vdis::process(uint32_t slot, const Buffer &in, nsecs_t timestamp)
{
    const Plugin *p = getPlugin();

    uint8_t faces[kFaceInfoSize] = {};
    uint32_t faceNum = 0;
    p->set(mHandle, kPluginName, IDX_FACE_INFO, faces);
    p->set(mHandle, kPluginName, IDX_FACE_NUM, &faceNum);

    ExtraBufferInfo extra = {};
    extra.evDen = 1;
    extra.zoomRatio = 1.0f;
    p->set(mHandle, kPluginName, IDX_EXTRA_BUFFER_INFO, &extra);

    BufferInfo info = {};
    info.type = BUF_TYPE_FRAME;
    info.inY = (uint32_t)(uintptr_t)in.y;
    info.inU = info.inY + in.uvOffset;
    info.inFd[0] = in.fd;
    info.inW = mInW;
    info.inH = mInH;
    info.outW = mOutW;
    info.outH = mOutH;
    info.inStride = mInStride;
    info.outStride = mOutStride;
    /* no output for this call: outputs go to the buffers from setOutput() */
    info.index = 0xffff0000u | ((slot & 0xff) << 8);
    info.timestamp = timestamp;
    info.timestampBoot = timestamp;
    p->set(mHandle, kPluginName, IDX_BUFFER_INFO, &info);
    p->process(mHandle);
}

bool QCamera3Vdis::getOutput(uint32_t &outSlot, uint32_t &inSlot, bool &valid)
{
    BufferInfo info = {};
    info.type = BUF_TYPE_OUTPUT;
    getPlugin()->get(mHandle, kPluginName, IDX_BUFFER_INFO, &info);
    if (info.index & 0x80000000u) {
        return false;
    }
    outSlot = (info.index >> 16) & 0xff;
    inSlot = (info.index >> 8) & 0xff;
    valid = (info.index & 0xff) != 0 && info.timestamp != 0;
    return true;
}

}; // namespace qcamera
