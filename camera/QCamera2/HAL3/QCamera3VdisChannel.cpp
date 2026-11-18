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

#define LOG_TAG "QCamera3VdisChannel"

#include <string.h>

#include <vector>

#include <utils/Timers.h>

#include "QCamera3HWI.h"
#include "QCamera3VdisChannel.h"

extern "C" {
#include "mm_camera_dbg.h"
}

#define VDIS_ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define IS_BUFFER_ERROR(x) (((x) & V4L2_BUF_FLAG_ERROR) == V4L2_BUF_FLAG_ERROR)

using namespace android;

namespace qcamera {

bool QCamera3VdisChannel::isEligible(uint32_t cameraId, const camera3_stream_t *stream,
        uint32_t opMode)
{
    int32_t inW, inH;
    return opMode == CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE &&
            stream->rotation == CAMERA3_STREAM_ROTATION_0 &&
            QCamera3Vdis::getInputSize(stream->width, stream->height, inW, inH) &&
            QCamera3Vdis::isAvailable(cameraId);
}

QCamera3VdisChannel::QCamera3VdisChannel(uint32_t cam_handle,
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
        uint32_t numBuffers)
    : QCamera3RegularChannel(cam_handle, channel_handle, cam_ops, cb_routine,
              cb_buffer_err, paddingInfo, userData, stream, stream_type,
              postprocess_mask, metadataChannel, numBuffers),
      mVdisEnabled(false),
      mFps(30),
      mInDim{0, 0},
      mInStride(0),
      mInUvOffset(0),
      mOutStride(0),
      mOutUvOffset(0),
      mOutSize(0),
      mExit(false),
      mReturning(false),
      mInMem(nullptr),
      mOutMem(nullptr),
      mOutBusy{},
      mLastIn(-1),
      mLastInTime(0),
      mLastTimestamp(0)
{
}

QCamera3VdisChannel::~QCamera3VdisChannel()
{
    /* stop here, while putStreamBufs() still reaches this class */
    if (m_bIsActive) {
        stop();
    }
    {
        std::lock_guard<std::mutex> lock(mLock);
        mExit = true;
    }
    mCond.notify_all();
    if (mWorker.joinable()) {
        mWorker.join();
    }
}

bool QCamera3VdisChannel::enableVdis(int32_t fps)
{
    int32_t inW, inH;
    if (fps > 60 || !QCamera3Vdis::getInputSize(mCamera3Stream->width,
            mCamera3Stream->height, inW, inH)) {
        return false;
    }
    mInDim.width = inW;
    mInDim.height = inH;
    mBackendDim = mInDim;
    mFps = fps > 0 ? fps : 30;

    /* NV12 venus, as gralloc allocates video encoder buffers */
    uint32_t w = mCamera3Stream->width, h = mCamera3Stream->height;
    mOutStride = VDIS_ALIGN(w, 128);
    mOutUvOffset = VDIS_ALIGN(mOutStride * VDIS_ALIGN(h, 32), 4096);
    mOutSize = VDIS_ALIGN(mOutUvOffset + mOutStride * VDIS_ALIGN(h / 2, 16), 4096);

    mVdisEnabled = true;
    LOGI("VDIS on: backend %dx%d for %dx%d", inW, inH, w, h);
    return true;
}

QCamera3StreamMem *QCamera3VdisChannel::getStreamBufs(uint32_t len)
{
    if (!mVdisEnabled) {
        return QCamera3RegularChannel::getStreamBufs(len);
    }

    std::lock_guard<std::mutex> lock(mLock);
    mInMem = new QCamera3StreamMem(NUM_INPUT_BUFFERS, false /* queued per request */);
    mOutMem = new QCamera3HeapMemory(NUM_OUTPUT_BUFFERS);
    if (mInMem->allocateAll(len) < 0 || mOutMem->allocate(mOutSize) < 0) {
        LOGE("VDIS buffer allocation failed");
        mInMem->deallocate();
        delete mInMem;
        mInMem = nullptr;
        mOutMem->deallocate();
        delete mOutMem;
        mOutMem = nullptr;
        return nullptr;
    }
    mFreeIn.clear();
    for (uint32_t i = 0; i < NUM_INPUT_BUFFERS; i++) {
        mFreeIn.push_back(i);
    }
    mFreeOut.clear();
    for (uint32_t i = 0; i < NUM_OUTPUT_BUFFERS; i++) {
        mFreeOut.push_back(i);
        mOutBusy[i] = false;
    }
    if (!mWorker.joinable()) {
        mWorker = std::thread(&QCamera3VdisChannel::workerLoop, this);
    }
    return mInMem;
}

void QCamera3VdisChannel::putStreamBufs()
{
    QCamera3RegularChannel::putStreamBufs();
    if (!mVdisEnabled) {
        return;
    }

    std::unique_lock<std::mutex> lock(mLock);
    mCond.wait(lock, [this] { return !mReturning; });
    resetVdisLocked();
    /* the HAL returns what is still pending as errors after stop */
    mPending.clear();
    mFreeIn.clear();
    mFreeOut.clear();
    if (mInMem != nullptr) {
        mInMem->deallocate();
        delete mInMem;
        mInMem = nullptr;
    }
    if (mOutMem != nullptr) {
        mOutMem->deallocate();
        delete mOutMem;
        mOutMem = nullptr;
    }
}

int32_t QCamera3VdisChannel::stop()
{
    int32_t rc = QCamera3RegularChannel::stop();
    if (mVdisEnabled) {
        std::unique_lock<std::mutex> lock(mLock);
        mCond.wait(lock, [this] { return !mReturning; });
        resetVdisLocked();
        mPending.clear();
    }
    return rc;
}

int32_t QCamera3VdisChannel::request(buffer_handle_t *buffer, uint32_t frameNumber,
        int &indexUsed)
{
    if (!mVdisEnabled) {
        return QCamera3RegularChannel::request(buffer, frameNumber, indexUsed);
    }
    if (buffer == nullptr) {
        return BAD_VALUE;
    }

    int32_t rc;
    if (!m_bIsActive) {
        rc = registerBuffer(buffer, mIsType);
        if (rc != NO_ERROR) {
            return rc;
        }
        rc = start();
        if (rc != NO_ERROR) {
            return rc;
        }
    }
    int index = mMemory.getMatchBufIndex((void *)buffer);
    if (index < 0) {
        rc = registerBuffer(buffer, mIsType);
        if (rc != NO_ERROR) {
            return rc;
        }
        index = mMemory.getMatchBufIndex((void *)buffer);
        if (index < 0) {
            return DEAD_OBJECT;
        }
    }
    if (mMemory.getSize(index) < (ssize_t)mOutSize) {
        LOGE("video buffer of %zd bytes, VDIS writes %u", mMemory.getSize(index), mOutSize);
        return BAD_VALUE;
    }

    std::lock_guard<std::mutex> lock(mLock);
    if (mFreeIn.empty()) {
        LOGE("no free VDIS input buffer for frame %u", frameNumber);
        return NO_MEMORY;
    }
    uint32_t in = mFreeIn.front();
    mFreeIn.pop_front();
    mInMem->markFrameNumber(in, frameNumber);
    mMemory.markFrameNumber(index, frameNumber);
    rc = mStreams[0]->bufDone(in);
    if (rc != NO_ERROR) {
        LOGE("failed to queue VDIS input %u", in);
        mInMem->markFrameNumber(in, -1);
        mMemory.markFrameNumber(index, -1);
        mFreeIn.push_front(in);
        return rc;
    }
    mPending.push_back({frameNumber, (int32_t)in, false, false, false});
    indexUsed = in;
    return NO_ERROR;
}

void QCamera3VdisChannel::streamCbRoutine(mm_camera_super_buf_t *super_frame,
        QCamera3Stream *stream)
{
    if (!mVdisEnabled) {
        QCamera3RegularChannel::streamCbRoutine(super_frame, stream);
        return;
    }
    if (checkStreamCbErrors(super_frame, stream) != NO_ERROR) {
        return;
    }

    mm_camera_buf_def_t *buf = super_frame->bufs[0];
    uint32_t in = buf->buf_idx;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mInMem == nullptr || in >= NUM_INPUT_BUFFERS) {
            LOGE("unexpected VDIS input %u", in);
        } else {
            int32_t frameNumber = mInMem->getFrameNumber(in);
            Pending *p = frameNumber >= 0 ? findPendingLocked(frameNumber) : nullptr;
            if (p == nullptr || p->done) {
                releaseInputLocked(in);
                if (p != nullptr) {
                    p->inIdx = -1;
                }
            } else {
                p->received = true;
                if (IS_BUFFER_ERROR(buf->flags) || !startVdisLocked()) {
                    failLocked(*p);
                } else {
                    mInMem->invalidateCache(in);
                    processLocked(in, buf->ts.tv_sec * 1000000000LL + buf->ts.tv_nsec);
                    mLastIn = in;
                    mLastInTime = systemTime(SYSTEM_TIME_MONOTONIC);
                }
            }
        }
    }
    mCond.notify_all();
    free(super_frame);
}

