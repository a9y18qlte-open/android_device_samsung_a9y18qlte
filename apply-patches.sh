#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Apply the patches under patches/ to the source tree, by path:
# patches/hardware/samsung/*.patch applies to hardware/samsung. Run it from
# anywhere before building; patches that are already applied are skipped.

set -e

DEVICE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${DEVICE_DIR}/../../.." && pwd)"

cd "${DEVICE_DIR}/patches"
for PATCH in $(find . -name '*.patch' | sort); do
    PROJECT="$(dirname "${PATCH#./}")"
    TARGET="${ROOT_DIR}/${PROJECT}"
    NAME="${PROJECT}/$(basename "${PATCH}")"

    if [ ! -d "${TARGET}" ]; then
        echo "[!] ${NAME}: ${PROJECT} not found" >&2
        exit 1
    fi
    if (cd "${TARGET}" && git apply --reverse --check "${DEVICE_DIR}/patches/${PATCH#./}" 2>/dev/null); then
        echo "[=] ${NAME}: already applied"
    elif (cd "${TARGET}" && git apply --check "${DEVICE_DIR}/patches/${PATCH#./}" 2>/dev/null); then
        (cd "${TARGET}" && git apply "${DEVICE_DIR}/patches/${PATCH#./}")
        echo "[+] ${NAME}: applied"
    else
        echo "[!] ${NAME}: does not apply" >&2
        exit 1
    fi
done
