// SPDX-License-Identifier: Apache-2.0
// KM_INDATA/KM_OUTDATA wire format for the Samsung skeymast TA.
//
// Encoded/decoded by hand with BoringSSL's CBB/CBS byte API rather than the
// ASN1_ITEM template macros. The template descriptor structs changed layout
// between BoringSSL versions (Android 12.1 vs 16), so a module compiled against
// one and linked against another's libcrypto corrupts memory — that is exactly
// why the stock prebuilts are pinned to libcrypto-v33. CBB/CBS are ABI-stable
// across versions, so this module links whatever libcrypto the platform ships.
// The DER produced is canonical and byte-identical to the stock templates.
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

// Request. ver/km_ver are emitted as the constants above; caller sets the rest.
// Optional fields [0]int0 [1]long0 [2]bin0 [3]bin1 [4]key use EXPLICIT (constructed)
// context tags wrapping a universal INTEGER/OCTET STRING, matching the stock
// skeymast wire format. A field is emitted when its *_set flag is true, EVEN IF
// its octet length is 0 (stock emits empty octet strings for present-but-empty
// fields; the TA expects the slots). long0's inner INTEGER uses the raw minimal
// big-endian encoding (stock km_set_ASN1_INTEGER_BN: no sign pad).
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
