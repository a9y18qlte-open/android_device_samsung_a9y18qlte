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

#define LOG_TAG "QCamera3Bokeh"

#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cutils/properties.h>
#include <sync/sync.h>
#include <ui/GraphicBufferAllocator.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>

#include "QCamera3Bokeh.h"
#include "QCamera3HWI.h"

extern "C" {
#include "mm_camera_dbg.h"
}

using namespace android;

namespace qcamera {

extern cam_capability_t *gCamCapability[MM_CAMERA_MAX_NUM_SENSORS];

namespace {

/* ArcSoft dual camera refocus API, as used by Samsung's libsecdepth
 * (ArcSoft_DualCamRefocus_1.1.120211001.337). */
#define ASVL_PAF_NV12 0x801
#define ASVL_PAF_NV21 0x802
#define ARC_DCIR_NORMAL_MODE 1

typedef struct {
    uint32_t u32PixelArrayFormat;
    int32_t i32Width;
    int32_t i32Height;
    uint8_t *ppu8Plane[4];
    int32_t pi32Pitch[4];
} ASVLOFFSCREEN;

typedef struct {
    void *pCalibData;
    int32_t i32CalibDataSize;
} ARC_DC_CALDATA;

/* Sizes of both images in full sensor pixels, i.e. the crop of the sensor
 * array they show before any scaling. The calibration is in these units. */
typedef struct {
    int32_t i32MainWidth_CropNoScale;
    int32_t i32MainHeight_CropNoScale;
    int32_t i32AuxWidth_CropNoScale;
    int32_t i32AuxHeight_CropNoScale;
} ARC_REFOCUSCAMERAIMAGE_PARAM;

typedef struct {
    int32_t i32ImgDegree;
    float fMaxFOV;
    void *prtFaces;
    int32_t *pi32FaceAngles;
    int32_t i32FacesNum;
} ARC_DCIR_PARAM;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t i32BlurIntensity;
} ARC_DCIR_REFOCUS_PARAM;

/* Filled by ARC_DCIR_GetDisparityData, handed back to ARC_DCIR_Process. */
typedef struct {
    int32_t i32Reserved;
    int32_t i32Width;
    int32_t i32Height;
    void *pData;
    int32_t i32Pad[12];
} ARC_DCIR_DISPARITY;

struct ArcSoftApi {
    bool loaded;
    void *handle;
    int (*init)(void **, int);
    int (*uninit)(void **);
    int (*setCameraImageInfo)(void *, ARC_REFOCUSCAMERAIMAGE_PARAM *);
    int (*setCaliData)(void *, ARC_DC_CALDATA *);
    int (*calcDisparityData)(void *, ASVLOFFSCREEN *, ASVLOFFSCREEN *, ARC_DCIR_PARAM *);
    int (*getDisparityData)(void *, ARC_DCIR_DISPARITY *);
    int (*process)(void *, ARC_DCIR_DISPARITY *, ASVLOFFSCREEN *, ARC_DCIR_REFOCUS_PARAM *,
            ASVLOFFSCREEN *);
};

const char *kArcSoftLib = "libdualcam_refocus.so";
const char *kArcSoftPath = "/vendor/lib/libdualcam_refocus.so";
const char *kCalPath = "/sys/class/camera/rear/rear_dualcal";
const size_t kCalSize = 512;

pthread_mutex_t gArcLock = PTHREAD_MUTEX_INITIALIZER;
ArcSoftApi gArc;

bool loadArcSoft()
{
    pthread_mutex_lock(&gArcLock);
    if (!gArc.loaded) {
        void *h = dlopen(kArcSoftLib, RTLD_NOW);
        if (h == NULL) {
            LOGE("dlopen %s failed: %s", kArcSoftLib, dlerror());
        } else {
            gArc.init = (int (*)(void **, int))dlsym(h, "ARC_DCIR_Init");
            gArc.uninit = (int (*)(void **))dlsym(h, "ARC_DCIR_Uninit");
            gArc.setCameraImageInfo = (int (*)(void *, ARC_REFOCUSCAMERAIMAGE_PARAM *))
                    dlsym(h, "ARC_DCIR_SetCameraImageInfo");
            gArc.setCaliData = (int (*)(void *, ARC_DC_CALDATA *))
                    dlsym(h, "ARC_DCIR_SetCaliData");
            gArc.calcDisparityData = (int (*)(void *, ASVLOFFSCREEN *, ASVLOFFSCREEN *,
                    ARC_DCIR_PARAM *))dlsym(h, "ARC_DCIR_CalcDisparityData");
            gArc.getDisparityData = (int (*)(void *, ARC_DCIR_DISPARITY *))
                    dlsym(h, "ARC_DCIR_GetDisparityData");
            gArc.process = (int (*)(void *, ARC_DCIR_DISPARITY *, ASVLOFFSCREEN *,
                    ARC_DCIR_REFOCUS_PARAM *, ASVLOFFSCREEN *))dlsym(h, "ARC_DCIR_Process");
            if (gArc.init && gArc.uninit && gArc.setCameraImageInfo && gArc.setCaliData &&
                    gArc.calcDisparityData && gArc.getDisparityData && gArc.process) {
                gArc.handle = h;
            } else {
                LOGE("%s lacks the expected symbols", kArcSoftLib);
                dlclose(h);
            }
        }
        gArc.loaded = true;
    }
    bool ok = gArc.handle != NULL;
    pthread_mutex_unlock(&gArcLock);
    return ok;
}

bool readCalibration(uint8_t *buf)
{
    int fd = open(kCalPath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        LOGE("cannot open %s", kCalPath);
        return false;
    }
    size_t got = 0;
    while (got < kCalSize) {
        ssize_t n = read(fd, buf + got, kCalSize - got);
        if (n <= 0) {
            break;
        }
        got += (size_t)n;
    }
    close(fd);
    if (got != kCalSize) {
        LOGE("short calibration read: %zu bytes", got);
        return false;
    }
    return true;
}

bool hasCalibration()
{
    uint8_t cal[kCalSize];
    if (!readCalibration(cal)) {
        return false;
    }
    for (size_t i = 0; i < kCalSize; i++) {
        if (cal[i] != 0 && cal[i] != 0xff) {
            return true;
        }
    }
    LOGE("calibration is blank");
    return false;
}

uint32_t findAuxCamera()
{
    char prop[PROPERTY_VALUE_MAX];
    property_get("persist.vendor.camera.bokeh.aux_id", prop, "-1");
    int id = atoi(prop);
    if (id >= 0 && id < MM_CAMERA_MAX_NUM_SENSORS) {
        return (uint32_t)id;
    }
    for (uint32_t i = 0; i < MM_CAMERA_MAX_NUM_SENSORS; i++) {
        if (gCamCapability[i] != NULL && gCamCapability[i]->is_depth_sensor) {
            return i;
        }
    }
    /* Samsung's backend leaves is_depth_sensor unset; the depth camera is the
     * last of the five sensors. */
    return 4;
}

/* Full sensor array size, which the calibration uses: the active array, or the
 * largest picture size on quad-CFA sensors whose active array is binned. The
 * depth sensor's largest picture (2560x1920) is a downscale of 2592x1944. */
bool sensorFullSize(uint32_t id, int32_t &w, int32_t &h)
{
    if (id >= MM_CAMERA_MAX_NUM_SENSORS || gCamCapability[id] == NULL) {
        return false;
    }
    const cam_capability_t *cap = gCamCapability[id];
    w = cap->active_array_size.width;
    h = cap->active_array_size.height;
    for (size_t i = 0; i < cap->picture_sizes_tbl_cnt && i < MAX_SIZES_CNT; i++) {
        const cam_dimension_t &d = cap->picture_sizes_tbl[i];
        if ((int64_t)d.width * d.height > (int64_t)w * h) {
            w = d.width;
            h = d.height;
        }
    }
    return w > 0 && h > 0;
}

/* Debug: writes a frame (Y, then the interleaved chroma if any) to /data/vendor/camera. */
void dumpFrame(const char *name, const uint8_t *y, int32_t w, int32_t h, int32_t yStride,
        const uint8_t *uv, int32_t uvStride)
{
    char path[128];
    snprintf(path, sizeof(path), "/data/vendor/camera/bokeh_%s_%dx%d.%s", name, w, h,
            uv ? "yuv" : "grey");
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        LOGE("cannot write %s", path);
        return;
    }
    for (int32_t r = 0; r < h; r++) {
        write(fd, y + (size_t)r * yStride, (size_t)w);
    }
    for (int32_t r = 0; uv != NULL && r < h / 2; r++) {
        write(fd, uv + (size_t)r * uvStride, (size_t)w);
    }
    close(fd);
}

