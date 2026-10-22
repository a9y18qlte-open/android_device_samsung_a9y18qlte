// SPDX-License-Identifier: Apache-2.0
// DER encode/decode for KM_INDATA/KM_OUTDATA using BoringSSL's CBB/CBS byte API.
// Output is canonical DER, byte-identical to the stock skeymast templates.
//
// NOTE on BoringSSL ABI: this uses the CBB/CBS *functions* (not the ASN1_ITEM
// template macros, whose descriptor struct layout changed between versions). It
// builds against the platform's own BoringSSL headers, so the CBB struct layout
// matches the libcrypto it links — which soong guarantees in-tree. The CBB
// struct is NOT stable across BoringSSL versions, so do not cross-compile this
// against mismatched (e.g. older) openssl headers.
#include "km_asn1.h"

#include <openssl/bytestring.h>
#include <stdlib.h>
#include <string.h>

// Explicit (constructed) context-specific tag [n]: wraps a universal element.
#define EXP(n) (CBS_ASN1_CONTEXT_SPECIFIC | CBS_ASN1_CONSTRUCTED | (unsigned)(n))

static int put_int_content(CBB *i, uint64_t v, int pad) {
    uint8_t be[9];
    int n = 0;
    for (int b = 7; b >= 0; b--) {
        uint8_t byte = (uint8_t)(v >> (b * 8));
        if (n == 0 && byte == 0) continue;
        be[n++] = byte;
    }
    if (n == 0) be[n++] = 0;
    if (pad && (be[0] & 0x80) && !CBB_add_u8(i, 0x00)) return 0;
    return CBB_add_bytes(i, be, (size_t)n);
}

int km_indata_encode(const km_indata *in, uint8_t *out, uint32_t cap, uint32_t *out_len) {
    if (in->bin0.len > KM_BLOB_MAX || in->bin1.len > KM_BLOB_MAX || in->key.len > KM_BLOB_MAX)
        return -1;

    CBB cbb, seq, wrap, inner;
    if (!CBB_init_fixed(&cbb, out, cap)) return -3;
    int ok = CBB_add_asn1(&cbb, &seq, CBS_ASN1_SEQUENCE) &&
             CBB_add_asn1_uint64(&seq, KM_WIRE_VER) &&
             CBB_add_asn1_uint64(&seq, KM_WIRE_KMVER) &&
             CBB_add_asn1_uint64(&seq, in->cmd) &&
             CBB_add_asn1_uint64(&seq, in->pid);
    if (ok && in->int0_set)
        ok = CBB_add_asn1(&seq, &wrap, EXP(0)) && CBB_add_asn1(&wrap, &inner, CBS_ASN1_INTEGER) &&
             put_int_content(&inner, in->int0, 1);
    if (ok && in->long0_set)
        ok = CBB_add_asn1(&seq, &wrap, EXP(1)) && CBB_add_asn1(&wrap, &inner, CBS_ASN1_INTEGER) &&
             put_int_content(&inner, in->long0, 0);
    if (ok && in->bin0_set)
        ok = CBB_add_asn1(&seq, &wrap, EXP(2)) && CBB_add_asn1(&wrap, &inner, CBS_ASN1_OCTETSTRING) &&
             (in->bin0.len == 0 || CBB_add_bytes(&inner, in->bin0.data, in->bin0.len));
    if (ok && in->bin1_set)
        ok = CBB_add_asn1(&seq, &wrap, EXP(3)) && CBB_add_asn1(&wrap, &inner, CBS_ASN1_OCTETSTRING) &&
             (in->bin1.len == 0 || CBB_add_bytes(&inner, in->bin1.data, in->bin1.len));
    if (ok && in->key_set)
        ok = CBB_add_asn1(&seq, &wrap, EXP(4)) && CBB_add_asn1(&wrap, &inner, CBS_ASN1_OCTETSTRING) &&
             (in->key.len == 0 || CBB_add_bytes(&inner, in->key.data, in->key.len));

    // par [8]: EXPLICIT { SET OF KM_PARAM }. Each KM_PARAM is
    // SEQUENCE{ INTEGER tag, [0]{INTEGER i} | [1]{OCTET b} }. The value slot is
    // [1] for BYTES/BIGNUM tags, [0] otherwise. CBB_flush_asn1_set_of sorts the
    // set into DER order (stock emits it sorted by tag).
    if (ok && in->n_params > 0) {
        CBB pwrap, setof, pseq, pv, pvi;
        ok = CBB_add_asn1(&seq, &pwrap, EXP(8)) && CBB_add_asn1(&pwrap, &setof, CBS_ASN1_SET);
        for (uint32_t k = 0; ok && k < in->n_params; k++) {
            const km_param *pp = &in->params[k];
            uint32_t ty = KM_TAG_TYPE(pp->tag);
            ok = CBB_add_asn1(&setof, &pseq, CBS_ASN1_SEQUENCE) &&
                 CBB_add_asn1(&pseq, &pvi, CBS_ASN1_INTEGER) && put_int_content(&pvi, pp->tag, 1);
            if (!ok) break;
            if (ty == KM_TYPE_BYTES || ty == KM_TYPE_BIGNUM) {
                ok = CBB_add_asn1(&pseq, &pv, EXP(1)) &&
                     CBB_add_asn1(&pv, &pvi, CBS_ASN1_OCTETSTRING) &&
                     (pp->b.len == 0 || CBB_add_bytes(&pvi, pp->b.data, pp->b.len));
            } else {
                ok = CBB_add_asn1(&pseq, &pv, EXP(0)) &&
                     CBB_add_asn1(&pv, &pvi, CBS_ASN1_INTEGER) && put_int_content(&pvi, pp->i, 1);
            }
        }
        if (ok) ok = CBB_flush_asn1_set_of(&setof) && CBB_flush(&seq);
    }

    size_t der_len = 0;
    if (!ok || !CBB_finish(&cbb, NULL, &der_len)) { CBB_cleanup(&cbb); return -5; }
    *out_len = (uint32_t)der_len;
    return 0;
}

