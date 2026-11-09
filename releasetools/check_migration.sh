#!/sbin/sh
#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Check if the target is a clean install or migrating from build >= 20261107 (2ec612d)

SYSDEV="/dev/block/bootdevice/by-name/system"
[ -e "$SYSDEV" ] || SYSDEV="/dev/block/platform/soc/1da4000.ufshc/by-name/system"
[ -e "$SYSDEV" ] || SYSDEV="/dev/block/by-name/system"

if [ ! -b "$SYSDEV" ]; then
    exit 0
fi

# Check if data partition exists and has packages.xml
# If packages.xml does not exist, it's a clean flash (user wiped /data or first time install)
DATAPKG="/data/system/packages.xml"
CLEAN_FLASH=0

if [ ! -f "$DATAPKG" ]; then
    DATADEV="/dev/block/bootdevice/by-name/userdata"
    [ -e "$DATADEV" ] || DATADEV="/dev/block/platform/soc/1da4000.ufshc/by-name/userdata"
    [ -e "$DATADEV" ] || DATADEV="/dev/block/by-name/userdata"
    if [ -b "$DATADEV" ] && ! grep -q " /data " /proc/mounts; then
        mkdir -p /tmp/check_data
        if mount -o ro "$DATADEV" /tmp/check_data 2>/dev/null; then
            if [ ! -f "/tmp/check_data/system/packages.xml" ]; then
                CLEAN_FLASH=1
            fi
            umount /tmp/check_data 2>/dev/null
        else
            CLEAN_FLASH=1
        fi
        rm -rf /tmp/check_data
    else
        CLEAN_FLASH=1
    fi
fi

if [ "$CLEAN_FLASH" -eq 1 ]; then
    exit 0
fi

# Existing /data is present. Check the currently installed /system image.
MNT="/tmp/check_sys"
mkdir -p "$MNT"
if mount -o ro "$SYSDEV" "$MNT" 2>/dev/null; then
    PROP="$MNT/build.prop"
    [ -f "$MNT/system/build.prop" ] && PROP="$MNT/system/build.prop"
    
    if [ -f "$PROP" ]; then
        # ro.build.date.utc for 22.2-20261107 is 1794067086
        DATE=$(grep -E '^ro.build.date.utc=' "$PROP" | head -n 1 | cut -d= -f2 | tr -d '\r ')
        VER=$(grep -E '^ro.lineage.version=' "$PROP" | head -n 1 | cut -d= -f2 | tr -d '\r ')
        umount "$MNT"
        rm -rf "$MNT"
        
        # Check if the installed build is 20261107 (1794067086) or newer
        if [ -n "$DATE" ] && [ "$DATE" -ge 1794067086 ] 2>/dev/null; then
            exit 0
        fi
        
        # Build is older: abort dirty flash
        echo "========================================================" >&2
        echo " Incompatible Dirty Flash Detected!" >&2
        echo " Current build: $VER (date: $DATE)" >&2
        echo " Upgrading directly from pre-migration test-key builds" >&2
        echo " is not supported without migrating package signatures." >&2
        echo " Please either:" >&2
        echo "   1) Update to lineage-22.2-20261107 (migration build) first, OR" >&2
        echo "   2) Perform a clean flash (Wipe Data in recovery)." >&2
        echo "========================================================" >&2
        exit 1
    else
        umount "$MNT"
        rm -rf "$MNT"
        exit 0
    fi
fi

rm -rf "$MNT"
exit 0
