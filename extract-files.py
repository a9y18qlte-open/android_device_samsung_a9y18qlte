#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: 2024 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

import re

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
        'libipanat',
        'libmdf',
        'liboffloadhal',
        'librmnetctl',
        'libsavscmn',
        'libsecaudiocoreutils',
        'libsecnativefeature',
        'libsecure_storage',
        'android.hardware.camera.provider@2.4-legacy',
        'camera.device@1.0-impl',
        'camera.device@3.2-impl',
        'camera.device@3.3-impl',
        'camera.device@3.4-impl',
        'camera.device@3.5-impl',
        'android.hardware.camera.provider@2.5-legacy',
        'vendor.qti.hardware.fm@1.0',
        'vendor.samsung.hardware.radio.bridge@2.0',
        'vendor.samsung.hardware.radio@2.0',
        'vendor.samsung.hardware.security.securestorage@3.0',
    ): lib_fixup_vendor_suffix,
    'libcamera_metadata_shim': lambda lib, partition, *args, **kwargs: (
        'libcamera_metadata_shim.a9y18qlte' if partition == 'vendor' else None
    ),
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
    # Their ASN.1 templates use the Android 13 BoringSSL struct layout, which
    # Android 14 changed; use the Android 13 libcrypto.
    (
        'vendor/lib64/hw/gatekeeper.mdfpp.so',
        'vendor/lib64/libkeymaster2_mdfpp.so',
        'vendor/lib64/libkeymaster_helper_vendor.so',
    ): blob_fixup()
        .replace_needed('libcrypto.so', 'libcrypto-v33.so'),
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
    # mm_stream_streamon() iterates buf_status[] with an int8_t index. HAL3
    # gralloc streams register 128 buffer slots, so the index wraps to -128,
    # reads garbage before the array and waits for a buffer "mapping" that
    # never happens -> stream-on timeout, no HAL3 preview. Make the index
    # unsigned: sxtb r2, r0 (42 b2) -> uxtb r2, r0 (c2 b2).
    # mm_stream_calc_offset_snapshot() pads scanline with the backend's
    # height_padding (1). When height / 2 is odd (e.g. 5664x3186 16:9 snapshot,
    # height / 2 = 1593), Qualcomm CPP DMA writes Chroma in pairs of rows (1594
    # rows), crossing the page boundary and triggering SMMU fault / crash.
    # Always pad scanline to a multiple of 4:
    # scanline = (h + p - 1) & -p  ->  scanline = (h + 3) & ~3 at 0x1ed10.
    # mm_stream_calc_offset_snapshot() pads the snapshot stride with the
    # backend's width_padding (1), so e.g. the telephoto's 3672-wide snapshot
    # gets a stride that is not 16-byte aligned; the VFE write master rounds
    # each line up and overruns the buffer (SMMU page fault, "camera daemon
    # died"). Always pad the stride to 32:
    # stride = (w + p - 1) & -p  ->  stride = (w + 31) & ~31 at 0x1ed1a.
    'vendor/lib/libmmcamera_interface.so': blob_fixup()
        .call(patch_bytes, [
            (0x1bf7c, '013042b29142eddc', 0x1bf7e, 'c2'),
            (0x1ed10, '83185242013b03ea0205', 0x1ed10, '00f1030323f0030500bf'),
            (0x1ed1a, '0beb01024942013a02ea0103', 0x1ed1a, '0bf11f0222f01f0300bf00bf'),
        ]),
    # SehCameraProvider::getCameraIdList() drops every camera id > 19; only
    # Samsung's sehGetCameraIdList() returns them. That hides the telephoto
    # (50), ultra-wide (52) and depth (54) sensors from AOSP cameraserver. Hide
    # only id 20 (a second instance of the main sensor used for Samsung dual
    # modes) instead:
    # cmp r0, #19; bgt (13 28 16 dc) -> cmp r0, #20; beq (14 28 16 d0).
    'vendor/lib/hw/vendor.samsung.hardware.camera.provider@3.0-impl.so': blob_fixup()
        .call(patch_bytes, [(0x12ed8, '0ff082ed132816dc', 0x12edc, '142816d0')])
        .add_needed('libcamera_metadata_shim.so'),
    # The depth sensor (s5k5e9yx) is mounted like the other rear modules, but is
    # configured with MountAngle 270 (Samsung only uses it for depth data).
    # Exposed as camera 54 it renders upside down.
    'vendor/etc/camera/camera_config.xml': blob_fixup()
        .call(fix_depth_mount_angle),
    # The sensor sub-module reports ISO as
    #   100 * analog_gain * digital_gain / iso100_gain
    # but Samsung's sensor path never fills digital_gain, so it holds garbage
    # and android.sensor.sensitivity comes out as INT32_MIN. Drop that multiply:
    # vmul.f32 s0, s0, s4 -> nop.w at 0x63ad8.
    'vendor/lib/libmmcamera2_sensor_modules.so': blob_fixup()
        .call(patch_bytes, [(0x63ad4, '21ee000a20ee020a80ee030abdeec00a', 0x63ad8, 'aff30080')]),
    # Video recordings were green: the HAL writes UBWC video because it cannot
    # read vendor.video.disable.ubwc, but the stock encoder only takes linear
    # NV12. Keep video linear (NV12_VENUS) while preview stays UBWC - making
    # both linear enables CPP output duplication, which page-faults with this
    # kernel. In QCamera3Channel::getStreamDefaultFormat() replace the
    # isVideoUBWCEnabled() call with "movs r0, #0; nop" (0xb50d4).
    'vendor/lib/hw/camera.sdm660.so': blob_fixup()
        .call(patch_bytes, [(0xb50d4, '8cf06cee0028', 0xb50d4, '002000bf')]),
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
