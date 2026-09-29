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

HELPER="${LINEAGE_ROOT}/vendor/lineage/build/tools/extract_utils.sh"
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
        vendor/lib/libmmcamera_interface.so)
            # mm_stream_streamon() iterates buf_status[] with an int8_t index.
            # HAL3 gralloc streams register 128 buffer slots, so the index wraps
            # to -128, reads garbage before the array and waits for a buffer
            # "mapping" that never happens -> stream-on timeout, no HAL3 preview.
            # Make the index unsigned: sxtb r2, r0 (42 b2) -> uxtb r2, r0 (c2 b2).
            if [ "$(xxd -s 0x1bf7c -l 8 -p "${2}")" = "013042b29142eddc" ]; then
                printf '\xc2' | dd of="${2}" bs=1 seek=$((0x1bf7e)) conv=notrunc status=none
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
            ;;
    esac
}

# Initialize the helper
setup_vendor "${DEVICE}" "${VENDOR}" "${LINEAGE_ROOT}" true "${CLEAN_VENDOR}"

extract "${MY_DIR}/proprietary-files.txt" "${SRC}" \
        "${KANG}" --section "${SECTION}"

"${MY_DIR}/setup-makefiles.sh"
