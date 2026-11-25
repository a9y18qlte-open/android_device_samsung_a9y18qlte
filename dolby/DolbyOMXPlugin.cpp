/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "DolbyOMXPlugin"

#include <dlfcn.h>
#include <string.h>

#include <iterator>

#include <log/log.h>
#include <media/hardware/OMXPluginBase.h>
#include <media/stagefright/omx/SoftOMXComponent.h>

namespace android {

namespace {

struct Component {
    const char* name;
    const char* lib;
    const char* role;
};

constexpr Component kComponents[] = {
        {"OMX.dolby.ac3.decoder", "libstagefright_soft_ddpdec.so", "audio_decoder.ac3"},
        {"OMX.dolby.eac3.decoder", "libstagefright_soft_ddpdec.so", "audio_decoder.eac3"},
        {"OMX.dolby.eac3_joc.decoder", "libstagefright_soft_ddpdec.so", "audio_decoder.eac3_joc"},
        {"OMX.dolby.ac4.decoder", "libstagefright_soft_ac4dec.so", "audio_decoder.ac4"},
};

const Component* find(const char* name) {
    for (const auto& c : kComponents) {
        if (!strcmp(name, c.name)) return &c;
    }
    return nullptr;
}

using CreateFunc = SoftOMXComponent* (*)(const char*, const OMX_CALLBACKTYPE*, OMX_PTR,
                                         OMX_COMPONENTTYPE**);

class DolbyOMXPlugin : public OMXPluginBase {
  public:
    OMX_ERRORTYPE makeComponentInstance(const char* name, const OMX_CALLBACKTYPE* callbacks,
                                        OMX_PTR appData, OMX_COMPONENTTYPE** component) override {
        const Component* c = find(name);
        if (!c) return OMX_ErrorInvalidComponentName;

        void* lib = dlopen(c->lib, RTLD_NOW | RTLD_NODELETE);
        if (!lib) {
            ALOGE("dlopen %s: %s", c->lib, dlerror());
            return OMX_ErrorComponentNotFound;
        }
        auto create = reinterpret_cast<CreateFunc>(dlsym(
                lib, "_Z22createSoftOMXComponentPKcPK16OMX_CALLBACKTYPEPvPP17OMX_COMPONENTTYPE"));
        if (!create) {
            dlclose(lib);
            return OMX_ErrorComponentNotFound;
        }

        sp<SoftOMXComponent> codec = create(name, callbacks, appData, component);
        if (codec == nullptr) {
            dlclose(lib);
            return OMX_ErrorInsufficientResources;
        }
        if (OMX_ERRORTYPE err = codec->initCheck(); err != OMX_ErrorNone) {
            dlclose(lib);
            return err;
        }
        codec->incStrong(this);
        codec->setLibHandle(lib);
        return OMX_ErrorNone;
    }

    OMX_ERRORTYPE destroyComponentInstance(OMX_COMPONENTTYPE* component) override {
        auto* me = static_cast<SoftOMXComponent*>(component->pComponentPrivate);
        me->prepareForDestruction();
        void* lib = me->libHandle();
        me->decStrong(this);
        dlclose(lib);
        return OMX_ErrorNone;
    }

    OMX_ERRORTYPE enumerateComponents(OMX_STRING name, size_t size, OMX_U32 index) override {
        if (index >= std::size(kComponents)) return OMX_ErrorNoMore;
        strlcpy(name, kComponents[index].name, size);
        return OMX_ErrorNone;
    }

    OMX_ERRORTYPE getRolesOfComponent(const char* name, Vector<String8>* roles) override {
        const Component* c = find(name);
        if (!c) return OMX_ErrorInvalidComponentName;
        roles->clear();
        roles->push(String8(c->role));
        return OMX_ErrorNone;
    }
};

}  // namespace

extern "C" OMXPluginBase* createOMXPlugin() {
    return new DolbyOMXPlugin();
}

extern "C" void destroyOMXPlugin(OMXPluginBase* plugin) {
    delete plugin;
}

}  // namespace android
