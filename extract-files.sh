#
# Copyright (C) 2022 The LineageOS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

set -e

VENDOR=samsung
DEVICE=a9y18qlte

# Load extract_utils and do some sanity checks
MY_DIR="${BASH_SOURCE%/*}"
if [[ ! -d "${MY_DIR}" ]]; then MY_DIR="${PWD}"; fi

LINEAGE_ROOT="${MY_DIR}"/../../..

HELPER="${LINEAGE_ROOT}/tools/extract-utils/extract_utils.sh"
if [ ! -f "${HELPER}" ]; then
    echo "Unable to find helper script at ${HELPER}"
    exit 1
fi
source "${HELPER}"

SECTION=
KANG=

while [ "${#}" -gt 0 ]; do
    case "${1}" in
        -n | --no-cleanup )
                CLEAN_VENDOR=false
                ;;
        -k | --kang )
                KANG="--kang"
                ;;
        -s | --section )
                SECTION="${2}"; shift
                CLEAN_VENDOR=false
                ;;
        * )
                SRC="${1}"
                ;;
    esac
    shift
done

if [ -z "${SRC}" ]; then
    SRC="adb"
fi

function blob_fixup() {
    case "${1}" in
        vendor/etc/init/init.vendor.rilcommon.rc)
            # remove triggers setting vendor.vts.native_server.on
            sed -i '/on property:vts.native_server.on=\*/,/setprop/d' "${2}"
            ;;
        vendor/lib64/libril.so)
            # radio::sec::signalLevelInfosChanged() logs an error on every signal
            # level update while no Samsung radio indication client is
            # registered, which is always the case on AOSP. Drop the log call:
            # bl __android_log_buf_print (8e 33 00 94) -> nop.
            if [ "$(xxd -s 0x6b380 -l 12 -p "${2}")" = "e1071f32e403082a8e330094" ]; then
                printf '\x1f\x20\x03\xd5' | dd of="${2}" bs=1 seek=$((0x6b388)) conv=notrunc status=none
            fi
            ;;
        vendor/etc/init/android.hardware.gnss@2.0-service-qti.rc)
            # vendor_qti_diag is a Samsung/QTI config.fs AID that this build does
            # not define; init rejects the whole service over an unknown group.
            sed -i 's/ vendor_qti_diag//' "${2}"
            ;;
        lib64/libsec-ims.so)
            # utf8_length() was removed from libutils in Android 11.
            [ "$2" = "" ] && return 0
            "${PATCHELF}" --add-needed "libsecims_shim.so" "${2}"
            # CallSession::StartAllMediaForUAS() leaves the audio of an answered
            # VoWiFi call (AP voice engine, SAE) to Samsung's phone app, which
            # starts it through changeAudioPath(); AOSP never calls that, so
            # incoming VoWiFi calls had no audio. Start it on answer like outgoing
            # calls: b.ne (41 25 00 54) -> nop.
            if [ "$(xxd -s 0x3efccc -l 12 -p "${2}")" = "682a50b91f05007141250054" ]; then
                printf '\x1f\x20\x03\xd5' | dd of="${2}" bs=1 seek=$((0x3efcd4)) conv=notrunc status=none
            fi
            ;;
        lib64/libsvejni.so)
            # VoWiFi audio engine (sveservice). Links libsurfaceflinger, which no
            # longer exists, for nothing it uses.
            [ "$2" = "" ] && return 0
            "${PATCHELF}" --remove-needed "libsurfaceflinger.so" "${2}"
            ;;
        lib64/libAudioFWInterface.so)
            # Android 10 AudioRecord/AudioTrack users. libsve_shim provides the old
            # AudioRecord constructor and set(), which Android 12 changed.
            [ "$2" = "" ] && return 0
            # It allocates the objects itself, with the Android 10 sizes:
            # AudioRecord 696 -> 1128 bytes, AudioTrack 1032 -> 1264 bytes (Android 13).
            if [ "$(xxd -s 0x20b0 -l 4 -p "${2}")" = "00578052" ]; then
                printf '\x00\x8d\x80\x52' | dd of="${2}" bs=1 seek=$((0x20b0)) conv=notrunc status=none
            fi
            if [ "$(xxd -s 0x22e8 -l 4 -p "${2}")" = "00818052" ]; then
                printf '\x00\x9e\x80\x52' | dd of="${2}" bs=1 seek=$((0x22e8)) conv=notrunc status=none
            fi
            # Samsung's stream type 15 does not exist on AOSP and AudioTrack
            # rejects it; play the call on the voice call stream (0) instead:
            # orr w1, wzr, #0xf -> mov w1, #0.
            if [ "$(xxd -s 0x2818 -l 4 -p "${2}")" = "e10f0032" ]; then
                printf '\x01\x00\x80\x52' | dd of="${2}" bs=1 seek=$((0x2818)) conv=notrunc status=none
            fi
            "${PATCHELF}" --add-needed "libsve_shim.so" "${2}"
            # Android 13 moved the RefBase of AudioTrack and AudioRecord off the
            # start of the object; libsve_shim finds it before counting references.
            # Point the RefBase::incStrong()/decStrong() imports at it by renaming
            # them in place (RefBase -> SveBase keeps the string table layout).
            perl -0777 -pi -e 's/_ZNK7android7RefBase9(inc|dec)StrongEPKv/_ZNK7android7SveBase9$1StrongEPKv/g' "${2}"
            # The same change moved fields it reads inline: AudioTrack::mStatus
            # (initCheck()) 0x208 -> 0x200, AudioTrack::mSessionId (getSessionId())
            # 0x34c -> 0x35c, AudioRecord::mStatus 0x88 -> 0x8c.
            local patch addr old new
            for patch in 240c:040942b9:040142b9 2950:080842b9:080042b9 \
                    2514:144c43b9:145c43b9 2528:88698052:886b8052 21d4:048940b9:048d40b9; do
                IFS=: read -r addr old new <<< "${patch}"
                if [ "$(xxd -s 0x${addr} -l 4 -p "${2}")" = "${old}" ]; then
                    echo "${new}" | xxd -r -p | dd of="${2}" bs=1 seek=$((0x${addr})) conv=notrunc status=none
                fi
            done
            ;;
        lib64/libsamsung_videoengine_9_0.so)
            # Camera::connect() and Surface::isValid() changed in Android 11/12.
            [ "$2" = "" ] && return 0
            "${PATCHELF}" --add-needed "libsve_shim.so" "${2}"
            ;;
        vendor/lib64/hw/gatekeeper.mdfpp.so|vendor/lib64/libkeymaster2_mdfpp.so|vendor/lib64/libkeymaster_helper_vendor.so)
            # Their ASN.1 templates use the Android 13 BoringSSL struct layout,
            # which Android 14 changed; use the Android 13 libcrypto.
            [ "$2" = "" ] && return 0
            "${PATCHELF}" --replace-needed "libcrypto.so" "libcrypto-v33.so" "${2}"
            ;;
        vendor/bin/pm-service)
            # Android 14's RefBase aborts on its stack-allocated objects; use the
            # Android 13 libutils.
            [ "$2" = "" ] && return 0
            "${PATCHELF}" --replace-needed "libutils.so" "libutils-v33.so" "${2}"
            ;;
        vendor/bin/hw/android.hardware.drm@1.2-service.widevine)
            # DT_NEEDED contains unused libbinder.so, which is absent from the vendor namespace.
            [ "$2" = "" ] && return 0
            "${PATCHELF}" --remove-needed "libbinder.so" "${2}"
            ;;
        vendor/lib/libwvhidl.so)
            # References _ZN6google8protobuf8internal13empty_string_E, removed in Android 14 protobuf;
            # use the Android 10 (VNDK v29) libprotobuf-cpp-lite.
            [ "$2" = "" ] && return 0
            "${PATCHELF}" --replace-needed "libprotobuf-cpp-lite.so" "libprotobuf-cpp-lite-v29.so" "${2}"
            ;;
        lib64/libsveservice.so)
            # Parcel::print(TextOutput&), removed in Android 14; libsve_shim
            # provides it.
            [ "$2" = "" ] && return 0
            "${PATCHELF}" --add-needed "libsve_shim.so" "${2}"
            ;;
        lib64/liberis_strongswan.so)
            # Android 13's BoringSSL renamed the lh_* hash table functions to
            # OPENSSL_lh_*; liberis_shim provides the old names.
            [ "$2" = "" ] && return 0
            "${PATCHELF}" --add-needed "liberis_shim.so" "${2}"
            ;;
        bin/multiclientd)
            # Q blob importing strdup8to16(), removed from libcutils in Android 11.
            [ "$2" = "" ] && return 0
            "${PATCHELF}" --replace-needed "libcutils.so" "libcutils-v29.so" "${2}"
            ;;
        vendor/lib/libsec-ril.so|vendor/lib/libsec-ril-dsds.so|vendor/lib64/libsec-ril.so|vendor/lib64/libsec-ril-dsds.so)
            # strdup8to16() was removed from libcutils in Android 11; use the Q
            # (VNDK v29) libcutils shipped as libcutils-v29.so.
            [ "$2" = "" ] && return 0
            "${PATCHELF}" --replace-needed "libcutils.so" "libcutils-v29.so" "${2}"
            # The RIL publishes the SIM slot of the ongoing call as
            # ril.dds.call.slotid, a radio_prop that vendor processes may not
            # read. Rename it (same length) so the audio HIDL impl can use it
            # to select the SIM 2 voice session.
            sed -i 's/ril.dds.call.slotid/vendor.calls.slotid/g' "${2}"
            ;;
        vendor/lib/libmmcamera_interface.so)
            # mm_stream_streamon() iterates buf_status[] with an int8_t index.
            # HAL3 gralloc streams register 128 buffer slots, so the index wraps
            # to -128, reads garbage before the array and waits for a buffer
            # "mapping" that never happens -> stream-on timeout, no HAL3 preview.
            # Make the index unsigned: sxtb r2, r0 (42 b2) -> uxtb r2, r0 (c2 b2).
            if [ "$(xxd -s 0x1bf7c -l 8 -p "${2}")" = "013042b29142eddc" ]; then
                printf '\xc2' | dd of="${2}" bs=1 seek=$((0x1bf7e)) conv=notrunc status=none
            fi
            # mm_stream_calc_offset_snapshot() pads scanline with the backend's
            # height_padding (1). When height / 2 is odd (e.g. 5664x3186 16:9 snapshot,
            # height / 2 = 1593), Qualcomm CPP DMA writes Chroma in pairs of rows (1594 rows),
            # crossing the page boundary and triggering SMMU fault / crash.
            # Always pad scanline to a multiple of 4:
            # scanline = (h + p - 1) & -p  ->  scanline = (h + 3) & ~3 at 0x1ed10.
            if [ "$(xxd -s 0x1ed10 -l 10 -p "${2}")" = "83185242013b03ea0205" ]; then
                printf '\x00\xf1\x03\x03\x23\xf0\x03\x05\x00\xbf' | dd of="${2}" bs=1 seek=$((0x1ed10)) conv=notrunc status=none
            fi
            # mm_stream_calc_offset_snapshot() pads the snapshot stride with the
            # backend's width_padding (1), so e.g. the telephoto's 3672-wide
            # snapshot gets a stride that is not 16-byte aligned; the VFE write
            # master rounds each line up and overruns the buffer (SMMU page
            # fault, "camera daemon died"). Always pad the stride to 32:
            # stride = (w + p - 1) & -p  ->  stride = (w + 31) & ~31 at 0x1ed1a.
            if [ "$(xxd -s 0x1ed1a -l 12 -p "${2}")" = "0beb01024942013a02ea0103" ]; then
                printf '\x0b\xf1\x1f\x02\x22\xf0\x1f\x03\x00\xbf\x00\xbf' | dd of="${2}" bs=1 seek=$((0x1ed1a)) conv=notrunc status=none
            fi
            ;;
        vendor/lib/hw/vendor.samsung.hardware.camera.provider@3.0-impl.so)
            # SehCameraProvider::getCameraIdList() drops every camera id > 19;
            # only Samsung's sehGetCameraIdList() returns them. That hides the
            # telephoto (50), ultra-wide (52) and depth (54) sensors from AOSP
            # cameraserver. Hide only id 20 (a second instance of the main
            # sensor used for Samsung dual modes) instead:
            # cmp r0, #19; bgt (13 28 16 dc) -> cmp r0, #20; beq (14 28 16 d0).
            if [ "$(xxd -s 0x12ed8 -l 8 -p "${2}")" = "0ff082ed132816dc" ]; then
                printf '\x14\x28\x16\xd0' | dd of="${2}" bs=1 seek=$((0x12edc)) conv=notrunc status=none
            fi
            "${PATCHELF}" --add-needed "libcamera_metadata_shim.so" "${2}"
            ;;
        vendor/etc/camera/camera_config.xml)
            # The depth sensor (s5k5e9yx) is mounted like the other rear
            # modules, but is configured with MountAngle 270 (Samsung only uses
            # it for depth data). Exposed as camera 54 it renders upside down.
            sed -i '/<SensorName>s5k5e9yx<\/SensorName>/,/<\/CameraModuleConfig>/ s|<MountAngle>270</MountAngle>|<MountAngle>90</MountAngle>|' "${2}"
            ;;
        vendor/lib/libmmcamera2_sensor_modules.so)
            # The sensor sub-module reports ISO as
            #   100 * analog_gain * digital_gain / iso100_gain
            # but Samsung's sensor path never fills digital_gain, so it holds
            # garbage and android.sensor.sensitivity comes out as INT32_MIN.
            # Drop that multiply: vmul.f32 s0, s0, s4 -> nop.w at 0x63ad8.
            if [ "$(xxd -s 0x63ad4 -l 16 -p "${2}")" = "21ee000a20ee020a80ee030abdeec00a" ]; then
                printf '\xaf\xf3\x00\x80' | dd of="${2}" bs=1 seek=$((0x63ad8)) conv=notrunc status=none
            fi
            ;;
        vendor/lib/hw/camera.sdm660.so)
            # Video recordings were green: the HAL writes UBWC video because it
            # cannot read vendor.video.disable.ubwc, but the stock encoder only
            # takes linear NV12. Keep video linear (NV12_VENUS) while preview
            # stays UBWC - making both linear enables CPP output duplication,
            # which page-faults with this kernel. In
            # QCamera3Channel::getStreamDefaultFormat() replace the
            # isVideoUBWCEnabled() call with "movs r0, #0; nop" (0xb50d4).
            if [ "$(xxd -s 0xb50d4 -l 6 -p "${2}")" = "8cf06cee0028" ]; then
                printf '\x00\x20\x00\xbf' | dd of="${2}" bs=1 seek=$((0xb50d4)) conv=notrunc status=none
            fi
            ;;
    esac
}

# Initialize the helper
setup_vendor "${DEVICE}" "${VENDOR}" "${LINEAGE_ROOT}" true "${CLEAN_VENDOR}"

extract "${MY_DIR}/proprietary-files.txt" "${SRC}" \
        "${KANG}" --section "${SECTION}"

"${MY_DIR}/setup-makefiles.sh"
