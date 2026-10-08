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

#include <map>
#include <mutex>
#include <new>

#include <android/content/AttributionSourceState.h>
#include <binder/Binder.h>
#include <camera/Camera.h>
#include <camera/CameraBase.h>
#include <binder/Parcel.h>
#include <gui/Surface.h>
#include <media/AudioRecord.h>
#include <media/AudioTrack.h>
#include <ui/GraphicBufferMapper.h>
#include <utils/String8.h>

using android::AudioRecord;
using android::AudioTrack;
using android::Camera;
using android::RefBase;
using android::CameraBase;
using android::sp;
using android::String16;
using android::String8;
using android::Surface;
using android::content::AttributionSourceState;

namespace {

// Android 14 removed AudioRecord's legacy callback interface; Android 13's adapter.
typedef void (*legacy_callback_t)(int event, void* user, void* info);
enum {
    EVENT_MORE_DATA = 0,
    EVENT_OVERRUN = 1,
    EVENT_MARKER = 2,
    EVENT_NEW_POS = 3,
    EVENT_NEW_IAUDIORECORD = 4,
};

class LegacyCallbackWrapper : public AudioRecord::IAudioRecordCallback {
    const legacy_callback_t mCallback;
    void* const mData;

  public:
    LegacyCallbackWrapper(legacy_callback_t callback, void* user)
        : mCallback(callback), mData(user) {}

    size_t onMoreData(const AudioRecord::Buffer& buffer) override {
        AudioRecord::Buffer copy = buffer;
        mCallback(EVENT_MORE_DATA, mData, &copy);
        return copy.size();
    }

    void onOverrun() override { mCallback(EVENT_OVERRUN, mData, nullptr); }

    void onMarker(uint32_t markerPosition) override {
        mCallback(EVENT_MARKER, mData, &markerPosition);
    }

    void onNewPos(uint32_t newPos) override { mCallback(EVENT_NEW_POS, mData, &newPos); }

    void onNewIAudioRecord() override { mCallback(EVENT_NEW_IAUDIORECORD, mData, nullptr); }
};

// AudioRecord only keeps a weak reference to its callback. Keep each wrapper alive
// until the next set() on the same object address; the blob creates one AudioRecord
// per call, so this stays at a handful of small objects.
std::mutex gCallbackWrappersLock;
std::map<const AudioRecord*, sp<LegacyCallbackWrapper>> gCallbackWrappers;

}  // namespace

extern "C" {

// AudioRecord::AudioRecord(const String16& opPackageName)
// The blob allocates sizeof(AudioRecord) of Android 10; its blob fixup raises that
// allocation to the Android 12 size. uid and pid are filled in by set(); recording is
// refused without an attribution token.
void _ZN7android11AudioRecordC1ERKNS_8String16E(AudioRecord* self,
                                                 const String16& opPackageName) {
    AttributionSourceState attributionSource;
    attributionSource.packageName = std::string(String8(opPackageName).c_str());
    attributionSource.token = sp<android::BBinder>::make();
    new (self) AudioRecord(attributionSource);
}

// status_t AudioRecord::set(...) without maxSharedAudioHistoryMs
// Samsung's audio engine records from its own source (13) for VoWiFi calls, which
// Android 12 rejects; record the call from the VoIP source instead.
android::status_t
_ZN7android11AudioRecord3setE14audio_source_tj14audio_format_tjmPFviPvS3_ES3_jb15audio_session_tNS0_13transfer_typeE19audio_input_flags_tjiPK18audio_attributes_ti28audio_microphone_direction_tf(
        AudioRecord* self, audio_source_t inputSource, uint32_t sampleRate, audio_format_t format,
        audio_channel_mask_t channelMask, size_t frameCount, legacy_callback_t cbf,
        void* user, uint32_t notificationFrames, bool threadCanCallJava,
        audio_session_t sessionId, AudioRecord::transfer_type transferType,
        audio_input_flags_t flags, uid_t uid, pid_t pid, const audio_attributes_t* pAttributes,
        audio_port_handle_t selectedDeviceId, audio_microphone_direction_t selectedMicDirection,
        float microphoneFieldDimension) {
    if (inputSource >= AUDIO_SOURCE_CNT && inputSource != AUDIO_SOURCE_ECHO_REFERENCE &&
        inputSource != AUDIO_SOURCE_FM_TUNER && inputSource != AUDIO_SOURCE_HOTWORD) {
        inputSource = AUDIO_SOURCE_VOICE_COMMUNICATION;
    }
    sp<LegacyCallbackWrapper> callback;
    if (cbf != nullptr) callback = sp<LegacyCallbackWrapper>::make(cbf, user);
    {
        std::lock_guard<std::mutex> lock(gCallbackWrappersLock);
        if (callback != nullptr) {
            gCallbackWrappers[self] = callback;
        } else {
            gCallbackWrappers.erase(self);
        }
    }
    return self->set(inputSource, sampleRate, format, channelMask, frameCount, callback,
                     notificationFrames, threadCanCallJava, sessionId, transferType, flags, uid,
                     pid, pAttributes, selectedDeviceId, selectedMicDirection,
                     microphoneFieldDimension, 0 /* maxSharedAudioHistoryMs */);
}

// static bool Surface::isValid(const sp<Surface>&), inline until Android 11 removed it
bool _ZN7android7Surface7isValidERKNS_2spIS0_EE(const sp<Surface>& surface) {
    return surface != nullptr && surface->getIGraphicBufferProducer() != nullptr;
}

}  // extern "C"

