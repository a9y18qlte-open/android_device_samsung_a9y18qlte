#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

import common
import os

PARTITIONS = ("system", "vendor")


def FullOTA_InstallBegin(info):
    try:
        script_data = info.input_zip.read("SYSTEM/etc/check_migration.sh")
    except KeyError:
        script_path = os.path.join(os.path.dirname(__file__), "check_migration.sh")
        with open(script_path, "rb") as f:
            script_data = f.read()

    common.ZipWriteStr(info.output_zip, "install/bin/check_migration.sh", script_data)

    info.script.Print("Verifying build compatibility...")
    info.script.AppendExtra(
        'package_extract_file("install/bin/check_migration.sh", "/tmp/check_migration.sh");\n'
        'set_metadata("/tmp/check_migration.sh", "uid", 0, "gid", 0, "mode", 0755);\n'
        'run_program("/tmp/check_migration.sh") == 0 || '
        'abort("E3005: Upgrading directly from pre-migration test-key builds is not supported. Please update to lineage-22.2-20261107 (2ec612d) first or wipe data.");\n'
    )


def FullOTA_InstallEnd(info):
    for partition in PARTITIONS:
        device = "/dev/block/bootdevice/by-name/" + partition
        info.script.AppendExtra('run_program("/system/bin/e2fsck", "-fy", "%s");' % device)
        info.script.AppendExtra('run_program("/system/bin/resize2fs", "%s");' % device)
