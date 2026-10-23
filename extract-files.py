#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: 2024 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

import re
from os import path

from extract_utils.file import File
from extract_utils.fixups_blob import (
    BlobFixupCtx,
    blob_fixup,
    blob_fixups_user_type,
)
from extract_utils.fixups_lib import (
    lib_fixups,
    lib_fixups_user_type,
)
from extract_utils.main import (
    ExtractUtils,
    ExtractUtilsModule,
)

namespace_imports = [
    'hardware/samsung',
    'device/samsung/a9y18qlte/camera',
    'device/samsung/a9y18qlte',
    'hardware/qcom-caf/common/libqti-perfd-client',
    'hardware/qcom-caf/msm8998',
    'hardware/qcom-caf/wlan',
]


def lib_fixup_vendor_suffix(lib: str, partition: str, *args, **kwargs):
    return f'{lib}_vendor' if partition == 'vendor' else None


lib_fixups: lib_fixups_user_type = {
    **lib_fixups,
    (
        'com.qualcomm.qti.ant@1.0',
        'libfloatingfeature',
        'libmdf',
        'librmnetctl',
        'libsavscmn',
        'libsecaudiocoreutils',
        'libsecnativefeature',
        'libsecure_storage',
        'vendor.qti.hardware.fm@1.0',
        'vendor.samsung.hardware.radio.bridge@2.0',
        'vendor.samsung.hardware.security.securestorage@3.0',
    ): lib_fixup_vendor_suffix,
}


def patch_bytes(
    ctx: BlobFixupCtx,
    file: File,
    file_path: str,
    patches: list[tuple[int, str, int, str]],
    *args,
    **kwargs,
):
    # Each patch is (check offset, expected hex, write offset, new hex). A patch
    # is only applied while the original bytes are still in place.
    with open(file_path, 'rb+') as f:
        for check_offset, expected, write_offset, new in patches:
            expected_bytes = bytes.fromhex(expected)
            f.seek(check_offset)
            if f.read(len(expected_bytes)) != expected_bytes:
                continue
            f.seek(write_offset)
            f.write(bytes.fromhex(new))


def delete_rc_block(
    ctx: BlobFixupCtx,
    file: File,
    file_path: str,
    *args,
    **kwargs,
):
    # Same as sed '/on property:vts.native_server.on=\*/,/setprop/d'
    with open(file_path, 'r', newline='', encoding='utf-8') as f:
        lines = f.readlines()
    out, deleting = [], False
    for line in lines:
        if not deleting and 'on property:vts.native_server.on=*' in line:
            deleting = True
        if deleting:
            if 'setprop' in line and 'on property:vts.native_server.on=*' not in line:
                deleting = False
            continue
        out.append(line)
    with open(file_path, 'w', newline='', encoding='utf-8') as f:
        f.writelines(out)


def fix_depth_mount_angle(
    ctx: BlobFixupCtx,
    file: File,
    file_path: str,
    *args,
    **kwargs,
):
    with open(file_path, 'r', newline='', encoding='utf-8') as f:
        data = f.read()
    data = re.sub(
        r'(<SensorName>s5k5e9yx</SensorName>.*?</CameraModuleConfig>)',
        lambda m: m.group(1).replace(
            '<MountAngle>270</MountAngle>', '<MountAngle>90</MountAngle>'
        ),
        data,
        flags=re.DOTALL,
    )
    with open(file_path, 'w', newline='', encoding='utf-8') as f:
        f.write(data)


def scale_exposure_table(
    ctx: BlobFixupCtx,
    file: File,
    file_path: str,
    src_soname: str,
    *args,
    **kwargs,
):
    # A copy of an imx576 chromatix 3A library for sensor mode 1, whose line
    # is 4400 pixels instead of the 6320 the tuning was made for: rename its
    # SONAME to the copy's file name, and scale the line counts of the AEC
    # exposure table (valid count at 0xcc04, then (gain Q8, lines) pairs from
    # 0xcc10, starting at gain 1.0x) by 6320/4400, so every exposure index
    # keeps its exposure time.
    with open(file_path, 'rb') as f:
        data = bytearray(f.read())
    old = src_soname.encode() + b'\0'
    new = path.basename(file_path).encode()
    if data.count(old) != 1 or len(new) >= len(old):
        return
    offset = data.find(old)
    data[offset:offset + len(old)] = new.ljust(len(old), b'\0')
    table = 0xcc10
    if int.from_bytes(data[table:table + 4], 'little') != 256:
        return
    for entry in range(table, table + 8 * 600, 8):
        gain = int.from_bytes(data[entry:entry + 4], 'little')
        lines = int.from_bytes(data[entry + 4:entry + 8], 'little')
        if not (256 <= gain < 200000 and 0 < lines < 200000):
            break
        lines = min(65535, round(lines * 6320 / 4400))
        data[entry + 4:entry + 8] = lines.to_bytes(4, 'little')
    with open(file_path, 'wb') as f:
        f.write(data)


