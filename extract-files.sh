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
        vendor/etc/init/android.hardware.gnss@2.0-service-qti.rc)
            # vendor_qti_diag is a Samsung/QTI config.fs AID that this build does
            # not define; init rejects the whole service over an unknown group.
            sed -i 's/ vendor_qti_diag//' "${2}"
            ;;
        lib64/libsec-ims.so)
            # utf8_length() was removed from libutils in Android 11.
            [ "$2" = "" ] && return 0
            "${PATCHELF}" --add-needed "libsecims_shim.so" "${2}"
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