// Access CBS fields directly rather than via CBS_init/CBS_data/CBS_len: newer
// BoringSSL (Android 16) makes those static-inline and does not export them,
// while older headers declare them extern. The struct layout {data,len} is
// stable, so direct access links against any libcrypto version.

// Decode a big-endian INTEGER content (DER) into int64. A 9-byte value with a
// leading 0x00 is an unsigned 64-bit with the top bit set (e.g. an operation
// handle); strip the sign pad so it fits.
static int be_to_i64(const CBS *c, int64_t *out) {
    size_t n = c->len;
    const uint8_t *p = c->data;
    if (n == 9 && p[0] == 0) { p++; n = 8; }
    if (n == 0 || n > 8) return -1;
    int64_t v = (p[0] & 0x80) ? -1 : 0;  // sign-extend
    for (size_t i = 0; i < n; i++) v = (v << 8) | p[i];
    *out = v;
    return 0;
}

static int dup_octet(const CBS *c, uint8_t **buf, uint32_t *len) {
    size_t n = c->len;
    uint8_t *b = malloc(n ? n : 1);
    if (b == NULL) return -1;
    if (n) memcpy(b, c->data, n);
    *buf = b;
    *len = (uint32_t)n;
    return 0;
}

int km_outdata_decode(const uint8_t *der, uint32_t der_len, km_outdata *out) {
    memset(out, 0, sizeof(*out));
    CBS top, seq, child;
    int present;
    int64_t ver, cmd, pid, err;
    top.data = der;
    top.len = der_len;
    if (!CBS_get_asn1(&top, &seq, CBS_ASN1_SEQUENCE)) return -1;
    if (!CBS_get_asn1_int64(&seq, &ver) || !CBS_get_asn1_int64(&seq, &cmd) ||
        !CBS_get_asn1_int64(&seq, &pid) || !CBS_get_asn1_int64(&seq, &err))
        return -1;
    out->err = (int32_t)err;

    // Optional fields are EXPLICIT [n] (constructed) wrapping a universal element.
#define EXP(n) (CBS_ASN1_CONTEXT_SPECIFIC | CBS_ASN1_CONSTRUCTED | (unsigned)(n))
    CBS inner;
    if (!CBS_get_optional_asn1(&seq, &child, &present, EXP(0))) return -1;
    if (present) {
        if (!CBS_get_asn1(&child, &inner, CBS_ASN1_INTEGER) || be_to_i64(&inner, &out->int0))
            return -1;
        out->int0_set = true;
    }
    if (!CBS_get_optional_asn1(&seq, &child, &present, EXP(1))) return -1;
    if (present) {
        if (!CBS_get_asn1(&child, &inner, CBS_ASN1_INTEGER) || be_to_i64(&inner, &out->long0))
            return -1;
        out->long0_set = true;
    }
    if (!CBS_get_optional_asn1(&seq, &child, &present, EXP(2))) return -1;
    if (present) {
        if (!CBS_get_asn1(&child, &inner, CBS_ASN1_OCTETSTRING) ||
            dup_octet(&inner, &out->bin0, &out->bin0_len))
            return -1;
    }
    if (!CBS_get_optional_asn1(&seq, &child, &present, EXP(3))) return -1;
    if (present) {
        if (!CBS_get_asn1(&child, &inner, CBS_ASN1_OCTETSTRING) ||
            dup_octet(&inner, &out->bin1, &out->bin1_len))
            return -1;
    }
    // [4] par: EXPLICIT { SET OF KM_PARAM }. Decode tags + values into out->params.
    // Byte values point into `der` (caller keeps it alive). [5] log is ignored.
    if (!CBS_get_optional_asn1(&seq, &child, &present, EXP(4))) return -1;
    if (present) {
        CBS setof;
        if (!CBS_get_asn1(&child, &setof, CBS_ASN1_SET)) return -1;
        while (setof.len > 0 && out->n_params < KM_MAX_PARAMS) {
            CBS pseq, v, pvi;
            int64_t tag;
            if (!CBS_get_asn1(&setof, &pseq, CBS_ASN1_SEQUENCE) ||
                !CBS_get_asn1(&pseq, &pvi, CBS_ASN1_INTEGER) || be_to_i64(&pvi, &tag))
                return -1;
            km_param *pp = &out->params[out->n_params++];
            memset(pp, 0, sizeof(*pp));
            pp->tag = (uint32_t)tag;
            uint32_t ty = KM_TAG_TYPE(pp->tag);
            if (ty == KM_TYPE_BYTES || ty == KM_TYPE_BIGNUM) {
                if (!CBS_get_asn1(&pseq, &v, EXP(1)) || !CBS_get_asn1(&v, &pvi, CBS_ASN1_OCTETSTRING))
                    return -1;
                pp->b.data = pvi.data;
                pp->b.len = (uint32_t)pvi.len;
            } else {
                int64_t iv;
                if (!CBS_get_asn1(&pseq, &v, EXP(0)) || !CBS_get_asn1(&v, &pvi, CBS_ASN1_INTEGER) ||
                    be_to_i64(&pvi, &iv))
                    return -1;
                pp->i = (uint64_t)iv;
            }
        }
    }
    return 0;
#undef EXP
}

void km_outdata_release(km_outdata *out) {
    free(out->bin0);
    free(out->bin1);
    out->bin0 = out->bin1 = NULL;
    out->bin0_len = out->bin1_len = 0;
}