# 60 fps: make the binned 16:9 mode (res 1, 2832x1592) of the main and
# front IMX576 a 60 fps mode with the clocking of the high speed modes:
# VT clock divider 0x0303 4 -> 2, line length 6320 -> 4400, frame length
# 2177 -> 3128, 0x3f80/1 as res 5/6; mode table entry: line/frame length,
# op pixel clock 480 -> 540 MHz (one VFE; more splits the sensor over both
# and per-request preview outputs fail), max fps 30 -> 60.
IMX576_60FPS_PATCHES = [
    (0x600ac, '42031800', 0x600ae, '1100'),
    (0x600b4, '4303b000', 0x600b6, '3000'),
    (0x600bc, '40030800', 0x600be, '0c00'),
    (0x600c4, '41038100', 0x600c6, '3800'),
    (0x601c4, '03030400', 0x601c6, '0200'),
    (0x6022c, '803f0200', 0x6022e, '0300'),
    (0x60234, '813f3000', 0x60236, 'e800'),
    (0xac488, '100b3806b01881080000000000389c1c'
              '02000100000000000000000000001e40'
              '0000000000003e40',
     0xac488, '100b38063011380c0000000000bf2f20'
              '02000100000000000000000000001e40'
              '0000000000004e40'),
]  # fmt: skip


