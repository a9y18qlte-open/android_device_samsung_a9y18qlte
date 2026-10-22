// SPDX-License-Identifier: Apache-2.0
// OSS QSEECom client for the Samsung skeymast trusted app. Open starts the TA
// with a 2*region shared buffer; transact encodes KM_INDATA into [u64 len][DER],
// sends it over QSEECom and decodes KM_OUTDATA from the response.

#define LOG_TAG "km_tz_oss"

#include "km_transport.h"

#include <log/log.h>
#include <stdlib.h>
#include <string.h>

#include "QSEEComAPI.h"

#define KM_TA_PATH "/vendor/firmware_mnt/image/"
#define KM_TA_NAME "skeymast"

// Stock keymaster region (0x19080): big enough for attest_key's ~11 KB /efs/DAK
// injection. Shared by both HALs; the TA reads the length prefix, not the size.
#define KM_REGION      0x19080u
#define KM_BUF_PAYLOAD (KM_REGION - 8u)

static struct QSEECom_handle *g_handle = NULL;

// Scratch buffers must be our OWN memory, not the ION buffer: QSEECom_send_cmd
// copies the request in and the response back out; pointing inside ION skips the
// copy-back and reads a stale response. 64-byte aligned, [u64 len][DER] layout.
static uint8_t g_inbuf[KM_REGION + 64] __attribute__((aligned(64)));
static uint8_t g_outbuf[KM_REGION + 64] __attribute__((aligned(64)));

static inline uint8_t *buf_payload(uint8_t *b) { return b + 8; }
static inline void buf_set_len(uint8_t *b, uint32_t n) { *(uint64_t *)b = n; }
static inline uint32_t buf_get_len(const uint8_t *b) { return (uint32_t)(*(const uint64_t *)b); }

int km_tz_open(void) {
    if (g_handle != NULL) return 0;
    uint32_t region = KM_REGION;  // *2: one region in, one out (as stock)
    int rc = QSEECom_start_app(&g_handle, KM_TA_PATH, KM_TA_NAME, region * 2);
    if (rc != 0 || g_handle == NULL) {
        ALOGE("QSEECom_start_app(%s) failed: %d", KM_TA_NAME, rc);
        g_handle = NULL;
        return -1;
    }
    return 0;
}

int km_tz_close(void) {
    if (g_handle == NULL) return 0;
    int rc = QSEECom_shutdown_app(&g_handle);
    if (rc != 0) ALOGE("QSEECom_shutdown_app failed: %d", rc);
    g_handle = NULL;
    return rc;
}

int km_tz_transact(const km_indata *in, km_outdata *out) {
    if (in == NULL || out == NULL) return -1;
    if (g_handle == NULL) return -500;  // KM connection-not-open, as stock

    uint32_t region = KM_REGION;
    uint8_t *inbuf = g_inbuf;
    uint8_t *outbuf = g_outbuf;

    uint32_t der_len = 0;
    if (km_indata_encode(in, buf_payload(inbuf), KM_BUF_PAYLOAD, &der_len) != 0) {
        ALOGE("km_indata_encode failed");
        return -0x26;
    }
    buf_set_len(inbuf, der_len);
    buf_set_len(outbuf, 0);  // stock zeroes the out length before each cmd

    // Stock sends the full region as both request and response length.
    QSEECom_set_bandwidth(g_handle, 1);
    int rc = QSEECom_send_cmd(g_handle, inbuf, region, outbuf, region);
    QSEECom_set_bandwidth(g_handle, 0);
    if (rc != 0) {
        ALOGE("QSEECom_send_cmd failed: %d", rc);
        return -0x26;
    }

    uint32_t resp_len = buf_get_len(outbuf);
    if (resp_len == 0 || resp_len > KM_BUF_PAYLOAD) {
        ALOGE("bad response len %u", resp_len);
        return -0x3e8;
    }
    if (km_outdata_decode(buf_payload(outbuf), resp_len, out) != 0) {
        ALOGE("km_outdata_decode failed (len=%u)", resp_len);
        return -0x3e8;
    }
    return 0;
}