/* Centered crop of a w x h array with the aspect ratio of fw x fh. */
void aspectCrop(int32_t w, int32_t h, int32_t fw, int32_t fh, int32_t &cw, int32_t &ch)
{
    if ((int64_t)w * fh > (int64_t)h * fw) {
        ch = h;
        cw = (int32_t)((int64_t)h * fw / fh) & ~1;
    } else {
        cw = w;
        ch = (int32_t)((int64_t)w * fh / fw) & ~1;
    }
}

} // namespace

QCamera3Bokeh::QCamera3Bokeh(uint32_t mainId, const camera_module_callbacks_t *callbacks)
    : mCallbacks(callbacks),
      mMainId(mainId),
      mAuxId(0),
      mAuxHw(NULL),
      mAuxDevice(NULL),
      mAuxSettings(NULL),
      mNumAuxBuffers(0),
      mNextFrameNumber(0),
      mStarting(false),
      mRunning(false),
      mStopRequested(false),
      mRetryTime(0),
      mStartThreadValid(false),
      mRequestThreadValid(false)
{
    memset(&mCallbackOps, 0, sizeof(mCallbackOps));
    mCallbackOps.ops.process_capture_result = processCaptureResultCb;
    mCallbackOps.ops.notify = notifyCb;
    mCallbackOps.self = this;
    memset(&mAuxStream, 0, sizeof(mAuxStream));
    memset(mAuxBuffers, 0, sizeof(mAuxBuffers));
    pthread_mutex_init(&mLock, NULL);
    pthread_cond_init(&mCond, NULL);
    pthread_mutex_init(&mAuxLock, NULL);
}