// static sp<Camera> Camera::connect(int, const String16&, int, int)
sp<Camera> CameraConnect(int cameraId, const String16& clientPackageName, int clientUid,
                         int clientPid) __asm__("_ZN7android6Camera7connectEiRKNS_8String16Eii");
sp<Camera> CameraConnect(int cameraId, const String16& clientPackageName, int clientUid,
                         int clientPid) {
    return Camera::connect(cameraId, std::string(String8(clientPackageName).c_str()), clientUid,
                           clientPid,
                           29 /* targetSdkVersion, the blobs target Android 10 */,
                           false /* overrideToPortrait */, false /* forceSlowJpegMode */);
}

// static status_t CameraBase<Camera>::getCameraInfo(int, CameraInfo*)
android::status_t CameraGetCameraInfo(int cameraId, android::hardware::CameraInfo* cameraInfo)
        __asm__("_ZN7android10CameraBaseINS_6CameraENS_12CameraTraitsIS1_EEE13getCameraInfoEiPNS_8hardware10CameraInfoE");
android::status_t CameraGetCameraInfo(int cameraId, android::hardware::CameraInfo* cameraInfo) {
    return CameraBase<Camera>::getCameraInfo(cameraId, false /* overrideToPortrait */, cameraInfo);
}

// RefBase::incStrong() and decStrong() for libAudioFWInterface.so, whose imports of
// them are renamed to SveBase (same length, see extract-files.sh). Android 13 made AudioSystem::AudioDeviceCallback a
// virtual RefBase, which moved the RefBase of AudioTrack and AudioRecord away from
// the start of the object; the blob still passes the object address as the RefBase.
static const RefBase* toRefBase(const void* object) {
    static const void* const kAudioTrackVtable = [] {
        sp<AudioTrack> track = sp<AudioTrack>::make();
        return *reinterpret_cast<const void* const*>(track.get());
    }();
    static const void* const kAudioRecordVtable = [] {
        sp<AudioRecord> record = sp<AudioRecord>::make(AttributionSourceState());
        return *reinterpret_cast<const void* const*>(record.get());
    }();
    const void* vtable = *static_cast<const void* const*>(object);
    if (vtable == kAudioTrackVtable) return static_cast<const AudioTrack*>(object);
    if (vtable == kAudioRecordVtable) return static_cast<const AudioRecord*>(object);
    return static_cast<const RefBase*>(object);
}

extern "C" void sve_RefBase_incStrong(const void* object, const void* id)
        __asm__("_ZNK7android7SveBase9incStrongEPKv");
extern "C" void sve_RefBase_incStrong(const void* object, const void* id) {
    toRefBase(object)->incStrong(id);
}

extern "C" void sve_RefBase_decStrong(const void* object, const void* id)
        __asm__("_ZNK7android7SveBase9decStrongEPKv");
extern "C" void sve_RefBase_decStrong(const void* object, const void* id) {
    toRefBase(object)->decStrong(id);
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
