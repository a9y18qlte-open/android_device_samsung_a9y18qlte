/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/*
 * The panel backlight is the MDSS lcd-backlight LED (0-365); this device has
 * no touchkeys and no Samsung sec LED, so neither button nor notification
 * light is exposed.
 */
#define PANEL_BRIGHTNESS_NODE "/sys/class/leds/lcd-backlight/brightness"
#define PANEL_MAX_BRIGHTNESS_NODE "/sys/class/leds/lcd-backlight/max_brightness"

#define LED_ADJUSTMENT_R 1.0
#define LED_ADJUSTMENT_G 1.0
#define LED_ADJUSTMENT_B 1.0

#define LED_BRIGHTNESS_BATTERY 255
#define LED_BRIGHTNESS_NOTIFICATION 255
#define LED_BRIGHTNESS_ATTENTION 255
