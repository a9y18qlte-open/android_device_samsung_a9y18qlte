/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <hardware/audio.h>

// Tells Samsung's audio HAL which SIM a call is on; see SimSlot.cpp.
namespace samsung_sim_slot {

void onOutputOpened(audio_stream_out_t* stream, audio_output_flags_t flags);
void onOutputClosed(audio_stream_out_t* stream);
void onStreamParametersSet(audio_stream_t* stream, const char* keysAndValues);
int setMode(audio_hw_device_t* device, audio_mode_t mode);

}  // namespace samsung_sim_slot