QCamera3Bokeh::~QCamera3Bokeh()
{
    stop();
    pthread_mutex_destroy(&mAuxLock);
    pthread_cond_destroy(&mCond);
    pthread_mutex_destroy(&mLock);
}

bool QCamera3Bokeh::isSupported(uint32_t cameraId)
{
    char prop[PROPERTY_VALUE_MAX];
    property_get("persist.vendor.camera.bokeh.enable", prop, "1");
    if (!atoi(prop)) {
        return false;
    }
    if (cameraId >= MM_CAMERA_MAX_NUM_SENSORS || gCamCapability[cameraId] == NULL ||
            gCamCapability[cameraId]->position != CAM_POSITION_BACK ||
            gCamCapability[cameraId]->is_depth_sensor) {
        return false;
    }
    /* the depth camera's capabilities may not be loaded yet at this point:
     * camera 0's static metadata is built before the others are queried */
    uint32_t auxId = findAuxCamera();
    if (auxId == cameraId) {
        return false;
    }
    /* no dlopen here: this runs for the static metadata at provider start */
    return hasCalibration() && access(kArcSoftPath, R_OK) == 0;
}

void QCamera3Bokeh::start()
{
    pthread_mutex_lock(&mLock);
    /* every bokeh request calls this: after a failed start, retry once a second */
    if (mStarting || mRunning || systemTime() < mRetryTime) {
        pthread_mutex_unlock(&mLock);
        return;
    }
    if (mStartThreadValid) {
        pthread_join(mStartThread, NULL);
        mStartThreadValid = false;
    }
    mStarting = true;
    mStopRequested = false;
    if (pthread_create(&mStartThread, NULL, startThread, this) == 0) {
        pthread_setname_np(mStartThread, "CAM_BokehStart");
        mStartThreadValid = true;
    } else {
        mStarting = false;
    }
    pthread_mutex_unlock(&mLock);
}

void *QCamera3Bokeh::startThread(void *data)
{
    QCamera3Bokeh *self = (QCamera3Bokeh *)data;
    pthread_mutex_lock(&self->mAuxLock);
    int32_t rc = self->openAux();
    pthread_mutex_lock(&self->mLock);
    self->mStarting = false;
    if (rc == NO_ERROR && !self->mStopRequested) {
        self->mRunning = true;
        if (pthread_create(&self->mRequestThread, NULL, requestThread, self) == 0) {
            pthread_setname_np(self->mRequestThread, "CAM_BokehAux");
            self->mRequestThreadValid = true;
        } else {
            self->mRunning = false;
        }
    }
    bool failed = !self->mRunning;
    if (failed && !self->mStopRequested) {
        self->mRetryTime = systemTime() + s2ns(1);
    }
    pthread_cond_broadcast(&self->mCond);
    pthread_mutex_unlock(&self->mLock);
    if (failed && rc == NO_ERROR) {
        self->closeAux();
    }
    pthread_mutex_unlock(&self->mAuxLock);
    return NULL;
}

