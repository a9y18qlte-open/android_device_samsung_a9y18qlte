/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Loads Samsung's libsec-ril and passes everything through, except:
 *
 * - A RIL_REQUEST_SIM_AUTHENTICATION answer is reported as successful when the
 *   card itself returned 90 00 with data. libsec-ril reports some valid AKA
 *   answers, e.g. a synchronisation failure carrying AUTS (3GPP TS 31.102
 *   7.1.2.1, tag DC), as RIL_E_INTERNAL_ERR. The framework drops the card's
 *   answer on any error, so the IMS stack can never answer the network's
 *   challenge and VoLTE registration stops at 401.
 *
 * - The RIL_REQUEST_GET_SMSC_ADDRESS answer is turned from the AT+CSCA form
 *   libsec-ril returns ("8491020005",145) into the dial string the framework
 *   expects (+8491020005). The framework keeps only the digits before the
 *   comma, so the SMSC lost its international type; over IMS the SC address
 *   becomes the SIP target of the SMS, and the network cannot route it
 *   without the "+".
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

#include <stdlib.h>

#define MAX_PENDING 8

static void* gRealRil;
static const struct RIL_Env* gRilEnv;
static struct RIL_Env gShimEnv;
static const RIL_RadioFunctions* gRealFuncs;
static RIL_RadioFunctions gShimFuncs;

struct PendingTokens {
    RIL_Token tokens[MAX_PENDING];
    unsigned next;
};

static pthread_mutex_t gPendingLock = PTHREAD_MUTEX_INITIALIZER;
static struct PendingTokens gPendingAuth;
static struct PendingTokens gPendingSmsc;

static void track(struct PendingTokens* list, RIL_Token t) {
    pthread_mutex_lock(&gPendingLock);
    list->tokens[list->next++ % MAX_PENDING] = t;
    pthread_mutex_unlock(&gPendingLock);
}

static int untrack(struct PendingTokens* list, RIL_Token t) {
    int found = 0;
    pthread_mutex_lock(&gPendingLock);
    for (int i = 0; i < MAX_PENDING; i++) {
        if (list->tokens[i] == t) {
            list->tokens[i] = NULL;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&gPendingLock);
    return found;
}

/*
 * "8491020005",145 -> +8491020005 (type 145: international number). Returns 0
 * when the answer is not in that form and should be passed on unchanged.
 */
static int smscToDialString(const char* in, char* out, size_t outlen) {
    const char* comma = strchr(in, ',');
    size_t n = 0;

    if (comma == NULL) return 0;
    if (atoi(comma + 1) == 145 && strchr(in, '+') == NULL && n + 1 < outlen) out[n++] = '+';
    for (const char* p = in; p < comma && n + 1 < outlen; p++) {
        if (*p != '"' && *p != ' ') out[n++] = *p;
    }
    out[n] = '\0';
    return 1;
}

static void shimOnRequestComplete(RIL_Token t, RIL_Errno e, void* response, size_t responselen) {
    char smsc[64];

    if (t != NULL && untrack(&gPendingSmsc, t)) {
        if (e == RIL_E_SUCCESS && response != NULL &&
            smscToDialString((const char*)response, smsc, sizeof(smsc))) {
            gRilEnv->OnRequestComplete(t, e, smsc, strlen(smsc) + 1);
            return;
        }
    } else if (t != NULL && untrack(&gPendingAuth, t) && e != RIL_E_SUCCESS && response != NULL &&
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
    if (request == RIL_REQUEST_SIM_AUTHENTICATION) track(&gPendingAuth, t);
    if (request == RIL_REQUEST_GET_SMSC_ADDRESS) track(&gPendingSmsc, t);
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
