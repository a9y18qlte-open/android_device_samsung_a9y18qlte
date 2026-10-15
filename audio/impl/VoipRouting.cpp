/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Samsung's audio HAL only moves its VoIP call (the voip-call usecase) to a
 * new device when the routing reaches the call's output while that output is
 * in standby. Routing an active output only reselects the output's own
 * usecase. VoIP apps pick their device right after the call starts, when the
 * output often runs already, so the call stayed on the earpiece while the app
 * showed the speaker.
 *
 * In communication mode, put an output in standby before routing it to
 * another device. Its next write starts it on the new device, as the HAL does
 * for every VoIP route change anyway.
 */

#define LOG_TAG "SamsungVoipRouting"

#include "VoipRouting.h"

#include <log/log.h>

#include <stdlib.h>
#include <string.h>

#include <map>
#include <mutex>

namespace samsung_voip_routing {

namespace {

std::mutex gLock;
audio_mode_t gMode = AUDIO_MODE_NORMAL;
std::map<audio_stream_t*, int> gRouting;

}  // namespace

void setMode(audio_mode_t mode) {
    std::lock_guard<std::mutex> lock(gLock);
    gMode = mode;
}

void beforeStreamParametersSet(audio_stream_t* stream, bool isInput, const char* keysAndValues) {
    if (isInput) return;
    const char* routing = strstr(keysAndValues, "routing=");
    if (routing == nullptr) return;
    int device = atoi(routing + strlen("routing="));
    if (device == 0) return;

    std::lock_guard<std::mutex> lock(gLock);
    auto it = gRouting.find(stream);
    bool changed = it != gRouting.end() && it->second != device;
    gRouting[stream] = device;
    if (changed && gMode == AUDIO_MODE_IN_COMMUNICATION) {
        ALOGD("Standby %p before routing it to %#x", stream, device);
        stream->standby(stream);
    }
}

void onStreamClosed(audio_stream_t* stream) {
    std::lock_guard<std::mutex> lock(gLock);
    gRouting.erase(stream);
}

}  // namespace samsung_voip_routing