void QCamera3Bokeh::stop()
{
    pthread_mutex_lock(&mLock);
    mStopRequested = true;
    pthread_cond_broadcast(&mCond);
    pthread_mutex_unlock(&mLock);

    if (mStartThreadValid) {
        pthread_join(mStartThread, NULL);
        mStartThreadValid = false;
    }

    pthread_mutex_lock(&mAuxLock);
    pthread_mutex_lock(&mLock);
    bool running = mRunning;
    mRunning = false;
    pthread_cond_broadcast(&mCond);
    pthread_mutex_unlock(&mLock);
    if (mRequestThreadValid) {
        pthread_join(mRequestThread, NULL);
        mRequestThreadValid = false;
    }
    if (running) {
        closeAux();
    }
    pthread_mutex_unlock(&mAuxLock);
}

int32_t QCamera3Bokeh::openAux()
{
    mAuxId = findAuxCamera();
    LOGI("opening depth camera %u", mAuxId);

    mAuxHw = new QCamera3HardwareInterface(mAuxId, mCallbacks);
    struct hw_device_t *dev = NULL;
    int rc = mAuxHw->openCamera(&dev);
    if (rc != 0 || dev == NULL) {
        LOGE("opening depth camera %u failed: %d", mAuxId, rc);
        delete mAuxHw;
        mAuxHw = NULL;
        return UNKNOWN_ERROR;
    }
    mAuxDevice = (camera3_device_t *)dev;

    rc = mAuxDevice->ops->initialize(mAuxDevice, &mCallbackOps.ops);
    if (rc != 0) {
        LOGE("initialize failed: %d", rc);
        closeAux();
        return UNKNOWN_ERROR;
    }

    /* Largest depth picture size; full field of view. */
    int32_t w = 0, h = 0;
    const cam_capability_t *auxCap = gCamCapability[mAuxId];
    for (size_t i = 0; auxCap != NULL && i < auxCap->picture_sizes_tbl_cnt &&
            i < MAX_SIZES_CNT; i++) {
        const cam_dimension_t &d = auxCap->picture_sizes_tbl[i];
        if ((int64_t)d.width * d.height > (int64_t)w * h) {
            w = d.width;
            h = d.height;
        }
    }
    if (w == 0 || h == 0) {
        LOGE("no picture sizes for camera %u", mAuxId);
        closeAux();
        return UNKNOWN_ERROR;
    }
    memset(&mAuxStream, 0, sizeof(mAuxStream));
    mAuxStream.stream_type = CAMERA3_STREAM_OUTPUT;
    mAuxStream.width = (uint32_t)w;
    mAuxStream.height = (uint32_t)h;
    mAuxStream.format = HAL_PIXEL_FORMAT_YCbCr_420_888;
    mAuxStream.usage = GRALLOC_USAGE_SW_READ_OFTEN;
    mAuxStream.data_space = HAL_DATASPACE_V0_JFIF;
    mAuxStream.rotation = CAMERA3_STREAM_ROTATION_0;
    mAuxStream.physical_camera_id = "";

    camera3_stream_t *streams[] = {&mAuxStream};
    camera3_stream_configuration_t config;
    memset(&config, 0, sizeof(config));
    config.num_streams = 1;
    config.streams = streams;
    config.operation_mode = CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE;
    rc = mAuxDevice->ops->configure_streams(mAuxDevice, &config);
    if (rc != 0) {
        LOGE("configure_streams %dx%d failed: %d", w, h, rc);
        closeAux();
        return UNKNOWN_ERROR;
    }

    mNumAuxBuffers = (int)mAuxStream.max_buffers + 2;
    if (mNumAuxBuffers > kMaxAuxBuffers) {
        mNumAuxBuffers = kMaxAuxBuffers;
    }
    uint64_t usage = (uint64_t)mAuxStream.usage | GRALLOC_USAGE_SW_READ_OFTEN |
            GRALLOC_USAGE_HW_CAMERA_WRITE;
    memset(mAuxBuffers, 0, sizeof(mAuxBuffers));
    for (int i = 0; i < mNumAuxBuffers; i++) {
        uint32_t stride = 0;
        status_t err = GraphicBufferAllocator::get().allocate(mAuxStream.width,
                mAuxStream.height, mAuxStream.format, 1, usage,
                &mAuxBuffers[i].handle, &stride, "QCamera3Bokeh");
        if (err != NO_ERROR) {
            LOGE("buffer allocation failed: %d", err);
            mNumAuxBuffers = i;
            closeAux();
            return NO_MEMORY;
        }
    }

    mAuxSettings = mAuxDevice->ops->construct_default_request_settings(mAuxDevice,
            CAMERA3_TEMPLATE_PREVIEW);
    if (mAuxSettings == NULL) {
        LOGE("no default request settings");
        closeAux();
        return UNKNOWN_ERROR;
    }
    mNextFrameNumber = 0;
    LOGI("depth camera %u streaming %dx%d with %d buffers", mAuxId, w, h, mNumAuxBuffers);
    return NO_ERROR;
}

