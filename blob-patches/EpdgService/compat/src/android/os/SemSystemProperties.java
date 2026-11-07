/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package android.os;

import java.io.FileInputStream;
import java.io.IOException;
import java.util.zip.CRC32;

/** Samsung's SystemProperties wrapper, as used by EpdgService. */
public final class SemSystemProperties {
    // EpdgService and its MAPCON provider reload these into their databases when
    // Samsung's firmware version (ro.build.PDA) changes. AOSP has none, and the build
    // date is not updated by every build, so it is a checksum of the files themselves:
    // they are reloaded exactly when they change.
    private static final String[] DATA_FILES = {
            "/system/etc/epdg_apns_conf.xml",
            "/system/etc/mapconprovider.xml",
    };
    private static String sDataVersion;

    private SemSystemProperties() {}

    public static String get(String key, String def) {
        if ("ro.build.PDA".equals(key)) {
            return getDataVersion();
        }
        return SystemProperties.get(key, def);
    }

    private static synchronized String getDataVersion() {
        if (sDataVersion == null) {
            CRC32 crc = new CRC32();
            byte[] buf = new byte[65536];
            for (String path : DATA_FILES) {
                try (FileInputStream in = new FileInputStream(path)) {
                    int n;
                    while ((n = in.read(buf)) > 0) {
                        crc.update(buf, 0, n);
                    }
                } catch (IOException e) {
                    // Missing file: it simply does not count.
                }
            }
            sDataVersion = "data-" + Long.toHexString(crc.getValue());
        }
        return sDataVersion;
    }
}
