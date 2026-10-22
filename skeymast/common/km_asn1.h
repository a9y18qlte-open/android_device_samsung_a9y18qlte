// SPDX-License-Identifier: Apache-2.0
// KM_INDATA/KM_OUTDATA wire format for the Samsung skeymast TA. Encoded/decoded
// with BoringSSL's ABI-stable CBB/CBS byte API (not the ASN1_ITEM macros), so
// the module links whatever platform libcrypto ships, no libcrypto-v33 pin.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KM_BLOB_MAX 0xc00  // stock octet-string cap

typedef struct {
    const uint8_t *data;
    uint32_t len;
} km_blob;

// Fixed header values the TA checks (see PLAN.md).
enum { KM_WIRE_VER = 2, KM_WIRE_KMVER = 20 };
enum { KM_CMD_ENROLL = 0x201, KM_CMD_VERIFY = 0x202 };

// Keymaster tag type lives in the top nibble of the tag (keymaster_defs.h).
#define KM_TAG_TYPE(tag) ((tag) & 0xf0000000u)
#define KM_TYPE_ENUM     0x10000000u
#define KM_TYPE_ENUM_REP 0x20000000u
#define KM_TYPE_UINT     0x30000000u
#define KM_TYPE_UINT_REP 0x40000000u
#define KM_TYPE_ULONG    0x50000000u
#define KM_TYPE_DATE     0x60000000u
#define KM_TYPE_BOOL     0x70000000u
#define KM_TYPE_BIGNUM   0x80000000u
#define KM_TYPE_BYTES    0x90000000u
#define KM_TYPE_ULONG_REP 0xa0000000u

// One keymaster key parameter: `b` for BYTES/BIGNUM tags, `i` otherwise.
typedef struct {
    uint32_t tag;
    uint64_t i;
    km_blob b;
} km_param;

#define KM_MAX_PARAMS 64

// Request. Optional fields [0]int0 [1]long0 [2]bin0 [3]bin1 [4]key are emitted
// when their *_set flag is true, even with length 0 (the TA expects the slots).
typedef struct {
    uint32_t cmd;
    uint32_t pid;
    bool int0_set;
    uint32_t int0;
    bool long0_set;
    uint64_t long0;
    bool bin0_set;
    km_blob bin0;
    bool bin1_set;
    km_blob bin1;
    bool key_set;
    km_blob key;
    // par [8]: SET OF KM_PARAM, emitted when n_params > 0.
    const km_param *params;
    uint32_t n_params;
} km_indata;

// Response. Owns heap copies of the octet fields; free with km_outdata_release.
typedef struct {
    int32_t err;
    bool int0_set;
    int64_t int0;
    bool long0_set;
    int64_t long0;
    uint8_t *bin0;
    uint32_t bin0_len;
    uint8_t *bin1;
    uint32_t bin1_len;
    // par [4]: byte values point into the caller's DER buffer; keep it alive.
    km_param params[KM_MAX_PARAMS];
    uint32_t n_params;
} km_outdata;

// Encode `in` as DER into out[0..*out_len). Returns 0 on success; sets *out_len
// to the byte count. Fails if it would exceed cap.
int km_indata_encode(const km_indata *in, uint8_t *out, uint32_t cap, uint32_t *out_len);

// Parse DER at der[0..der_len) into *out (zeroed first). Returns 0 on success.
int km_outdata_decode(const uint8_t *der, uint32_t der_len, km_outdata *out);

void km_outdata_release(km_outdata *out);

#ifdef __cplusplus
}
#endif
