/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package android.os;

/** Samsung's hardware quality reporting; there is nothing to report to on AOSP. */
public final class SemHqmManager {
    public boolean sendHWParamToHQM(int type, String compId, String feature, String hitType,
            String compVer, String compManufacture, String devCustomDataSet, String basicCustomDataSet,
            String priCustomDataSet) {
        return false;
    }
}
