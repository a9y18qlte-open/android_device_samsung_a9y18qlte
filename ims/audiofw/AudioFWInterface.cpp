/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

// The wave in/out interface of Samsung's VoWiFi audio engine (libsvejni,
// libRecorder): one AudioRecord and one AudioTrack, both mono 16-bit PCM,
// read and written in blocking mode.

#define LOG_TAG "AudioFWInterface"

#include <android/content/AttributionSourceState.h>
#include <binder/Binder.h>
#include <log/log.h>
#include <media/AudioRecord.h>
#include <media/AudioTrack.h>

using android::AudioRecord;
using android::AudioTrack;
using android::BBinder;
using android::NO_ERROR;
using android::sp;
using android::content::AttributionSourceState;

namespace {

sp<AudioRecord> gWaveIn;
sp<AudioTrack> gWaveOut;
uint32_t gWaveInRate;
uint32_t gWaveOutRate;

// The engine's direction: 1 uplink, 2 downlink, 3 both, 4 microphone. Anything
// else is Samsung's own source 13, which AOSP rejects; record the call from the
// VoIP source instead.
constexpr audio_source_t waveInSource(int direction) {
    switch (direction) {
        case 1: return AUDIO_SOURCE_VOICE_UPLINK;
        case 2: return AUDIO_SOURCE_VOICE_DOWNLINK;
        case 3: return AUDIO_SOURCE_VOICE_CALL;
        case 4: return AUDIO_SOURCE_MIC;
        default: return AUDIO_SOURCE_VOICE_COMMUNICATION;
    }
}
static_assert(waveInSource(1) == AUDIO_SOURCE_VOICE_UPLINK);
static_assert(waveInSource(3) == AUDIO_SOURCE_VOICE_CALL);
static_assert(waveInSource(4) == AUDIO_SOURCE_MIC);
static_assert(waveInSource(0) == AUDIO_SOURCE_VOICE_COMMUNICATION);

}  // namespace

extern "C" {

void AUDIOFW_SET_WAVEINFORMAT(int sampleRate, int channels) {
    ALOGI("AUDIOFW_SET_WAVEINFORMAT %d Hz, %d ch", sampleRate, channels);
    gWaveInRate = sampleRate;
}

void AUDIOFW_SET_WAVEOUTFORMAT(int sampleRate, int channels) {
    ALOGI("AUDIOFW_SET_WAVEOUTFORMAT %d Hz, %d ch", sampleRate, channels);
    gWaveOutRate = sampleRate;
}

int AUDIOFW_WAVEINOPEN(bool voiceCall, int direction) {
    ALOGI("AUDIOFW_WAVEINOPEN(), VoiceCall: %d, Direction:%d", voiceCall, direction);
    if (gWaveIn == nullptr) {
        AttributionSourceState attributionSource;
        attributionSource.packageName = "com.sec.sve";
        // Recording is refused without an attribution token.
        attributionSource.token = sp<BBinder>::make();
        gWaveIn = sp<AudioRecord>::make(attributionSource);
    }
    android::status_t status = gWaveIn->set(waveInSource(direction), gWaveInRate,
                                            AUDIO_FORMAT_PCM_16_BIT, AUDIO_CHANNEL_IN_MONO,
                                            0 /* frameCount */, nullptr /* callback */,
                                            0 /* notificationFrames */,
                                            false /* threadCanCallJava */,
                                            AUDIO_SESSION_ALLOCATE, AudioRecord::TRANSFER_SYNC);
    if (status != NO_ERROR) {
        ALOGE("WaveInOpen: fail to set AudioRecord (retVal = %d)", status);
        return -1;
    }
    status = gWaveIn->initCheck();
    if (status != NO_ERROR) {
        ALOGE("WaveInOpen: fail to init AudioRecord (%d)", status);
        return -1;
    }
    return 0;
}

int AUDIOFW_WAVEINSTART() {
    if (gWaveIn == nullptr) return -1;
    return gWaveIn->start() == NO_ERROR ? 0 : -1;
}

int AUDIOFW_WAVEREAD(char* buffer, unsigned int size) {
    if (gWaveIn == nullptr) return -1;
    return gWaveIn->read(buffer, size, true /* blocking */);
}

void AUDIOFW_WAVEINCLOSE() {
    if (gWaveIn != nullptr) gWaveIn->stop();
    gWaveIn.clear();
}

void AUDIOFW_WAVEINHANDLE_CLEAR() {
    gWaveIn.clear();
}

int AUDIOFW_WAVEINHANDLE_CHECK() {
    return gWaveIn != nullptr;
}

int AUDIOFW_WAVEOUTOPEN(int* minFrameCount, int* sessionId) {
    ALOGI("AUDIOFW_WAVEOUTOPEN()");
    if (gWaveOut == nullptr) gWaveOut = sp<AudioTrack>::make();

    // Samsung plays the call on its own stream type 15, which AOSP rejects.
    const audio_stream_type_t stream = AUDIO_STREAM_VOICE_CALL;
    size_t frameCount = 0;
    AudioTrack::getMinFrameCount(&frameCount, stream, gWaveOutRate);
    *minFrameCount = frameCount;
    ALOGI("WaveOutOpen (minFrameCount = %zu)", frameCount);

    AttributionSourceState attributionSource;
    attributionSource.token = sp<BBinder>::make();
    gWaveOut->set(stream, gWaveOutRate, AUDIO_FORMAT_PCM_16_BIT, AUDIO_CHANNEL_OUT_MONO,
                  frameCount, AUDIO_OUTPUT_FLAG_NONE, nullptr /* callback */,
                  0 /* notificationFrames */, nullptr /* sharedBuffer */,
                  false /* threadCanCallJava */, AUDIO_SESSION_ALLOCATE,
                  AudioTrack::TRANSFER_DEFAULT, nullptr /* offloadInfo */, attributionSource);
    android::status_t status = gWaveOut->initCheck();
    if (status != NO_ERROR) {
        ALOGE("WaveOutOpen: fail to init AudioTrack (%d)", status);
        return -1;
    }
    gWaveOut->start();
    *sessionId = gWaveOut->getSessionId();
    ALOGI("WaveOutOpen sessionId %d", *sessionId);
    return 0;
}

int AUDIOFW_WAVEWRITE(char* buffer, unsigned int size) {
    if (gWaveOut == nullptr) return -1;
    ssize_t written = gWaveOut->write(buffer, size, true /* blocking */);
    if (written != static_cast<ssize_t>(size)) ALOGE("write error: %zd", written);
    return written;
}

void AUDIOFW_WAVEOUTCLOSE() {
    if (gWaveOut != nullptr) gWaveOut->stop();
    gWaveOut.clear();
}

int AUDIOFW_WAVEOUTHANDLE_CHECK() {
    return gWaveOut != nullptr;
}

int AUDIOFW_WAVEHANDLE_CHECK() {
    return gWaveIn != nullptr || gWaveOut != nullptr;
}

int AUDIOFW_SOUNDDEVICE_GET_REMAINCHUNK() {
    if (gWaveOut == nullptr || gWaveOut->initCheck() != NO_ERROR) return 0;
    uint32_t position = 0;
    gWaveOut->getPosition(&position);
    return position;
}

}  // extern "C"
