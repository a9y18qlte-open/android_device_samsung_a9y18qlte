/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.samsungims;

import android.app.Application;
import android.content.BroadcastReceiver;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.res.XmlResourceParser;
import android.content.IntentFilter;
import android.database.ContentObserver;
import android.database.Cursor;
import android.net.Uri;
import android.os.FileObserver;
import android.os.Handler;
import android.os.PersistableBundle;
import android.os.SystemProperties;
import android.os.UserHandle;
import android.provider.Settings;
import android.provider.Telephony;
import android.telephony.CarrierConfigManager;
import android.telephony.SubscriptionInfo;
import android.telephony.SubscriptionManager;
import android.telephony.TelephonyManager;
import android.text.TextUtils;
import android.util.Log;
import android.util.Xml;

import com.android.ims.ImsManager;

import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.List;

/**
 * Does for Samsung's IMS service (com.sec.imsservice) what Samsung's telephony
 * framework does on stock, which it needs to register for VoLTE:
 *
 * - Tell it the ISIM is loaded. It waits for an ISIM_LOADED notification that
 *   AOSP never sends, and registers with a wrong identity without it.
 * - Keep its per-slot VoLTE switch (Settings.System voicecall_type, set by the
 *   stock Settings app) in line with AOSP's Enhanced 4G LTE setting.
 * - Make sure the carrier has an IMS APN; AOSP's APN list often lacks one.
 * - Make sure the carrier has an XCAP APN Android 10 can bring up, for call
 *   forwarding, call waiting and call barring over Ut.
 * - Tell Android VoLTE is available on a SIM exactly when the IMS service has
 *   its VoLTE switch on for it. The IMS service registers whenever it has a
 *   profile for the carrier, but only places calls when the switch is on;
 *   if Android routes a call to it anyway, the call hangs at "calling".
 */
public class ImsHelperApp extends Application {
    private static final String TAG = "SamsungImsHelper";

    private static final String ACTION_SIM_STATE_CHANGED = "android.intent.action.SIM_STATE_CHANGED";
    // Sent by Samsung's IMS service once it has processed a loaded SIM, also
    // after it restarts. It carries no slot.
    private static final String ACTION_IMS_ON_SIMLOADED = "com.samsung.ims.action.onsimloaded";
    private static final String ACTION_ISIM_LOADED = "android.intent.action.ISIM_LOADED";

    private static final String[] VOICECALL_TYPE = {"voicecall_type", "voicecall_type2"};
    private static final String[] VOICECALL_TYPE_USER_ACTION =
            {"voicecall_type_user_action", "voicecall_type_user_action2"};
    private static final int VOICECALL_TYPE_VOLTE = 0;
    private static final int VOICECALL_TYPE_CS_ONLY = 1;

    // RILConstants.DATA_PROFILE_IMS
    private static final int DATA_PROFILE_IMS = 2;

    // The IMS service asks for its XCAP PDN with NET_CAPABILITY_CBS, as
    // Android 10 has no APN type for NET_CAPABILITY_XCAP.
    private static final String APN_TYPE_XCAP = "xcap";
    private static final String APN_TYPE_CBS = "cbs";
    // XCAP APN for carriers res/xml/xcap_apns.xml does not list.
    private static final String XCAP_APN_DEFAULT = "xcap";

    private static final String PROP_OMC_PATH = "persist.sys.omc_path";

    // The IMS service's per-slot service switches (imsswitch_<slot>.xml).
    private static final File IMS_PREFS_DIR =
            new File("/data/user_de/0/com.sec.imsservice/shared_prefs");
    private static final String IMS_SWITCH_PREFIX = "imsswitch_";
    private static final String IMS_SWITCH_MMTEL = "mmtel";

    private Handler mHandler;
    private FileObserver mImsSwitchObserver;

    @Override
    public void onCreate() {
        super.onCreate();
        // Before anything else: the IMS service reads the carrier settings as
        // soon as it sees a loaded SIM.
        clearOmcPath();
        mHandler = new Handler();

        IntentFilter filter = new IntentFilter();
        filter.addAction(ACTION_SIM_STATE_CHANGED);
        filter.addAction(ACTION_IMS_ON_SIMLOADED);
        registerReceiver(mReceiver, filter);

        ContentObserver observer = new ContentObserver(mHandler) {
            @Override
            public void onChange(boolean selfChange) {
                updateVoiceCallTypes();
                updateVolteAvailability();
            }
        };
        ContentResolver cr = getContentResolver();
        for (String key : VOICECALL_TYPE) {
            cr.registerContentObserver(Settings.System.getUriFor(key), false, observer);
        }
        cr.registerContentObserver(SubscriptionManager.CONTENT_URI, true, observer);

        updateVoiceCallTypes();
        watchImsSwitches();
        updateVolteAvailability();
    }

