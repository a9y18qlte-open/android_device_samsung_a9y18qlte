/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <hardware/audio.h>

// Moves VoIP calls with their output's routing; see VoipRouting.cpp.
namespace samsung_voip_routing {

void setMode(audio_mode_t mode);
void beforeStreamParametersSet(audio_stream_t* stream, bool isInput, const char* keysAndValues);
void onStreamClosed(audio_stream_t* stream);

}  // namespace samsung_voip_routing