void QCamera3Bokeh::closeAux()
{
    if (mAuxDevice != NULL) {
        mAuxDevice->ops->flush(mAuxDevice);
        /* close_camera_device deletes the HAL object */
        mAuxDevice->common.close(&mAuxDevice->common);
        mAuxDevice = NULL;
        mAuxHw = NULL;
    } else if (mAuxHw != NULL) {
        delete mAuxHw;
        mAuxHw = NULL;
    }
    mAuxSettings = NULL;
    for (int i = 0; i < mNumAuxBuffers; i++) {
        if (mAuxBuffers[i].handle != NULL) {
            GraphicBufferAllocator::get().free(mAuxBuffers[i].handle);
        }
    }
    memset(mAuxBuffers, 0, sizeof(mAuxBuffers));
    mNumAuxBuffers = 0;
    LOGI("depth camera closed");
}

void *QCamera3Bokeh::requestThread(void *data)
{
    ((QCamera3Bokeh *)data)->requestLoop();
    return NULL;
}

void QCamera3Bokeh::requestLoop()
{
    bool first = true;
    pthread_mutex_lock(&mLock);
    while (mRunning) {
        /* keep the two newest finished frames for process(), recycle the rest */
        int idx = -1, oldest = -1, ready = 0;
        for (int i = 0; i < mNumAuxBuffers; i++) {
            AuxBuffer &b = mAuxBuffers[i];
            if (b.busy) {
                continue;
            }
            if (!b.ready) {
                idx = i;
                break;
            }
            ready++;
            if (oldest < 0 || b.timestamp < mAuxBuffers[oldest].timestamp) {
                oldest = i;
            }
        }
        if (idx < 0 && ready > 2) {
            idx = oldest;
        }
        if (idx < 0) {
            pthread_cond_wait(&mCond, &mLock);
            continue;
        }
        AuxBuffer &b = mAuxBuffers[idx];
        b.ready = false;
        b.busy = true;
        b.timestamp = 0;
        b.frameNumber = mNextFrameNumber++;

        camera3_stream_buffer_t out;
        memset(&out, 0, sizeof(out));
        out.stream = &mAuxStream;
        out.buffer = &b.handle;
        out.status = CAMERA3_BUFFER_STATUS_OK;
        out.acquire_fence = -1;
        out.release_fence = -1;
        camera3_capture_request_t req;
        memset(&req, 0, sizeof(req));
        req.frame_number = b.frameNumber;
        req.settings = first ? mAuxSettings : NULL;
        req.num_output_buffers = 1;
        req.output_buffers = &out;
        pthread_mutex_unlock(&mLock);

        /* blocks while the HAL pipeline is full */
        int rc = mAuxDevice->ops->process_capture_request(mAuxDevice, &req);

        pthread_mutex_lock(&mLock);
        if (rc != 0) {
            LOGE("depth request %u failed: %d", req.frame_number, rc);
            b.busy = false;
            /* do not spin on a broken session */
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 100000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&mCond, &mLock, &ts);
            continue;
        }
        first = false;
    }
    pthread_mutex_unlock(&mLock);
}

