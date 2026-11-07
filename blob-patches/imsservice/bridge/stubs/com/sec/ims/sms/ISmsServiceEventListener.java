/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.sec.ims.sms;

import android.os.IInterface;
import android.os.RemoteException;

/** Compile-time stand-in for the imsservice.apk interface (only the method the bridge calls). */
public interface ISmsServiceEventListener extends IInterface {
    void onReceiveSMSAck(int messageId, int reasonCode, String contentType, byte[] pdu,
            int retryAfter) throws RemoteException;
}
