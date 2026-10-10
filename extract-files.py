#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: 2024 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

from extract_utils.main import (
    ExtractUtils,
    ExtractUtilsModule,
)

from extract_utils.fixups_lib import (
    lib_fixup_remove,
    lib_fixups_user_type,
)

def lib_fixup_vendor(lib: str, partition: str, *args, **kwargs):
    return f'{lib}_vendor' if partition in ['odm', 'vendor'] else lib

def lib_fixup_product_hidl(lib: str, partition: str, *args, **kwargs):
    return '' if partition == 'product' else lib

lib_fixups: lib_fixups_user_type = {
    (
        'com.qualcomm.qti.ant@1.0',
        'vendor.qti.hardware.fm@1.0',
        'vendor.samsung.hardware.radio.bridge@2.0',
        'libfloatingfeature',
        'libmdf',
        'libsavscmn',
        'libsecaudiocoreutils',
        'libsecnativefeature',
        'libsecure_storage',
    ): lib_fixup_vendor,
    (
        'vendor.samsung.frameworks.security.ucm.crypto@1.0',
        'libucm_tlc_tz_esecomm',
    ): lib_fixup_remove,
    (
        'libhwbinder',
        'libhidltransport',
    ): lib_fixup_product_hidl,
}

namespace_imports = [
    'device/samsung/a9y18qlte',
    'hardware/qcom-caf/common/libqti-perfd-client',
    'hardware/qcom-caf/msm8998',
    'hardware/qcom-caf/wlan',
    'vendor/qcom/opensource/dataservices',
    'vendor/samsung/a9y18qlte',
]

module = ExtractUtilsModule(
    'a9y18qlte',
    'samsung',
    namespace_imports=namespace_imports,
    lib_fixups=lib_fixups,
    check_elf=True,
)

if __name__ == '__main__':
    utils = ExtractUtils.device(module)
    utils.run()