void QCamera3Bokeh::processCaptureResultCb(const camera3_callback_ops_t *ops,
        const camera3_capture_result_t *result)
{
    ((const CallbackOps *)ops)->self->onResult(result);
}

void QCamera3Bokeh::notifyCb(const camera3_callback_ops_t *ops, const camera3_notify_msg_t *msg)
{
    ((const CallbackOps *)ops)->self->onNotify(msg);
}

void QCamera3Bokeh::onNotify(const camera3_notify_msg_t *msg)
{
    pthread_mutex_lock(&mLock);
    if (msg->type == CAMERA3_MSG_SHUTTER) {
        for (int i = 0; i < mNumAuxBuffers; i++) {
            if (mAuxBuffers[i].busy &&
                    mAuxBuffers[i].frameNumber == msg->message.shutter.frame_number) {
                mAuxBuffers[i].timestamp = (int64_t)msg->message.shutter.timestamp;
            }
        }
    } else if (msg->type == CAMERA3_MSG_ERROR) {
        LOGW("depth camera error %d on frame %u", msg->message.error.error_code,
                msg->message.error.frame_number);
    }
    pthread_mutex_unlock(&mLock);
}

void QCamera3Bokeh::onResult(const camera3_capture_result_t *result)
{
    for (uint32_t n = 0; n < result->num_output_buffers; n++) {
        const camera3_stream_buffer_t &out = result->output_buffers[n];
        if (out.release_fence >= 0) {
            sync_wait(out.release_fence, 1000);
            close(out.release_fence);
        }
        pthread_mutex_lock(&mLock);
        for (int i = 0; i < mNumAuxBuffers; i++) {
            AuxBuffer &b = mAuxBuffers[i];
            if (b.busy && &b.handle == out.buffer) {
                b.busy = false;
                b.ready = (out.status == CAMERA3_BUFFER_STATUS_OK) && b.timestamp != 0;
            }
        }
        pthread_cond_broadcast(&mCond);
        pthread_mutex_unlock(&mLock);
    }
}

bool QCamera3Bokeh::copyAuxFrame(int64_t timestamp, uint8_t *dst, int32_t width,
        int32_t height, bool nv12)
{
    pthread_mutex_lock(&mLock);
    /* wait up to 300 ms for the first frame after start-up */
    for (int tries = 0; tries < 2; tries++) {
        int best = -1;
        for (int i = 0; i < mNumAuxBuffers; i++) {
            const AuxBuffer &b = mAuxBuffers[i];
            if (!b.ready) {
                continue;
            }
            if (best < 0 || llabs(b.timestamp - timestamp) <
                    llabs(mAuxBuffers[best].timestamp - timestamp)) {
                best = i;
            }
        }
        if (best >= 0) {
            AuxBuffer &b = mAuxBuffers[best];
            /* hold the buffer against recycling while copying */
            b.busy = true;
            int64_t delta = b.timestamp - timestamp;
            pthread_mutex_unlock(&mLock);

            android_ycbcr ycbcr;
            memset(&ycbcr, 0, sizeof(ycbcr));
            Rect bounds((int32_t)mAuxStream.width, (int32_t)mAuxStream.height);
            bool ok = GraphicBufferMapper::get().lockYCbCr(b.handle,
                    GRALLOC_USAGE_SW_READ_OFTEN, bounds, &ycbcr) == NO_ERROR;
            if (ok) {
                /* centered crop to the main image aspect ratio */
                int32_t cw, ch;
                aspectCrop((int32_t)mAuxStream.width, (int32_t)mAuxStream.height,
                        width, height, cw, ch);
                if (cw != width || ch != height) {
                    LOGE("depth crop %dx%d != %dx%d", cw, ch, width, height);
                    ok = false;
                } else {
                    int32_t x0 = (((int32_t)mAuxStream.width - cw) / 2) & ~1;
                    int32_t y0 = (((int32_t)mAuxStream.height - ch) / 2) & ~1;
                    const uint8_t *ys = (const uint8_t *)ycbcr.y;
                    for (int32_t r = 0; r < ch; r++) {
                        memcpy(dst + (size_t)r * cw, ys + (size_t)(y0 + r) * ycbcr.ystride + x0,
                                (size_t)cw);
                    }
                    const uint8_t *cb = (const uint8_t *)ycbcr.cb;
                    const uint8_t *cr = (const uint8_t *)ycbcr.cr;
                    uint8_t *uvDst = dst + (size_t)cw * ch;
                    const uint8_t *first = nv12 ? cb : cr;
                    const uint8_t *second = nv12 ? cr : cb;
                    for (int32_t r = 0; r < ch / 2; r++) {
                        const size_t off = (size_t)(y0 / 2 + r) * ycbcr.cstride;
                        const uint8_t *a = first + off + (size_t)(x0 / 2) * ycbcr.chroma_step;
                        const uint8_t *c = second + off + (size_t)(x0 / 2) * ycbcr.chroma_step;
                        uint8_t *d = uvDst + (size_t)r * cw;
                        if (ycbcr.chroma_step == 2 && c == a + 1) {
                            memcpy(d, a, (size_t)cw);
                        } else {
                            for (int32_t x = 0; x < cw / 2; x++) {
                                d[2 * x] = a[x * ycbcr.chroma_step];
                                d[2 * x + 1] = c[x * ycbcr.chroma_step];
                            }
                        }
                    }
                }
                GraphicBufferMapper::get().unlock(b.handle);
            }

            pthread_mutex_lock(&mLock);
            b.busy = false;
            pthread_cond_broadcast(&mCond);
            pthread_mutex_unlock(&mLock);
            LOGI("depth frame %u, %lld us from the main frame%s", b.frameNumber,
                    (long long)(delta / 1000), ok ? "" : " (copy failed)");
            return ok;
        }
        if (!mRunning && !mStarting) {
            break;
        }
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 300000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&mCond, &mLock, &ts);
    }
    pthread_mutex_unlock(&mLock);
    LOGE("no depth frame available");
    return false;
}