int32_t QCamera3VdisChannel::timeoutFrame(uint32_t frameNumber)
{
    if (!mVdisEnabled) {
        return QCamera3RegularChannel::timeoutFrame(frameNumber);
    }

    std::lock_guard<std::mutex> lock(mLock);
    Pending *p = findPendingLocked(frameNumber);
    if (p == nullptr || p->done) {
        return NO_ERROR;
    }
    if (!p->received) {
        /* still in the backend: it returns the buffer with an error */
        return mStreams[0]->timeoutFrame(p->inIdx);
    }
    LOGE("frame %u stuck in VDIS", frameNumber);
    resetVdisLocked();
    mCond.notify_all();
    return NO_ERROR;
}

void QCamera3VdisChannel::workerLoop()
{
    std::unique_lock<std::mutex> lock(mLock);
    while (!mExit) {
        mCond.wait_for(lock, std::chrono::milliseconds(50));
        if (mExit) {
            break;
        }

        if (mVdis.isActive() && mLastIn >= 0 &&
                systemTime(SYSTEM_TIME_MONOTONIC) - mLastInTime > STALL_NS) {
            drainLocked();
        }

        struct Result {
            uint32_t frameNumber;
            buffer_handle_t *buffer;
            bool error;
        };
        std::vector<Result> results;
        while (!mPending.empty() && mPending.front().done) {
            const Pending &p = mPending.front();
            int32_t index = mMemory.getGrallocBufferIndex(p.frameNumber);
            if (index >= 0) {
                results.push_back({p.frameNumber,
                        (buffer_handle_t *)mMemory.getBufferHandle(index), p.error});
                mMemory.markFrameNumber(index, -1);
            } else {
                LOGE("no video buffer for frame %u", p.frameNumber);
            }
            mPending.pop_front();
        }
        if (results.empty()) {
            continue;
        }

        mReturning = true;
        lock.unlock();
        for (const Result &r : results) {
            camera3_stream_buffer_t result = {};
            result.stream = mCamera3Stream;
            result.buffer = r.buffer;
            result.status = r.error ? CAMERA3_BUFFER_STATUS_ERROR : CAMERA3_BUFFER_STATUS_OK;
            result.acquire_fence = -1;
            result.release_fence = -1;
            if (r.error) {
                mChannelCbBufErr(this, r.frameNumber, CAMERA3_BUFFER_STATUS_ERROR, mUserData);
            }
            mChannelCB(NULL, &result, r.frameNumber, false, mUserData);
        }
        lock.lock();
        mReturning = false;
        mCond.notify_all();
    }
}

