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

#ifndef __QCAMERA3VDISCHANNEL_H__
#define __QCAMERA3VDISCHANNEL_H__

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "QCamera3Channel.h"
#include "QCamera3Vdis.h"

namespace qcamera {

/*
 * Video channel of the main rear camera that can run Samsung VDIS.
 *
 * Until enableVdis() is called before the first request it is a plain
 * QCamera3RegularChannel. With VDIS the backend fills internal buffers 6/5 the
 * size of the framework stream, one per request; VDIS warps them into
 * internal output buffers some frames later and each output is copied into
 * the framework buffer of the request its input came from, so every buffer
 * keeps its own frame and timestamp. Frames VDIS drops get an error buffer.
 *
 * Buffers go back to the framework from a worker thread, which also drains
 * VDIS when the input stops (end of a recording in a session that goes on):
 * result callbacks take the HAL mutex, which is held around request() and
 * timeoutFrame().
 */
class QCamera3VdisChannel : public QCamera3RegularChannel {
public:
    /* framework buffers the stream needs on top of a regular video stream */
    static constexpr uint32_t EXTRA_BUFFERS = QCamera3Vdis::VDIS_DELAY + 4;

    static bool isEligible(uint32_t cameraId, const camera3_stream_t *stream,
            uint32_t opMode);

    QCamera3VdisChannel(uint32_t cam_handle,
            uint32_t channel_handle,
            mm_camera_ops_t *cam_ops,
            channel_cb_routine cb_routine,
            channel_cb_buffer_err cb_buffer_err,
            cam_padding_info_t *paddingInfo,
            void *userData,
            camera3_stream_t *stream,
            cam_stream_type_t stream_type,
            cam_feature_mask_t postprocess_mask,
            QCamera3Channel *metadataChannel,
            uint32_t numBuffers);
    virtual ~QCamera3VdisChannel();

    /* call before the first request; false if the stream cannot use VDIS */
    bool enableVdis(int32_t fps);
    bool isVdisEnabled() const { return mVdisEnabled; }
    cam_dimension_t getBackendDim() const { return mInDim; }

    using QCamera3RegularChannel::request;
    virtual int32_t request(buffer_handle_t *buffer, uint32_t frameNumber,
            int &indexUsed);
    virtual void streamCbRoutine(mm_camera_super_buf_t *super_frame,
            QCamera3Stream *stream);
    virtual QCamera3StreamMem *getStreamBufs(uint32_t len);
    virtual void putStreamBufs();
    virtual int32_t stop();
    virtual int32_t timeoutFrame(uint32_t frameNumber);

private:
    /* input frames held: queued in the backend plus inside VDIS */
    static constexpr uint32_t NUM_INPUT_BUFFERS = 18;
    static constexpr uint32_t NUM_OUTPUT_BUFFERS = 12;
    /* no input for this long while frames are inside VDIS: drain it */
    static constexpr nsecs_t STALL_NS = 150000000;

    struct Pending {
        uint32_t frameNumber;
        int32_t inIdx;      /* input buffer, -1 once released */
        bool received;      /* input came back from the backend */
        bool done;
        bool error;
    };

    void workerLoop();
    bool startVdisLocked();
    void resetVdisLocked();
    void processLocked(uint32_t inIdx, nsecs_t timestamp);
    void drainLocked();
    void queueOutputLocked();
    void finishOutputLocked(uint32_t outIdx, uint32_t inIdx, bool valid);
    void failLocked(Pending &p);
    void releaseInputLocked(int32_t inIdx);
    Pending *findPendingLocked(uint32_t frameNumber);

    bool mVdisEnabled;
    int32_t mFps;
    cam_dimension_t mInDim;
    uint32_t mInStride;
    uint32_t mInUvOffset;
    uint32_t mOutStride;
    uint32_t mOutUvOffset;
    uint32_t mOutSize;

    std::mutex mLock;
    std::condition_variable mCond;
    std::thread mWorker;
    bool mExit;
    bool mReturning;
    QCamera3Vdis mVdis;
    QCamera3StreamMem *mInMem;
    QCamera3HeapMemory *mOutMem;
    bool mOutBusy[NUM_OUTPUT_BUFFERS];
    std::deque<uint32_t> mFreeIn;
    std::deque<uint32_t> mFreeOut;
    std::deque<Pending> mPending;  /* framework buffers in request order */
    int32_t mLastIn;
    nsecs_t mLastInTime;
    nsecs_t mLastTimestamp;
};

}; // namespace qcamera

#endif /* __QCAMERA3VDISCHANNEL_H__ */
