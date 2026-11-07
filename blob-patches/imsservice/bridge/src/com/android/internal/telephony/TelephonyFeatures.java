/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.android.internal.telephony;

/** Samsung framework class ImsSmsImpl uses; not part of AOSP. */
public final class TelephonyFeatures {
    /** Release build: keep message contents and numbers out of the logs. */
    public static boolean SHIP_BUILD = true;

    private TelephonyFeatures() {}

    /** Not a Verizon device. */
    public static int getVzwDeviceType(int phoneId) {
        return 0;
    }
}
