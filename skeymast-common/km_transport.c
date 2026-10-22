// SPDX-License-Identifier: Apache-2.0
// OSS QSEECom client for the Samsung skeymast trusted app.
//
// Mirrors the stock control flow from gatekeeper.mdfpp.so:
//   nwd_tz_open:    QSEECom_start_app("skeymast", 2 * aligned(size))
//   nwd_tz_run_cmd: set_bandwidth(1); send_cmd(in,len,out,len); set_bandwidth(0)
//   pack:           i2d_KM_INDATA into the in scratch buffer (len prefix u64)
//   unpack:         d2i_KM_OUTDATA from the out scratch buffer
// All sizes/alignment are taken verbatim from the RE (PLAN.md): buffer payload
// max 0x2000, 8-byte length prefix => 0x2008, rounded up to 64 bytes.

#define LOG_TAG "km_tz_oss"

#include <stdio.h>
#include "km_transport.h"

#include <log/log.h>
#include <stdlib.h>
#include <string.h>

#include "QSEEComAPI.h"

#define KM_TA_PATH "/vendor/firmware_mnt/image/"
#define KM_TA_NAME "skeymast"

#define KM_ALIGN64(x)  (((x) + 0x3fu) & ~0x3fu)
// Region size matches the stock KEYMASTER buffer (region 0x19080, sb 0x32100):
// keymaster attest_key injects the provisioned /efs/DAK keys+certs (~11 KB), far
// more than gatekeeper's 0x2080. Using the larger size for both HALs is safe -
// the TA reads the length-prefixed DER, not the buffer size - and gatekeeper
// re-verified fine with it. Payload = region - 8 (the u64 length prefix).
#define KM_REGION      0x19080u
#define KM_BUF_PAYLOAD (KM_REGION - 8u)

static struct QSEECom_handle *g_handle = NULL;

// In/out scratch buffers are our OWN memory, NOT the ION buffer. Stock uses
// static .bss buffers here: QSEECom_send_cmd copies our request into the ION
// buffer and copies the response back out into our out-buffer. If we pointed
// these inside the ION buffer, send_cmd would skip the copy-back and we'd read
// a stale (zero) response. 64-byte aligned, [u64 len][DER payload] layout.
static uint8_t g_inbuf[KM_REGION + 64] __attribute__((aligned(64)));
static uint8_t g_outbuf[KM_REGION + 64] __attribute__((aligned(64)));

// Shared-buffer helpers: first 8 bytes hold the DER length, payload follows.
static inline uint8_t *buf_payload(uint8_t *b) { return b + 8; }
static inline void buf_set_len(uint8_t *b, uint32_t n) { *(uint64_t *)b = n; }
static inline uint32_t buf_get_len(const uint8_t *b) { return (uint32_t)(*(const uint64_t *)b); }

int km_tz_open(void) {
    if (g_handle != NULL) return 0;  // stock guards reopen the same way
    // Stock requests 2 * aligned(size): one region for in, one for out.
    uint32_t region = KM_REGION;
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

    // Our own in/out scratch (see note above). send_cmd copies to/from ION.
    uint32_t region = KM_REGION;
    uint8_t *inbuf = g_inbuf;
    uint8_t *outbuf = g_outbuf;

    // Encode the request DER straight into the in-buffer payload.
    uint32_t der_len = 0;
    if (km_indata_encode(in, buf_payload(inbuf), KM_BUF_PAYLOAD, &der_len) != 0) {
        ALOGE("km_indata_encode failed");
        return -0x26;
    }
    buf_set_len(inbuf, der_len);
    buf_set_len(outbuf, 0);  // stock zeroes the out length before each cmd

    // Stock sends the full aligned region as both request and response length
    // (QSEECom_send_cmd copies the whole region in and the response back out),
    // regardless of the actual DER size.
    // Stock sends the full aligned region as both request and response length.
    QSEECom_set_bandwidth(g_handle, 1);
    int rc = QSEECom_send_cmd(g_handle, inbuf, region, outbuf, region);
    QSEECom_set_bandwidth(g_handle, 0);
    if (rc != 0) {
        ALOGE("QSEECom_send_cmd failed: %d", rc);
        return -0x26;
    }

    // Decode the response.
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
