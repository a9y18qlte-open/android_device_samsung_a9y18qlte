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

#ifndef __QCAMERA3BOKEH_H__
#define __QCAMERA3BOKEH_H__

#include <pthread.h>
#include <stdint.h>

#include <hardware/camera3.h>
#include <utils/Errors.h>
#include <utils/Timers.h>

namespace qcamera {

class QCamera3HardwareInterface;

/*
 * Dual camera bokeh (Samsung "Live Focus") for the main rear camera.
 *
 * While a session uses ANDROID_CONTROL_EXTENDED_SCENE_MODE_BOKEH_*, the depth
 * sensor runs as a second, internal HAL3 session of this HAL that streams YUV
 * frames into a small ring. Every bokeh still is blurred right before JPEG
 * encoding: the main frame and the depth frame closest in time go through
 * Samsung's ArcSoft dual camera refocus engine (libdualcam_refocus.so), which
 * computes a disparity map with the factory stereo calibration from the
 * kernel (/sys/class/camera/rear/rear_dualcal) and renders the blur.
 */
class QCamera3Bokeh {
public:
    QCamera3Bokeh(uint32_t mainId, const camera_module_callbacks_t *callbacks);
    ~QCamera3Bokeh();

    // Whether bokeh can be offered on this camera (main rear camera with the
    // engine, the calibration and the depth sensor present).
    static bool isSupported(uint32_t cameraId);

    // Starts the depth camera in the background; returns at once.
    void start();
    // Stops and closes the depth camera.
    void stop();

    // Blurs one YUV 4:2:0 semi-planar frame in place.
    // timestamp: sensor timestamp of the main frame, used to pick the depth frame.
    // focusX/Y: focus point in frame coordinates, < 0 for the frame centre.
    int32_t process(uint8_t *y, uint8_t *uv, int32_t width, int32_t height,
            int32_t yStride, int32_t uvStride, bool nv12, int64_t timestamp,
            int32_t focusX, int32_t focusY);

private:
    struct AuxBuffer {
        buffer_handle_t handle;
        uint32_t frameNumber;
        int64_t timestamp;
        bool busy;      // owned by the HAL
        bool ready;     // holds a finished frame
    };
    struct CallbackOps {
        camera3_callback_ops_t ops;
        QCamera3Bokeh *self;
    };

    static void *startThread(void *data);
    static void *requestThread(void *data);
    static void processCaptureResultCb(const camera3_callback_ops_t *ops,
            const camera3_capture_result_t *result);
    static void notifyCb(const camera3_callback_ops_t *ops, const camera3_notify_msg_t *msg);

    int32_t openAux();
    void closeAux();
    void requestLoop();
    void onResult(const camera3_capture_result_t *result);
    void onNotify(const camera3_notify_msg_t *msg);
    bool copyAuxFrame(int64_t timestamp, uint8_t *dst, int32_t width, int32_t height,
            bool nv12);

    const camera_module_callbacks_t *mCallbacks;
    uint32_t mMainId;
    uint32_t mAuxId;
    QCamera3HardwareInterface *mAuxHw;
    camera3_device_t *mAuxDevice;
    CallbackOps mCallbackOps;
    camera3_stream_t mAuxStream;
    const camera_metadata_t *mAuxSettings;
    static const int kMaxAuxBuffers = 8;
    AuxBuffer mAuxBuffers[kMaxAuxBuffers];
    int mNumAuxBuffers;
    uint32_t mNextFrameNumber;

    pthread_mutex_t mLock;
    pthread_cond_t mCond;
    bool mStarting;
    bool mRunning;
    bool mStopRequested;
    // after a failed start, e.g. no VFE free, no new one before this time
    nsecs_t mRetryTime;
    pthread_t mStartThread;
    bool mStartThreadValid;
    pthread_t mRequestThread;
    bool mRequestThreadValid;

    // serializes start/stop with process()
    pthread_mutex_t mAuxLock;
};

}; // namespace qcamera

#endif /* __QCAMERA3BOKEH_H__ */