bool QCamera3VdisChannel::startVdisLocked()
{
    if (mVdis.isActive()) {
        return true;
    }
    cam_frame_len_offset_t offset;
    memset(&offset, 0, sizeof(offset));
    mStreams[0]->getFrameOffset(offset);
    mInStride = offset.mp[0].stride;
    mInUvOffset = offset.mp[0].len + offset.mp[1].offset;
    uint32_t cameraId = ((QCamera3HardwareInterface *)mUserData)->getCameraID();
    return mVdis.init(cameraId, mInDim.width, mInDim.height, mInStride,
            mCamera3Stream->width, mCamera3Stream->height, mOutStride, mFps) == NO_ERROR;
}

void QCamera3VdisChannel::resetVdisLocked()
{
    mVdis.deinit();
    mFreeOut.clear();
    for (uint32_t i = 0; i < NUM_OUTPUT_BUFFERS; i++) {
        mOutBusy[i] = false;
        if (mOutMem != nullptr) {
            mFreeOut.push_back(i);
        }
    }
    for (Pending &p : mPending) {
        if (p.received && !p.done) {
            failLocked(p);
        }
    }
    mLastIn = -1;
}

void QCamera3VdisChannel::processLocked(uint32_t in, nsecs_t timestamp)
{
    queueOutputLocked();
    QCamera3Vdis::Buffer buffer = {mInMem->getPtr(in), mInUvOffset, mInMem->getFd(in)};
    mVdis.process(in, buffer, timestamp);
    mLastTimestamp = timestamp;

    uint32_t outIdx, inIdx;
    bool valid;
    if (mVdis.getOutput(outIdx, inIdx, valid)) {
        finishOutputLocked(outIdx, inIdx, valid);
    }
}

