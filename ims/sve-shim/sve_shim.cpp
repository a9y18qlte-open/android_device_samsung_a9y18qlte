/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <mutex>

#include <android/content/AttributionSourceState.h>
#include <camera/Camera.h>
#include <camera/CameraBase.h>
#include <binder/Parcel.h>
#include <gui/BufferItemConsumer.h>
#include <gui/BufferQueue.h>
#include <gui/Flags.h>
#include <gui/Surface.h>
#include <ui/GraphicBuffer.h>
#include <ui/GraphicBufferMapper.h>
#include <utils/String8.h>

using android::BufferItem;
using android::BufferItemConsumer;
using android::BufferQueue;
using android::Camera;
using android::GraphicBuffer;
using android::IGraphicBufferConsumer;
using android::IGraphicBufferProducer;
using android::CameraBase;
using android::sp;
using android::String16;
using android::String8;
using android::Surface;
using android::content::AttributionSourceState;
using android::content::res::CameraCompatibilityInfo;

extern "C" {

// static bool Surface::isValid(const sp<Surface>&), inline until Android 11 removed it
bool _ZN7android7Surface7isValidERKNS_2spIS0_EE(const sp<Surface>& surface) {
    return surface != nullptr && surface->getIGraphicBufferProducer() != nullptr;
}

}  // extern "C"

namespace {

// Returns the buffers the camera fills straight back to the queue, so the
// producer never blocks on a target nobody reads.
class BufferDrainer : public BufferItemConsumer::FrameAvailableListener {
    const sp<BufferItemConsumer> mConsumer;

  public:
    explicit BufferDrainer(const sp<BufferItemConsumer>& consumer) : mConsumer(consumer) {}

    void onFrameAvailable(const android::BufferItem&) override {
        BufferItem item;
        // Drain everything queued: one acquire per callback starves the camera
        // as soon as two frames land between two listener wake-ups.
        while (mConsumer->acquireBuffer(&item, 0 /* presentWhen */) == android::OK) {
            mConsumer->releaseBuffer(item);
        }
    }
};

class DummyBufferConsumer : public BufferItemConsumer {
  public:
    DummyBufferConsumer(const sp<IGraphicBufferConsumer>& consumer, uint64_t usage, int count)
        : BufferItemConsumer(consumer, usage, count) {}
};

void setDummyPreviewTarget(const sp<Camera>& camera) {
    static std::mutex mutex;
    static sp<BufferItemConsumer> consumer;
    static sp<Surface> surface;

    std::lock_guard<std::mutex> lock(mutex);
    if (consumer == nullptr) {
        sp<IGraphicBufferProducer> producer;
        sp<IGraphicBufferConsumer> bufferConsumer;
        BufferQueue::createBufferQueue(&producer, &bufferConsumer);
        consumer = new DummyBufferConsumer(bufferConsumer, GraphicBuffer::USAGE_HW_TEXTURE,
                                          4 /* bufferCount */);
        consumer->setFrameAvailableListener(new BufferDrainer(consumer));
        // Nobody looks at these frames, so let the camera overwrite the oldest
        // one rather than fail its requests when the queue runs dry.
        producer->setAsyncMode(true);
        surface = new Surface(producer);
    }
#if WB_LIBCAMERASERVICE_WITH_DEPENDENCIES
    camera->setPreviewTarget(surface);
#else
    camera->setPreviewTarget(surface->getIGraphicBufferProducer());
#endif
}

}  // namespace

// static sp<Camera> Camera::connect(int, const String16&, int, int)
sp<Camera> CameraConnect(int cameraId, const String16& clientPackageName, int clientUid,
                         int clientPid) __asm__("_ZN7android6Camera7connectEiRKNS_8String16Eii");
sp<Camera> CameraConnect(int cameraId, const String16& clientPackageName, int clientUid,
                         int clientPid) {
    AttributionSourceState clientAttribution;
    clientAttribution.packageName = std::string(String8(clientPackageName).c_str());
    clientAttribution.uid = clientUid;
    clientAttribution.pid = clientPid;
    CameraCompatibilityInfo compatInfo;
    sp<Camera> camera =
            Camera::connect(cameraId, 29 /* targetSdkVersion */, compatInfo,
                            false /* forceSlowJpegMode */, clientAttribution,
                            0 /* devicePolicy */);
    // The video engine renders the preview itself from the frame callbacks and
    // never sets a preview target, which Samsung's camera service allowed.
    // Camera2Client::startPreviewL() leaves the client in
    // WAITING_FOR_PREVIEW_WINDOW and configures no stream at all without one,
    // so the camera never streams and no callback ever arrives. Give it a
    // target of our own, which nothing reads, to get the streams configured.
    if (camera != nullptr) {
        setDummyPreviewTarget(camera);
    }
    return camera;
}

// static status_t CameraBase<Camera>::getCameraInfo(int, CameraInfo*)
android::status_t CameraGetCameraInfo(int cameraId, android::hardware::CameraInfo* cameraInfo)
        __asm__("_ZN7android10CameraBaseINS_6CameraENS_12CameraTraitsIS1_EEE13getCameraInfoEiPNS_8hardware10CameraInfoE");
android::status_t CameraGetCameraInfo(int cameraId, android::hardware::CameraInfo* cameraInfo) {
    AttributionSourceState clientAttribution;
    CameraCompatibilityInfo compatInfo;
    return CameraBase<Camera>::getCameraInfo(cameraId, compatInfo, clientAttribution, 0 /* deviceAolicy */, cameraInfo);
}

// static int32_t CameraBase<Camera>::getNumberOfCameras()
int32_t CameraGetNumberOfCameras()
        __asm__("_ZN7android10CameraBaseINS_6CameraENS_12CameraTraitsIS1_EEE18getNumberOfCamerasEv");
int32_t CameraGetNumberOfCameras() {
    AttributionSourceState clientAttribution;
    return CameraBase<Camera>::getNumberOfCameras(clientAttribution, 0 /* devicePolicy */);
}

// Parcel::print(TextOutput&, uint32_t) for libsveservice.so; Android 14 takes a
// std::ostream instead. It only dumps the parcel for debugging.
extern "C" void sve_Parcel_print(const void* parcel, void* to, uint32_t flags)
        __asm__("_ZNK7android6Parcel5printERNS_10TextOutputEj");
extern "C" void sve_Parcel_print(const void*, void*, uint32_t) {}

// GraphicBufferMapper::unlock(buffer_handle_t) for libsamsung_videoengine_9_0.so;
// Android 14 added an optional out-fence argument.
extern "C" android::status_t sve_GraphicBufferMapper_unlock(android::GraphicBufferMapper* mapper,
                                                           buffer_handle_t handle)
        __asm__("_ZN7android19GraphicBufferMapper6unlockEPK13native_handle");
extern "C" android::status_t sve_GraphicBufferMapper_unlock(android::GraphicBufferMapper* mapper,
                                                           buffer_handle_t handle) {
    return mapper->unlock(handle, nullptr);
}