int32_t QCamera3Bokeh::process(uint8_t *y, uint8_t *uv, int32_t width, int32_t height,
        int32_t yStride, int32_t uvStride, bool nv12, int64_t timestamp,
        int32_t focusX, int32_t focusY)
{
    if (!loadArcSoft()) {
        return NO_INIT;
    }

    int32_t mainW, mainH, auxW, auxH;
    pthread_mutex_lock(&mLock);
    bool active = mRunning || mStarting;
    uint32_t auxId = mAuxId;
    pthread_mutex_unlock(&mLock);
    if (!active) {
        LOGE("depth camera not running");
        return NO_INIT;
    }
    if (!sensorFullSize(mMainId, mainW, mainH) || !sensorFullSize(auxId, auxW, auxH)) {
        LOGE("no sensor sizes");
        return NO_INIT;
    }

    /* the depth stream cropped to the main image's aspect ratio */
    int32_t auxCropW, auxCropH;
    aspectCrop((int32_t)mAuxStream.width, (int32_t)mAuxStream.height, width, height,
            auxCropW, auxCropH);
    size_t auxLen = (size_t)auxCropW * auxCropH * 3 / 2;
    size_t outLen = (size_t)width * height * 3 / 2;
    uint8_t *aux = (uint8_t *)malloc(auxLen);
    uint8_t *out = (uint8_t *)malloc(outLen);
    if (aux == NULL || out == NULL) {
        free(aux);
        free(out);
        return NO_MEMORY;
    }
    if (!copyAuxFrame(timestamp, aux, auxCropW, auxCropH, nv12)) {
        free(aux);
        free(out);
        return NO_INIT;
    }

    uint8_t cal[kCalSize];
    if (!readCalibration(cal)) {
        free(aux);
        free(out);
        return NO_INIT;
    }

    char prop[PROPERTY_VALUE_MAX];
    property_get("persist.vendor.camera.bokeh.blur", prop, "70");
    int32_t blur = atoi(prop);
    if (blur < 0 || blur > 100) {
        blur = 70;
    }

    uint32_t fmt = nv12 ? ASVL_PAF_NV12 : ASVL_PAF_NV21;
    ASVLOFFSCREEN mainImg, auxImg, outImg;
    memset(&mainImg, 0, sizeof(mainImg));
    mainImg.u32PixelArrayFormat = fmt;
    mainImg.i32Width = width;
    mainImg.i32Height = height;
    mainImg.ppu8Plane[0] = y;
    mainImg.ppu8Plane[1] = uv;
    mainImg.pi32Pitch[0] = yStride;
    mainImg.pi32Pitch[1] = uvStride;
    memset(&auxImg, 0, sizeof(auxImg));
    auxImg.u32PixelArrayFormat = fmt;
    auxImg.i32Width = auxCropW;
    auxImg.i32Height = auxCropH;
    auxImg.ppu8Plane[0] = aux;
    auxImg.ppu8Plane[1] = aux + (size_t)auxCropW * auxCropH;
    auxImg.pi32Pitch[0] = auxCropW;
    auxImg.pi32Pitch[1] = auxCropW;
    memset(&outImg, 0, sizeof(outImg));
    outImg.u32PixelArrayFormat = fmt;
    outImg.i32Width = width;
    outImg.i32Height = height;
    outImg.ppu8Plane[0] = out;
    outImg.ppu8Plane[1] = out + (size_t)width * height;
    outImg.pi32Pitch[0] = width;
    outImg.pi32Pitch[1] = width;

    ARC_REFOCUSCAMERAIMAGE_PARAM info;
    aspectCrop(mainW, mainH, width, height, info.i32MainWidth_CropNoScale,
            info.i32MainHeight_CropNoScale);
    aspectCrop(auxW, auxH, width, height, info.i32AuxWidth_CropNoScale,
            info.i32AuxHeight_CropNoScale);
    ARC_DC_CALDATA calData = {cal, (int32_t)kCalSize};
    ARC_DCIR_PARAM param;
    memset(&param, 0, sizeof(param));
    param.fMaxFOV = 72.0f;   // as Samsung passes it
    ARC_DCIR_DISPARITY disparity;
    memset(&disparity, 0, sizeof(disparity));
    ARC_DCIR_REFOCUS_PARAM refocus;
    refocus.x = (focusX >= 0 && focusX < width) ? focusX : width / 2;
    refocus.y = (focusY >= 0 && focusY < height) ? focusY : height / 2;
    refocus.i32BlurIntensity = blur;

    void *engine = NULL;
    int rc = gArc.init(&engine, ARC_DCIR_NORMAL_MODE);
    if (rc == 0) {
        rc = gArc.setCameraImageInfo(engine, &info);
    }
    if (rc == 0) {
        rc = gArc.setCaliData(engine, &calData);
    }
    if (rc == 0) {
        rc = gArc.calcDisparityData(engine, &mainImg, &auxImg, &param);
    }
    if (rc == 0) {
        rc = gArc.getDisparityData(engine, &disparity);
    }
    if (rc == 0) {
        rc = gArc.process(engine, &disparity, &mainImg, &refocus, &outImg);
    }
    property_get("persist.vendor.camera.bokeh.dump", prop, "0");
    if (atoi(prop)) {
        dumpFrame("aux", aux, auxCropW, auxCropH, auxCropW, aux + (size_t)auxCropW * auxCropH,
                auxCropW);
        dumpFrame("main", y, width, height, yStride, uv, uvStride);
        if (rc == 0) {
            dumpFrame("out", out, width, height, width, out + (size_t)width * height, width);
        }
        if (disparity.pData != NULL && disparity.i32Width > 0 && disparity.i32Height > 0) {
            dumpFrame("depth", (uint8_t *)disparity.pData, disparity.i32Width,
                    disparity.i32Height, disparity.i32Width, NULL, 0);
        }
    }
    if (engine != NULL) {
        if (rc == 0) {
            for (int32_t r = 0; r < height; r++) {
                memcpy(y + (size_t)r * yStride, out + (size_t)r * width, (size_t)width);
            }
            for (int32_t r = 0; r < height / 2; r++) {
                memcpy(uv + (size_t)r * uvStride, out + (size_t)width * height +
                        (size_t)r * width, (size_t)width);
            }
        }
        gArc.uninit(&engine);
    }
    LOGI("bokeh %dx%d (sensor crop %dx%d), depth %dx%d (sensor crop %dx%d), "
            "focus %d,%d blur %d: %d",
            width, height, info.i32MainWidth_CropNoScale, info.i32MainHeight_CropNoScale,
            auxCropW, auxCropH, info.i32AuxWidth_CropNoScale, info.i32AuxHeight_CropNoScale,
            refocus.x, refocus.y, blur, rc);
    free(aux);
    free(out);
    return rc == 0 ? NO_ERROR : UNKNOWN_ERROR;
}

}; // namespace qcamera