blob_fixups: blob_fixups_user_type = {
    # libhidltransport and libhwbinder were merged into libhidlbase in
    # Android 10; their stubs are not available to the product partition.
    (
        'product/lib/com.qualcomm.qti.imscmservice@1.0.so',
        'product/lib/com.qualcomm.qti.imscmservice@2.0.so',
        'product/lib/com.qualcomm.qti.imscmservice@2.1.so',
        'product/lib/com.qualcomm.qti.imscmservice@2.2.so',
        'product/lib64/com.qualcomm.qti.imscmservice@1.0.so',
        'product/lib64/com.qualcomm.qti.imscmservice@2.0.so',
        'product/lib64/com.qualcomm.qti.imscmservice@2.1.so',
        'product/lib64/com.qualcomm.qti.imscmservice@2.2.so',
    ): blob_fixup()
        .remove_needed('libhidltransport.so')
        .remove_needed('libhwbinder.so'),
    'vendor/lib64/libgps.utils.so': blob_fixup()
        .add_needed('libprocessgroup_shim.so'),
    # Uses no C++ runtime symbols; libstdc++ is not available to vendor
    'vendor/lib/libdualcam_refocus.so': blob_fixup()
        .remove_needed('libstdc++.so'),
    # Remove triggers setting vendor.vts.native_server.on
    'vendor/etc/init/init.vendor.rilcommon.rc': blob_fixup()
        .call(delete_rc_block),
    # radio::sec::signalLevelInfosChanged() logs an error on every signal level
    # update while no Samsung radio indication client is registered, which is
    # always the case on AOSP. Drop the log call:
    # bl __android_log_buf_print (8e 33 00 94) -> nop.
    'vendor/lib64/libril.so': blob_fixup()
        .call(patch_bytes, [(0x6b380, 'e1071f32e403082a8e330094', 0x6b388, '1f2003d5')]),
    # vendor_qti_diag is a Samsung/QTI config.fs AID that this build does not
    # define; init rejects the whole service over an unknown group.
    'vendor/etc/init/android.hardware.gnss@2.0-service-qti.rc': blob_fixup()
        .regex_replace(' vendor_qti_diag', ''),
    # utf8_length() was removed from libutils in Android 11.
    # CallSession::StartAllMediaForUAS() leaves the audio of an answered VoWiFi
    # call (AP voice engine, SAE) to Samsung's phone app, which starts it
    # through changeAudioPath(); AOSP never calls that, so incoming VoWiFi calls
    # had no audio. Start it on answer like outgoing calls:
    # b.ne (41 25 00 54) -> nop.
    'system/lib64/libsec-ims.so': blob_fixup()
        .add_needed('libsecims_shim.so')
        .call(patch_bytes, [(0x3efccc, '682a50b91f05007141250054', 0x3efcd4, '1f2003d5')]),
    # VoWiFi audio engine (sveservice). Links libsurfaceflinger, which no longer
    # exists, for nothing it uses.
    'system/lib64/libsvejni.so': blob_fixup()
        .remove_needed('libsurfaceflinger.so'),
    # Android 10 AudioRecord/AudioTrack users. libsve_shim provides the old
    # AudioRecord constructor and set(), which Android 12 changed.
    # It allocates the objects itself, with the Android 10 sizes:
    # AudioRecord 696 -> 1128 bytes, AudioTrack 1032 -> 1264 bytes (Android 13).
    # Samsung's stream type 15 does not exist on AOSP and AudioTrack rejects it;
    # play the call on the voice call stream (0) instead:
    # orr w1, wzr, #0xf -> mov w1, #0.
    # Android 13 moved the RefBase of AudioTrack and AudioRecord off the start
    # of the object; libsve_shim finds it before counting references. Point the
    # RefBase::incStrong()/decStrong() imports at it by renaming them in place
    # (RefBase -> SveBase keeps the string table layout).
    # The same change moved fields it reads inline: AudioTrack::mStatus
    # (initCheck()) 0x208 -> 0x200, AudioTrack::mSessionId (getSessionId())
    # 0x34c -> 0x35c, AudioRecord::mStatus 0x88 -> 0x8c.
    'system/lib64/libAudioFWInterface.so': blob_fixup()
        .call(patch_bytes, [
            (0x20b0, '00578052', 0x20b0, '008d8052'),
            (0x22e8, '00818052', 0x22e8, '009e8052'),
            (0x2818, 'e10f0032', 0x2818, '01008052'),
        ])
        .add_needed('libsve_shim.so')
        .binary_regex_replace(
            rb'_ZNK7android7RefBase9(inc|dec)StrongEPKv',
            rb'_ZNK7android7SveBase9\1StrongEPKv',
        )
        .call(patch_bytes, [
            (0x240c, '040942b9', 0x240c, '040142b9'),
            (0x2950, '080842b9', 0x2950, '080042b9'),
            (0x2514, '144c43b9', 0x2514, '145c43b9'),
            (0x2528, '88698052', 0x2528, '886b8052'),
            (0x21d4, '048940b9', 0x21d4, '048d40b9'),
        ]),
    # Camera::connect() and Surface::isValid() changed in Android 11/12.
    'system/lib64/libsamsung_videoengine_9_0.so': blob_fixup()
        .add_needed('libsve_shim.so'),
    # Android 14's RefBase aborts on its stack-allocated objects; use the
    # Android 13 libutils.
    'vendor/bin/pm-service': blob_fixup()
        .add_needed('libutils-v33.so'),
    # DT_NEEDED contains unused libbinder.so, which is absent from the vendor
    # namespace.
    'vendor/bin/hw/android.hardware.drm@1.2-service.widevine': blob_fixup()
        .remove_needed('libbinder.so'),
    # Parcel::print(TextOutput&), removed in Android 14; libsve_shim provides it.
    'system/lib64/libsveservice.so': blob_fixup()
        .add_needed('libsve_shim.so'),
    # Android 13's BoringSSL renamed the lh_* hash table functions to
    # OPENSSL_lh_*; liberis_shim provides the old names.
    'system/lib64/liberis_strongswan.so': blob_fixup()
        .add_needed('liberis_shim.so'),
    # Q blob importing strdup8to16(), removed from libcutils in Android 11.
    # libcutils_shim provides it.
    'system/bin/multiclientd': blob_fixup()
        .add_needed('libcutils_shim.so'),
    # strdup8to16() was removed from libcutils in Android 11; libcutils_shim
    # provides it.
    # The RIL publishes the SIM slot of the ongoing call as ril.dds.call.slotid,
    # a radio_prop that vendor processes may not read. Rename it (same length)
    # so the audio HIDL impl can use it to select the SIM 2 voice session.
    (
        'vendor/lib/libsec-ril.so',
        'vendor/lib/libsec-ril-dsds.so',
        'vendor/lib64/libsec-ril.so',
        'vendor/lib64/libsec-ril-dsds.so',
    ): blob_fixup()
        .add_needed('libcutils_shim.so')
        .binary_regex_replace(b'ril.dds.call.slotid', b'vendor.calls.slotid'),
    # At 240 fps the backend picks imx576's res 5 tuning only for Samsung's
    # SLOW_MOTION special mode, which only Samsung's camera app sets. Without
    # it the preview 3A runs and lengthens exposure, dropping the frame rate.
    # Add a default res 5 entry using the slow-motion 3A.
    # Res 1 (60 fps) has a shorter line than its 3A tuning was made for, so
    # the exposure tables stopped at 11.6 ms instead of 1/60 s. Point it at
    # copies with scaled tables (scale_exposure_table).
    'vendor/etc/camera/imx576_e24qs_chromatix.xml': blob_fixup()
        .patch_file('camera/tools/chromatix/imx576_e24qs_chromatix'),
    # The front imx576 has no res 5 entry and no slow-motion 3A. Its sensor
    # module adds front dimension flags to every session's special mode mask,
    # so add a res 5 entry with them, using the main camera's slow-motion 3A.
    'vendor/etc/camera/imx576_d24qs_chromatix.xml': blob_fixup()
        .patch_file('camera/tools/chromatix/imx576_d24qs_chromatix'),
    'vendor/lib/libchromatix_imx576_e24qs_60_preview_3a.so': blob_fixup()
        .call(scale_exposure_table, 'libchromatix_imx576_e24qs_4k_preview_3a.so'),
    'vendor/lib/libchromatix_imx576_e24qs_60_video_3a.so': blob_fixup()
        .call(scale_exposure_table, 'libchromatix_imx576_e24qs_4k_video_3a.so'),
    'vendor/lib/libchromatix_imx576_d24qs_60_preview_3a.so': blob_fixup()
        .call(scale_exposure_table, 'libchromatix_imx576_d24qs_zsl_preview_3a.so'),
    'vendor/lib/libchromatix_imx576_d24qs_60_video_3a.so': blob_fixup()
        .call(scale_exposure_table, 'libchromatix_imx576_d24qs_default_video_3a.so'),
    # The depth sensor (s5k5e9yx) is mounted like the other rear modules, but is
    # configured with MountAngle 270 (Samsung only uses it for depth data).
    # Exposed as camera 4 it renders upside down.
    'vendor/etc/camera/camera_config.xml': blob_fixup()
        .call(fix_depth_mount_angle),
    # The sensor sub-module reports ISO as
    #   100 * analog_gain * digital_gain / iso100_gain
    # but Samsung's sensor path never fills digital_gain, so it holds garbage
    # and android.sensor.sensitivity comes out as INT32_MIN. Drop that multiply:
    # vmul.f32 s0, s0, s4 -> nop.w at 0x63ad8.
    # Also: the mode picker had no check enabled for the regular preview/video
    # use case (12), so the lowest mode (30 fps) always won. Enable its fps
    # check (table at 0x287f0, check x use case) so [60,60] gets the 60 fps
    # mode below.
    'vendor/lib/libmmcamera2_sensor_modules.so': blob_fixup()
        .call(patch_bytes, [
            (0x63ad4, '21ee000a20ee020a80ee030abdeec00a', 0x63ad8, 'aff30080'),
            (0x287f0, '01000000000000000000000001000000000000000000000001000000'
                      '000000000100000000000000000000000000000000000000',
             0x28820, '01000000'),
        ]),
    'vendor/lib/libmmcamera_imx576.so': blob_fixup()
        .call(patch_bytes, IMX576_60FPS_PATCHES),
    # Front high speed modes as on the main camera:
    # - res 5 (720p240) runs at lower VT/op PLL multipliers (0x143/0x13c)
    #   and frame length 1107, about 221.5 fps; the HAL times a batch's
    #   frames at 240, so the video stuttered. Use the main camera's
    #   0x15d/0x157 and 1104.
    # - res 6 is 1280x720 at 120 fps, a crop of the binned area the main
    #   camera's 1920x1080 res 6 reads. Take its crop (0x0408-0x040f),
    #   output size (0x034c-0x034f) and table entry size: 1080p120.
    'vendor/lib/libmmcamera_imx576_front.so': blob_fixup()
        .call(patch_bytes, IMX576_60FPS_PATCHES + [
            (0x77804, '41035300', 0x77806, '5000'),
            (0x7791c, '07034300', 0x7791e, '5d00'),
            (0x7793c, '0f033c00', 0x7793e, '5700'),
            (0x7d66c, '08040300', 0x7d66e, '0100'),
            (0x7d674, '09042000', 0x7d676, 'e000'),
            (0x7d684, '0b04be00', 0x7d686, '0a00'),
            (0x7d68c, '0c040500', 0x7d68e, '0700'),
            (0x7d694, '0d040000', 0x7d696, '8000'),
            (0x7d69c, '0e040200', 0x7d69e, '0400'),
            (0x7d6a4, '0f04d000', 0x7d6a6, '3800'),
            (0x7d6ac, '4c030500', 0x7d6ae, '0700'),
            (0x7d6b4, '4d030000', 0x7d6b6, '8000'),
            (0x7d6bc, '4e030200', 0x7d6be, '0400'),
            (0x7d6c4, '4f03d000', 0x7d6c6, '3800'),
            (0xac668, '0005d002', 0xac668, '80073804'),
        ]),
}  # fmt: skip

module = ExtractUtilsModule(
    'a9y18qlte',
    'samsung',
    blob_fixups=blob_fixups,
    lib_fixups=lib_fixups,
    namespace_imports=namespace_imports,
)

if __name__ == '__main__':
    utils = ExtractUtils.device(module)
    utils.run()
