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

#include <new>

#include <android/content/AttributionSourceState.h>
#include <binder/Binder.h>
#include <camera/Camera.h>
#include <gui/Surface.h>
#include <media/AudioRecord.h>
#include <utils/String8.h>

using android::AudioRecord;
using android::Camera;
using android::sp;
using android::String16;
using android::String8;
using android::Surface;
using android::content::AttributionSourceState;

extern "C" {

// AudioRecord::AudioRecord(const String16& opPackageName)
// The blob allocates sizeof(AudioRecord) of Android 10; its blob fixup raises that
// allocation to the Android 12 size. uid and pid are filled in by set(); recording is
// refused without an attribution token.
void _ZN7android11AudioRecordC1ERKNS_8String16E(AudioRecord* self,
                                                 const String16& opPackageName) {
    AttributionSourceState attributionSource;
    attributionSource.packageName = std::string(String8(opPackageName).string());
    attributionSource.token = sp<android::BBinder>::make();
    new (self) AudioRecord(attributionSource);
}

// status_t AudioRecord::set(...) without maxSharedAudioHistoryMs
// Samsung's audio engine records from its own source (13) for VoWiFi calls, which
// Android 12 rejects; record the call from the VoIP source instead.
android::status_t
_ZN7android11AudioRecord3setE14audio_source_tj14audio_format_tjmPFviPvS3_ES3_jb15audio_session_tNS0_13transfer_typeE19audio_input_flags_tjiPK18audio_attributes_ti28audio_microphone_direction_tf(
        AudioRecord* self, audio_source_t inputSource, uint32_t sampleRate, audio_format_t format,
        audio_channel_mask_t channelMask, size_t frameCount, AudioRecord::callback_t cbf,
        void* user, uint32_t notificationFrames, bool threadCanCallJava,
        audio_session_t sessionId, AudioRecord::transfer_type transferType,
        audio_input_flags_t flags, uid_t uid, pid_t pid, const audio_attributes_t* pAttributes,
        audio_port_handle_t selectedDeviceId, audio_microphone_direction_t selectedMicDirection,
        float microphoneFieldDimension) {
    if (inputSource >= AUDIO_SOURCE_CNT && inputSource != AUDIO_SOURCE_ECHO_REFERENCE &&
        inputSource != AUDIO_SOURCE_FM_TUNER && inputSource != AUDIO_SOURCE_HOTWORD) {
        inputSource = AUDIO_SOURCE_VOICE_COMMUNICATION;
    }
    return self->set(inputSource, sampleRate, format, channelMask, frameCount, cbf, user,
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
    return Camera::connect(cameraId, clientPackageName, clientUid, clientPid,
                           29 /* targetSdkVersion, the blobs target Android 10 */);
}
