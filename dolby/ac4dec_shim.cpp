/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "ac4dec_shim"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

#include <OMX_Component.h>
#include <log/log.h>

namespace android {
class SoftOMXComponent;
}

namespace {

constexpr const char* kDecoderLib = "libstagefright_soft_ac4dec_dolby.so";
constexpr const char* kTablesPath = "/vendor/etc/dolby/ac4_tables.bin";
constexpr OMX_INDEXTYPE kIndexParamAudioAc4Tables = static_cast<OMX_INDEXTYPE>(0x6f400008);

struct Ac4TablesParam {
    uint32_t nSize;
    uint8_t seed[3];
    uint8_t type[3];
    uint8_t key[3];
    uint8_t reserved[3];
    uint32_t size[3];
    uint8_t tables[256 + 84 + 84];
};
static_assert(sizeof(Ac4TablesParam) == 452);

bool readTables(uint8_t* tables, size_t size) {
    FILE* f = fopen(kTablesPath, "rbe");
    if (!f) {
        ALOGE("open %s failed", kTablesPath);
        return false;
    }
    bool ok = fread(tables, 1, size, f) == size && fgetc(f) == EOF;
    fclose(f);
    if (!ok) ALOGE("%s is not %zu bytes", kTablesPath, size);
    return ok;
}

using CreateFn = android::SoftOMXComponent* (*)(const char*, const OMX_CALLBACKTYPE*, OMX_PTR,
                                                OMX_COMPONENTTYPE**);

CreateFn loadCreate() {
    void* handle = dlopen(kDecoderLib, RTLD_NOW);
    if (!handle) {
        ALOGE("dlopen %s: %s", kDecoderLib, dlerror());
        return nullptr;
    }
    auto fn = reinterpret_cast<CreateFn>(dlsym(
            handle, "_Z22createSoftOMXComponentPKcPK16OMX_CALLBACKTYPEPvPP17OMX_COMPONENTTYPE"));
    if (!fn) ALOGE("dlsym: %s", dlerror());
    return fn;
}

}  // namespace

android::SoftOMXComponent* createSoftOMXComponent(const char* name,
                                                  const OMX_CALLBACKTYPE* callbacks,
                                                  OMX_PTR appData, OMX_COMPONENTTYPE** component) {
    static CreateFn create = loadCreate();
    if (!create) return nullptr;

    android::SoftOMXComponent* comp = create(name, callbacks, appData, component);
    if (!comp || !component || !*component) return comp;

    Ac4TablesParam param = {
            .nSize = sizeof(Ac4TablesParam),
            .type = {9, 10, 11},
            .key = {0, 4, 8},
            .size = {256, 84, 84},
    };
    if (!readTables(param.tables, sizeof(param.tables))) return comp;

    OMX_ERRORTYPE err = (*component)->SetParameter(*component, kIndexParamAudioAc4Tables, &param);
    if (err != OMX_ErrorNone) ALOGE("Failed to set AC-4 tables for %s: %#x", name, err);
    return comp;
}
