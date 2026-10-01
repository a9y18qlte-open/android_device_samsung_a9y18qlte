/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Loads Samsung's libsec-ril and passes everything through, except that a
 * RIL_REQUEST_SIM_AUTHENTICATION answer is reported as successful when the
 * card itself returned 90 00 with data.
 *
 * libsec-ril reports some valid AKA answers, e.g. a synchronisation failure
 * carrying AUTS (3GPP TS 31.102 7.1.2.1, tag DC), as RIL_E_INTERNAL_ERR. The
 * framework drops the card's answer on any error, so the IMS stack can never
 * answer the network's challenge and VoLTE registration stops at 401.
 */

#define LOG_TAG "secril-shim"

#include <dlfcn.h>
#include <pthread.h>
#include <string.h>

#include <cutils/properties.h>
#include <log/log.h>
#include <telephony/ril.h>

#if defined(REAL_RIL_DSDS)
#define RIL_SLOT "1"
#else
#define RIL_SLOT "0"
#endif

#if defined(REAL_RIL_DSDS)
#define REAL_RIL_PATH "/vendor/lib64/libsec-ril-dsds.so"
#else
#define REAL_RIL_PATH "/vendor/lib64/libsec-ril.so"
#endif

typedef const RIL_RadioFunctions* (*RilInitFunc)(const struct RIL_Env* env, int argc, char** argv);

#define MAX_PENDING_AUTH 8

static void* gRealRil;
static const struct RIL_Env* gRilEnv;
static struct RIL_Env gShimEnv;
static const RIL_RadioFunctions* gRealFuncs;
static RIL_RadioFunctions gShimFuncs;

static pthread_mutex_t gAuthLock = PTHREAD_MUTEX_INITIALIZER;
static RIL_Token gPendingAuth[MAX_PENDING_AUTH];
static unsigned gNextAuthSlot;

static void trackAuth(RIL_Token t) {
    pthread_mutex_lock(&gAuthLock);
    gPendingAuth[gNextAuthSlot++ % MAX_PENDING_AUTH] = t;
    pthread_mutex_unlock(&gAuthLock);
}

static int untrackAuth(RIL_Token t) {
    int found = 0;
    pthread_mutex_lock(&gAuthLock);
    for (int i = 0; i < MAX_PENDING_AUTH; i++) {
        if (gPendingAuth[i] == t) {
            gPendingAuth[i] = NULL;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&gAuthLock);
    return found;
}

static void shimOnRequestComplete(RIL_Token t, RIL_Errno e, void* response, size_t responselen) {
    if (t != NULL && untrackAuth(t) && e != RIL_E_SUCCESS && response != NULL &&
        responselen >= sizeof(RIL_SIM_IO_Response)) {
        const RIL_SIM_IO_Response* io = (const RIL_SIM_IO_Response*)response;
        if (io->sw1 == 0x90 && io->sw2 == 0x00 && io->simResponse != NULL &&
            io->simResponse[0] != '\0') {
            ALOGI("SIM_AUTHENTICATION: card answered 90 00, reporting success instead of error %d",
                  e);
            e = RIL_E_SUCCESS;
        }
    }
    gRilEnv->OnRequestComplete(t, e, response, responselen);
}

/*
 * Samsung's IMS stack tells the RIL instance of the SIM it is using when a
 * VoLTE call starts and ends (OEM IPC 0x02/0x80, "LTE proc type" 1 / 0).
 * Publish that SIM for the audio HAL, which otherwise only learns the SIM
 * of circuit-switched calls (vendor.calls.slotid).
 */
static void checkImsCall(const unsigned char* p, size_t len) {
    if (len < 5 || p[0] != 0x02 || p[1] != 0x80) return;
    ALOGI("IMS call state %u", p[4]);
    if (p[4] == 1) {
        property_set("vendor.calls.ims_slotid", RIL_SLOT);
    } else if (p[4] == 0) {
        property_set("vendor.calls.ims_slotid", "-1");
    }
}

static void shimOnRequest(int request, void* data, size_t datalen, RIL_Token t) {
    if (request == RIL_REQUEST_SIM_AUTHENTICATION) trackAuth(t);
    if (request == RIL_REQUEST_OEM_HOOK_RAW && data != NULL) {
        checkImsCall((const unsigned char*)data, datalen);
    }
    gRealFuncs->onRequest(request, data, datalen, t);
}

static void* realRil(void) {
    if (gRealRil == NULL) {
        gRealRil = dlopen(REAL_RIL_PATH, RTLD_NOW);
        if (gRealRil == NULL) ALOGE("dlopen %s failed: %s", REAL_RIL_PATH, dlerror());
    }
    return gRealRil;
}

const RIL_RadioFunctions* RIL_Init(const struct RIL_Env* env, int argc, char** argv) {
    RilInitFunc init;

    if (realRil() == NULL) return NULL;
    init = (RilInitFunc)dlsym(gRealRil, "RIL_Init");
    if (init == NULL) {
        ALOGE("%s has no RIL_Init", REAL_RIL_PATH);
        return NULL;
    }

    // A VoLTE call cannot outlive this RIL instance; drop a slot left behind
    // by a crash so the audio HAL does not use it for later calls.
    char slot[PROPERTY_VALUE_MAX];
    property_get("vendor.calls.ims_slotid", slot, "-1");
    if (!strcmp(slot, RIL_SLOT)) property_set("vendor.calls.ims_slotid", "-1");

    gRilEnv = env;
    gShimEnv = *env;
    gShimEnv.OnRequestComplete = shimOnRequestComplete;

    gRealFuncs = init(&gShimEnv, argc, argv);
    if (gRealFuncs == NULL) return NULL;

    gShimFuncs = *gRealFuncs;
    gShimFuncs.onRequest = shimOnRequest;
    ALOGI("loaded %s", REAL_RIL_PATH);
    return &gShimFuncs;
}

const RIL_RadioFunctions* RIL_SAP_Init(const struct RIL_Env* env, int argc, char** argv) {
    RilInitFunc init;

    if (realRil() == NULL) return NULL;
    init = (RilInitFunc)dlsym(gRealRil, "RIL_SAP_Init");
    if (init == NULL) return NULL;
    return init(env, argc, argv);
}
