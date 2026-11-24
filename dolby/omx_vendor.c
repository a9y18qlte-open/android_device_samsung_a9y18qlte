/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "log2_table.h"

#define NO_SINE 5
#define MAX_SBG 22

#define STATE_RESET 0x4
#define CHANGE_MASK 0xf0
#define CHANGE_COLLECT 0x10
#define CHANGE_FLUSH 0x20

static float log2_div8(int i) {
    float f;
    memcpy(&f, &kLog2Table[i], sizeof(f));
    return f;
}

int function_a(int sb_start, int sb_stop) {
    long log2_ratio_q10 = lrintf(ldexpf(log2_div8(sb_stop) - log2_div8(sb_start), -2) * 32768.0f);
    if (log2_ratio_q10 < -32768) log2_ratio_q10 = -32768;
    if (log2_ratio_q10 > 32767) log2_ratio_q10 = 32767;
    return (int)log2_ratio_q10;
}

void function_b(uint32_t num_sbg, uint32_t first_sbg, int32_t start_env, uint32_t harmonic_bits,
                int32_t* sine_start_env) {
    if (first_sbg + num_sbg < first_sbg) __builtin_trap();
    for (uint32_t sbg = 0; sbg < first_sbg; sbg++) sine_start_env[sbg] = NO_SINE;
    uint32_t end = first_sbg;
    for (uint32_t i = 0; first_sbg + i < first_sbg + num_sbg; i++) {
        if (i > 31) __builtin_trap();
        int32_t* env = &sine_start_env[first_sbg + i];
        if (!(harmonic_bits & (1u << (31 - i))))
            *env = NO_SINE;
        else if (start_env == -1 || *env != NO_SINE)
            *env = 0;
        else
            *env = start_env;
        end = first_sbg + i + 1;
    }
    for (uint32_t sbg = end; sbg < MAX_SBG; sbg++) sine_start_env[sbg] = NO_SINE;
}

int function_c(uint32_t prev_incomplete, uint32_t seq_counter, uint32_t prev_fraction,
               uint32_t fraction, uint32_t subframes_needed, uint32_t subframes_per_frame,
               uint32_t* change_state, uint32_t* subframes, uint32_t* frames, uint32_t* position,
               uint32_t* slot_state, uint32_t* out) {
    if (prev_fraction == 0 || fraction == 0 || seq_counter == 0) __builtin_trap();
    uint32_t pos = seq_counter & (fraction - 1);
    uint32_t state = *change_state;
    if ((state & 0xf) == STATE_RESET ||
        (pos == 0 && prev_incomplete == 0 && (state & CHANGE_MASK) == CHANGE_COLLECT)) {
        *subframes = 0;
        *frames = 0;
        state = *change_state;
    } else if (*subframes == 0) {
        *frames = 0;
        state = *change_state;
    }
    if ((state & CHANGE_MASK) == CHANGE_FLUSH) {
        if (subframes_per_frame < subframes_needed) prev_incomplete = 1;
        out[2] = prev_incomplete;
        out[1] = fraction == 1;
        out[0] = *change_state;
        return 0;
    }
    uint32_t first = 0;
    uint32_t count;
    if (pos == 0 || (state & 0xf) == STATE_RESET) {
        count = 0;
        *position = count;
    } else if ((state & CHANGE_MASK) != CHANGE_COLLECT) {
        count = *position;
    } else if (((fraction - 1) & (seq_counter - 1)) != 0) {
        if (*position == UINT32_MAX) __builtin_trap();
        count = *position + 1;
        *position = count;
    } else {
        first = 1 < fraction;
        *position = first;
        count = first;
    }
    if (count == UINT32_MAX) __builtin_trap();
    *position = count + 1;
    out[1] = count + 1 == fraction;
    if (*position < first) __builtin_trap();
    if (*position - first == 1) {
        if (*subframes + subframes_per_frame < *subframes) __builtin_trap();
        *subframes += subframes_per_frame;
        if (*frames == UINT32_MAX) __builtin_trap();
        *frames += 1;
    }
    out[2] = *subframes < subframes_needed;
    if (*subframes >= subframes_needed && out[1] != 0) *subframes = 0;
    state = *change_state;
    int prev_group_done = 0;
    if ((state & CHANGE_MASK) == CHANGE_COLLECT) {
        uint32_t prev_mask = prev_fraction - 1;
        uint32_t prev_pos = prev_mask & (seq_counter - 1);
        if (1 < prev_fraction && 1 < fraction) {
            state = (state & 0xf) | CHANGE_COLLECT;
            *change_state = state;
            prev_group_done = prev_pos == prev_mask;
        } else if (prev_fraction < 2 || fraction != 1) {
            if (prev_fraction == 1 && fraction >= 2 && pos != 0) {
                state = (state & 0xf) | CHANGE_COLLECT;
                *change_state = state;
            }
        } else if (prev_pos < prev_mask) {
            *slot_state = (*slot_state & 0xf) | CHANGE_COLLECT;
            state = *change_state;
        }
    }
    *out = state;
    return prev_group_done;
}