void QCamera3VdisChannel::drainLocked()
{
    /* feed the last frame again until the frames before it come out */
    nsecs_t period = 1000000000LL / mFps;
    for (uint32_t i = 0; i < QCamera3Vdis::VDIS_DELAY + 2; i++) {
        bool waiting = false;
        for (const Pending &p : mPending) {
            waiting |= p.received && !p.done;
        }
        if (!waiting) {
            break;
        }
        processLocked(mLastIn, mLastTimestamp + period);
    }
    resetVdisLocked();
}

void QCamera3VdisChannel::queueOutputLocked()
{
    if (mFreeOut.empty()) {
        LOGE("no free VDIS output buffer");
        return;
    }
    uint32_t o = mFreeOut.front();
    mFreeOut.pop_front();
    mOutBusy[o] = true;
    QCamera3Vdis::Buffer buffer = {mOutMem->getPtr(o), mOutUvOffset, mOutMem->getFd(o)};
    mVdis.setOutput(o, buffer);
}

void QCamera3VdisChannel::finishOutputLocked(uint32_t outIdx, uint32_t inIdx, bool valid)
{
    if (outIdx >= NUM_OUTPUT_BUFFERS || !mOutBusy[outIdx]) {
        return;
    }

    Pending *p = nullptr;
    if (valid && inIdx < NUM_INPUT_BUFFERS) {
        int32_t frameNumber = mInMem->getFrameNumber(inIdx);
        p = frameNumber >= 0 ? findPendingLocked(frameNumber) : nullptr;
        if (p != nullptr && (p->done || p->inIdx != (int32_t)inIdx)) {
            p = nullptr;
        }
    }
    if (p != nullptr) {
        /* outputs come in frame order: earlier frames got none */
        for (Pending &q : mPending) {
            if (&q == p) {
                break;
            }
            if (q.received && !q.done) {
                failLocked(q);
            }
        }
        int32_t index = mMemory.getGrallocBufferIndex(p->frameNumber);
        void *dst = index >= 0 ? mMemory.getPtr(index) : nullptr;
        if (dst == nullptr) {
            failLocked(*p);
        } else {
            mOutMem->invalidateCache(outIdx);
            memcpy(dst, mOutMem->getPtr(outIdx), mOutSize);
            mMemory.cleanInvalidateCache(index);
            p->done = true;
            releaseInputLocked(p->inIdx);
            p->inIdx = -1;
        }
    }
    mOutBusy[outIdx] = false;
    mFreeOut.push_back(outIdx);
}

void QCamera3VdisChannel::failLocked(Pending &p)
{
    p.done = true;
    p.error = true;
    if (p.received && p.inIdx >= 0) {
        releaseInputLocked(p.inIdx);
        p.inIdx = -1;
    }
}

void QCamera3VdisChannel::releaseInputLocked(int32_t inIdx)
{
    if (inIdx < 0 || mInMem == nullptr) {
        return;
    }
    mInMem->markFrameNumber(inIdx, -1);
    mFreeIn.push_back(inIdx);
}

QCamera3VdisChannel::Pending *QCamera3VdisChannel::findPendingLocked(uint32_t frameNumber)
{
    for (Pending &p : mPending) {
        if (p.frameNumber == frameNumber) {
            return &p;
        }
    }
    return nullptr;
}

}; // namespace qcamera
