/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Samsung's audio HAL starts the modem voice session of whichever SIM it was
 * last told about via g_call_sim_slot, and ignores that parameter while it is
 * in call mode ("skip to change mCurModem to cp2 during call"). Stock Samsung
 * Telecom sends it right before setMode(IN_CALL); AOSP never does, so calls
 * ran on the wrong modem and had no audio.
 *
 * libsec-ril publishes the SIM of the ongoing call in vendor.calls.slotid
 * (renamed from ril.dds.call.slotid by extract-files.sh): -1 no call, 0 SIM 1,
 * 1 SIM 2. For outgoing calls Telecom enters call mode before telephony has
 * even sent DIAL to the RIL, sometimes seconds before, and it cannot be waited
 * for in setMode() without stalling Telecom. So when the SIM is not known yet,
 * call mode is held back from the stock HAL until the RIL reports it. The
 * stock HAL starts the voice session when the output that drives the call is
 * routed while in call mode; policy has already routed it by then, so that
 * routing is replayed.
 */

#define LOG_TAG "SamsungSimSlot"

#include "SimSlot.h"

#include <cutils/properties.h>
#include <log/log.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mutex>
#include <string>
#include <thread>

namespace samsung_sim_slot {

namespace {

constexpr int kPollMs = 50;
constexpr int kMaxWaitMs = 20000;

std::mutex gLock;
audio_hw_device_t* gDevice;
audio_stream_out_t* gPrimaryOut;
std::string gPrimaryOutRouting;  // last non-zero "routing=" of gPrimaryOut
bool gCallPending;
unsigned gCallGeneration;

int callSlot() {
    char slot[PROPERTY_VALUE_MAX];
    property_get("vendor.calls.slotid", slot, "-1");
    if (!strcmp(slot, "0")) return 0;
    if (!strcmp(slot, "1")) return 1;
    return -1;
}

// gLock must be held.
int startCallLocked(int slot) {
    if (slot >= 0) {
        gDevice->set_parameters(gDevice, slot == 1 ? "g_call_sim_slot=0x02" : "g_call_sim_slot=0x01");
    }
    return gDevice->set_mode(gDevice, AUDIO_MODE_IN_CALL);
}

// gLock must be held.
void startPendingCallLocked(int slot) {
    gCallPending = false;
    startCallLocked(slot);
    if (gPrimaryOut != nullptr && !gPrimaryOutRouting.empty()) {
        gPrimaryOut->common.set_parameters(&gPrimaryOut->common, gPrimaryOutRouting.c_str());
    }
}

void waitForCallSlot(unsigned generation) {
    for (int waitedMs = kPollMs; waitedMs <= kMaxWaitMs; waitedMs += kPollMs) {
        usleep(kPollMs * 1000);
        int slot = callSlot();

        std::lock_guard<std::mutex> lock(gLock);
        if (!gCallPending || gCallGeneration != generation) return;
        if (slot >= 0) {
            ALOGI("Call on SIM %d, reported after %d ms", slot + 1, waitedMs);
            startPendingCallLocked(slot);
            return;
        }
    }

    std::lock_guard<std::mutex> lock(gLock);
    if (!gCallPending || gCallGeneration != generation) return;
    ALOGW("No SIM reported for the call, starting it on the current one");
    startPendingCallLocked(-1);
}

}  // namespace

void onOutputOpened(audio_stream_out_t* stream, audio_output_flags_t flags) {
    if (!(flags & AUDIO_OUTPUT_FLAG_PRIMARY)) return;
    std::lock_guard<std::mutex> lock(gLock);
    gPrimaryOut = stream;
    gPrimaryOutRouting.clear();
}

void onOutputClosed(audio_stream_out_t* stream) {
    std::lock_guard<std::mutex> lock(gLock);
    if (gPrimaryOut != stream) return;
    gPrimaryOut = nullptr;
    gPrimaryOutRouting.clear();
}

void onStreamParametersSet(audio_stream_t* stream, const char* keysAndValues) {
    const char* routing = strstr(keysAndValues, "routing=");
    if (routing == nullptr) return;
    int device = atoi(routing + strlen("routing="));
    if (device == 0) return;

    std::lock_guard<std::mutex> lock(gLock);
    if (gPrimaryOut == nullptr || stream != &gPrimaryOut->common) return;
    gPrimaryOutRouting = "routing=" + std::to_string(device);
}

int setMode(audio_hw_device_t* device, audio_mode_t mode) {
    std::lock_guard<std::mutex> lock(gLock);
    gDevice = device;

    if (mode != AUDIO_MODE_IN_CALL) {
        gCallPending = false;
        return device->set_mode(device, mode);
    }
    if (gCallPending) return 0;

    int slot = callSlot();
    if (slot >= 0) {
        ALOGI("Call on SIM %d", slot + 1);
        return startCallLocked(slot);
    }

    ALOGI("SIM of the call not reported yet, holding back call mode");
    gCallPending = true;
    std::thread(waitForCallSlot, ++gCallGeneration).detach();
    return 0;
}

}  // namespace samsung_sim_slot