    private final BroadcastReceiver mReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (ACTION_SIM_STATE_CHANGED.equals(intent.getAction())
                    && !"LOADED".equals(intent.getStringExtra("ss"))) {
                return;
            }
            updateVoiceCallTypes();
            for (SubscriptionInfo sub : activeSubscriptions()) {
                String numeric = simOperator(sub);
                if (numeric != null) {
                    ensureImsApn(numeric);
                    ensureXcapApn(numeric);
                }
                sendIsimLoaded(sub);
            }
            watchImsSwitches();
            updateVolteAvailability();
        }
    };

    /**
     * The IMS service rewrites a slot's switches after it has processed a SIM
     * with a new IMSI, sometimes after its own "SIM loaded" broadcast, so
     * watch the files themselves.
     */
    private void watchImsSwitches() {
        if (mImsSwitchObserver != null || !IMS_PREFS_DIR.isDirectory()) return;
        mImsSwitchObserver = new FileObserver(IMS_PREFS_DIR.getPath(),
                FileObserver.CLOSE_WRITE | FileObserver.MOVED_TO) {
            @Override
            public void onEvent(int event, String path) {
                if (path != null && path.startsWith(IMS_SWITCH_PREFIX)
                        && path.endsWith(".xml")) {
                    mHandler.post(ImsHelperApp.this::updateVolteAvailability);
                }
            }
        };
        mImsSwitchObserver.startWatching();
    }

    private void updateVolteAvailability() {
        CarrierConfigManager ccm = getSystemService(CarrierConfigManager.class);
        for (SubscriptionInfo sub : activeSubscriptions()) {
            int slot = sub.getSimSlotIndex();
            if (slot < 0) continue;
            boolean available = readImsSwitch(slot, IMS_SWITCH_MMTEL);

            PersistableBundle config = ccm.getConfigForSubId(sub.getSubscriptionId());
            if (config != null && config.getBoolean(
                    CarrierConfigManager.KEY_CARRIER_VOLTE_AVAILABLE_BOOL) == available) {
                continue;
            }
            // Held per slot by the phone process until it restarts, so set it
            // for every SIM that loads, not only where VoLTE works.
            Log.i(TAG, "slot " + slot + ": VoLTE " + (available ? "available" : "unavailable"));
            PersistableBundle override = new PersistableBundle();
            override.putBoolean(CarrierConfigManager.KEY_CARRIER_VOLTE_AVAILABLE_BOOL, available);
            ccm.overrideConfig(sub.getSubscriptionId(), override);
        }
    }

    private static boolean readImsSwitch(int slot, String name) {
        File file = new File(IMS_PREFS_DIR, IMS_SWITCH_PREFIX + slot + ".xml");
        try (FileInputStream in = new FileInputStream(file)) {
            XmlPullParser parser = Xml.newPullParser();
            parser.setInput(in, StandardCharsets.UTF_8.name());
            for (int type = parser.next(); type != XmlPullParser.END_DOCUMENT;
                    type = parser.next()) {
                if (type == XmlPullParser.START_TAG && "boolean".equals(parser.getName())
                        && name.equals(parser.getAttributeValue(null, "name"))) {
                    return Boolean.parseBoolean(parser.getAttributeValue(null, "value"));
                }
            }
        } catch (IOException | XmlPullParserException e) {
            // Not written yet: the IMS service has not processed this slot.
        }
        return false;
    }

    /**
     * The IMS service reads its carrier switches from <persist.sys.omc_path>/
     * customer.xml, else from /system/csc/customer.xml, which this build
     * generates from Samsung's own switch table. Builds before that pointed
     * the property at the phone's stock CSC on /odm; clear it.
     */
    private void clearOmcPath() {
        if (!SystemProperties.get(PROP_OMC_PATH).isEmpty()) {
            Log.i(TAG, "clearing " + PROP_OMC_PATH);
            SystemProperties.set(PROP_OMC_PATH, "");
        }
    }

    private List<SubscriptionInfo> activeSubscriptions() {
        List<SubscriptionInfo> subs =
                getSystemService(SubscriptionManager.class).getActiveSubscriptionInfoList();
        return subs != null ? subs : Collections.emptyList();
    }

    private void updateVoiceCallTypes() {
        ContentResolver cr = getContentResolver();
        for (SubscriptionInfo sub : activeSubscriptions()) {
            int slot = sub.getSimSlotIndex();
            if (slot < 0 || slot >= VOICECALL_TYPE.length) continue;

            boolean volte = ImsManager.getInstance(this, slot)
                    .isEnhanced4gLteModeSettingEnabledByUser();
            int wanted = volte ? VOICECALL_TYPE_VOLTE : VOICECALL_TYPE_CS_ONLY;
            if (Settings.System.getInt(cr, VOICECALL_TYPE[slot], -1) != wanted) {
                Log.i(TAG, "slot " + slot + ": " + VOICECALL_TYPE[slot] + " = " + wanted);
                Settings.System.putInt(cr, VOICECALL_TYPE_USER_ACTION[slot], 1);
                Settings.System.putInt(cr, VOICECALL_TYPE[slot], wanted);
            }
        }
    }

    /**
     * The modem only asks the network for P-CSCF addresses on a PDN set up
     * with the IMS data profile, so the IMS APN needs profile_id
     * DATA_PROFILE_IMS and modem_cognitive set; AOSP's APN list often has no
     * IMS APN at all, or one without them.
     */
    private void ensureImsApn(String numeric) {
        ContentResolver cr = getContentResolver();
        boolean found = false;
        try (Cursor c = cr.query(Telephony.Carriers.CONTENT_URI,
                new String[] {Telephony.Carriers._ID, Telephony.Carriers.TYPE,
                        Telephony.Carriers.PROFILE_ID, Telephony.Carriers.MODEM_PERSIST},
                Telephony.Carriers.NUMERIC + "=?", new String[] {numeric}, null)) {
            while (c != null && c.moveToNext()) {
                String type = c.getString(1);
                if (type == null || !type.contains("ims")) continue;
                found = true;
                if (c.getInt(2) == DATA_PROFILE_IMS && c.getInt(3) == 1) continue;
                ContentValues values = new ContentValues();
                values.put(Telephony.Carriers.PROFILE_ID, DATA_PROFILE_IMS);
                values.put(Telephony.Carriers.MODEM_PERSIST, 1);
                cr.update(Uri.withAppendedPath(Telephony.Carriers.CONTENT_URI,
                        Long.toString(c.getLong(0))), values, null, null);
                Log.i(TAG, "set IMS data profile on APN " + c.getLong(0) + " for " + numeric);
            }
        }
        if (found) return;

        ContentValues values = new ContentValues();
        values.put(Telephony.Carriers.NAME, "IMS");
        values.put(Telephony.Carriers.NUMERIC, numeric);
        values.put(Telephony.Carriers.MCC, numeric.substring(0, 3));
        values.put(Telephony.Carriers.MNC, numeric.substring(3));
        values.put(Telephony.Carriers.APN, "ims");
        values.put(Telephony.Carriers.TYPE, "ims");
        values.put(Telephony.Carriers.PROTOCOL, "IPV4V6");
        values.put(Telephony.Carriers.ROAMING_PROTOCOL, "IPV4V6");
        values.put(Telephony.Carriers.PROFILE_ID, DATA_PROFILE_IMS);
        values.put(Telephony.Carriers.MODEM_PERSIST, 1);
        cr.insert(Telephony.Carriers.CONTENT_URI, values);
        Log.i(TAG, "added IMS APN for " + numeric);
    }

    /**
     * The IMS service sets up a PDN on the carrier's XCAP APN for call
     * forwarding, call waiting and call barring over Ut, and AOSP's APN list
     * has no XCAP APNs. Take the carrier's XCAP APN from res/xml/xcap_apns.xml
     * (generated from Samsung's CSCs), else use "xcap". Many carriers run XCAP
     * on their internet APN; if the carrier already has an APN of that name,
     * give it the XCAP types rather than adding a second one. The IMS service
     * requests type cbs instead of xcap, so XCAP APNs need both. Users can
     * edit the APN in the APN settings if theirs differs.
     */
    private void ensureXcapApn(String numeric) {
        String apn = findXcapApn(numeric);
        ContentResolver cr = getContentResolver();
        long sameApn = -1;
        String sameApnType = null;
        try (Cursor c = cr.query(Telephony.Carriers.CONTENT_URI,
                new String[] {Telephony.Carriers._ID, Telephony.Carriers.TYPE,
                        Telephony.Carriers.APN},
                Telephony.Carriers.NUMERIC + "=?", new String[] {numeric}, null)) {
            while (c != null && c.moveToNext()) {
                String type = c.getString(1);
                if (hasApnType(type, APN_TYPE_XCAP)) {
                    if (!hasApnType(type, APN_TYPE_CBS)) {
                        addApnTypes(c.getLong(0), type, APN_TYPE_CBS);
                    }
                    return;
                }
                if (sameApn < 0 && apn.equalsIgnoreCase(c.getString(2))) {
                    sameApn = c.getLong(0);
                    sameApnType = type;
                }
            }
        }
        if (sameApn >= 0) {
            addApnTypes(sameApn, sameApnType, APN_TYPE_XCAP + "," + APN_TYPE_CBS);
            return;
        }

        ContentValues values = new ContentValues();
        values.put(Telephony.Carriers.NAME, "XCAP");
        values.put(Telephony.Carriers.NUMERIC, numeric);
        values.put(Telephony.Carriers.MCC, numeric.substring(0, 3));
        values.put(Telephony.Carriers.MNC, numeric.substring(3));
        values.put(Telephony.Carriers.APN, apn);
        values.put(Telephony.Carriers.TYPE, APN_TYPE_XCAP + "," + APN_TYPE_CBS);
        values.put(Telephony.Carriers.PROTOCOL, "IPV4V6");
        values.put(Telephony.Carriers.ROAMING_PROTOCOL, "IPV4V6");
        cr.insert(Telephony.Carriers.CONTENT_URI, values);
        Log.i(TAG, "added XCAP APN " + apn + " for " + numeric);
    }

    private void addApnTypes(long id, String type, String add) {
        ContentValues values = new ContentValues();
        values.put(Telephony.Carriers.TYPE, TextUtils.isEmpty(type) ? add : type + "," + add);
        getContentResolver().update(Uri.withAppendedPath(Telephony.Carriers.CONTENT_URI,
                Long.toString(id)), values, null, null);
        Log.i(TAG, "added type " + add + " to APN " + id);
    }

    /** The carrier's XCAP APN from Samsung's CSCs, else "xcap". */
    private String findXcapApn(String numeric) {
        try (XmlResourceParser parser = getResources().getXml(R.xml.xcap_apns)) {
            for (int type = parser.next(); type != XmlPullParser.END_DOCUMENT;
                    type = parser.next()) {
                if (type == XmlPullParser.START_TAG && "apn".equals(parser.getName())
                        && numeric.equals(parser.getAttributeValue(null, "numeric"))) {
                    return parser.getAttributeValue(null, "apn");
                }
            }
        } catch (IOException | XmlPullParserException e) {
            Log.e(TAG, "cannot read XCAP APNs", e);
        }
        return XCAP_APN_DEFAULT;
    }

    private static boolean hasApnType(String types, String type) {
        if (types == null) return false;
        for (String t : types.split(",")) {
            if (t.trim().equalsIgnoreCase(type)) return true;
        }
        return false;
    }

    private String simOperator(SubscriptionInfo sub) {
        String numeric = getSystemService(TelephonyManager.class)
                .createForSubscriptionId(sub.getSubscriptionId()).getSimOperator();
        return TextUtils.isEmpty(numeric) || numeric.length() < 5 ? null : numeric;
    }

    private void sendIsimLoaded(SubscriptionInfo sub) {
        Intent intent = new Intent(ACTION_ISIM_LOADED);
        intent.putExtra("phone", sub.getSimSlotIndex());
        intent.putExtra("slot", sub.getSimSlotIndex());
        intent.putExtra("subscription", sub.getSubscriptionId());
        intent.addFlags(Intent.FLAG_RECEIVER_INCLUDE_BACKGROUND);
        sendBroadcastAsUser(intent, UserHandle.ALL);
        Log.i(TAG, "ISIM_LOADED for slot " + sub.getSimSlotIndex());
    }
}
