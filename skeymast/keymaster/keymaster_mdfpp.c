// SPDX-License-Identifier: Apache-2.0
// OSS keymaster2 HAL for a9y18qlte: talks the skeymast KM_INDATA/KM_OUTDATA
// protocol to the TrustZone app via libskeymast_common. See keymaster/README.md.

#define LOG_TAG "keymaster_mdfpp_oss"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/system_properties.h>

#include <hardware/hardware.h>
#include <hardware/keymaster2.h>
#include <hardware/keymaster_defs.h>
#include <log/log.h>

#include "km_asn1.h"
#include "km_transport.h"

// skeymast keymaster command ids.
enum {
    KM_C_ADD_RNG_ENTROPY = 0x01,
    KM_C_GENERATE_KEY = 0x02,
    KM_C_GET_KEY_CHARACTERISTICS = 0x03,
    KM_C_IMPORT_KEY = 0x04,
    KM_C_EXPORT_KEY = 0x05,
    KM_C_BEGIN = 0x08,
    KM_C_UPDATE = 0x09,
    KM_C_FINISH = 0x0a,
    KM_C_ABORT = 0x0b,
    KM_C_ATTEST_KEY = 0x0e,
    KM_C_CONFIGURE = 0x0f,
    KM_C_UPGRADE_KEY = 0x10,
};

// Timestamp params the stock pack injects (epoch ms, KM_DATE): CREATION_DATETIME
// on generate/import, a per-op tag on begin (begin fails UNKNOWN_ERROR without it).
#define KM_TAG_CREATION_DATETIME_T 0x600002bdu
#define KM_TAG_BEGIN_TIMESTAMP     0x60000320u

static uint64_t now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

// Append {tag, now_ms()} unless `tag` is already present.
static uint32_t inject_ts(km_param *p, uint32_t n, uint32_t max, uint32_t tag) {
    for (uint32_t k = 0; k < n; k++) if (p[k].tag == tag) return n;
    if (n >= max) return n;
    p[n].tag = tag; p[n].i = now_ms(); p[n].b.data = NULL; p[n].b.len = 0;
    return n + 1;
}

// OS version = major*10000+minor*100+sub from ro.build.version.release.
static uint32_t os_version(void) {
    char v[PROP_VALUE_MAX] = {0};
    if (__system_property_get("ro.build.version.release", v) < 1) return 0;
    unsigned a = 0, b = 0, c = 0;
    sscanf(v, "%u.%u.%u", &a, &b, &c);
    if (a > 99) a = 99;
    if (b > 99) b = 99;
    if (c > 99) c = 99;
    return a * 10000 + b * 100 + c;
}

// OS patchlevel = year*100+month from ro.build.version.security_patch.
static uint32_t os_patchlevel(void) {
    char v[PROP_VALUE_MAX] = {0};
    if (__system_property_get("ro.build.version.security_patch", v) < 1) return 0;
    unsigned y = 0, m = 0;
    if (sscanf(v, "%u-%u", &y, &m) != 2) return 0;
    return y * 100 + m;
}

// keymaster_key_param_set_t -> km_param[]. Does NOT add OS version/patchlevel:
// the TA binds those itself (via configure) and rejects client-supplied ones.
static uint32_t to_km_params(const keymaster_key_param_set_t *in, km_param *out, uint32_t max) {
    uint32_t n = 0;
    if (!in) return 0;
    for (size_t k = 0; k < in->length && n < max; k++) {
        const keymaster_key_param_t *p = &in->params[k];
        uint32_t ty = p->tag & 0xf0000000u;
        out[n].tag = p->tag;
        out[n].i = 0;
        out[n].b.data = NULL;
        out[n].b.len = 0;
        if (ty == KM_BYTES || ty == KM_BIGNUM) {
            out[n].b.data = p->blob.data;
            out[n].b.len = (uint32_t)p->blob.data_length;
        } else if (ty == KM_BOOL) {
            out[n].i = p->boolean ? 1 : 0;
        } else if (ty == KM_ULONG || ty == KM_ULONG_REP || ty == KM_DATE) {
            out[n].i = p->long_integer;  // union: date_time aliases this
        } else {
            out[n].i = p->integer;       // union: enumerated aliases this
        }
        n++;
    }
    return n;
}

// km_param[] (decoded response) -> heap keymaster_key_param_set_t.
static int from_km_params(const km_param *in, uint32_t n, keymaster_key_param_set_t *out) {
    out->params = NULL;
    out->length = 0;
    if (n == 0) return 0;
    keymaster_key_param_t *arr = calloc(n, sizeof(*arr));
    if (!arr) return -1;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t ty = in[k].tag & 0xf0000000u;
        arr[k].tag = (keymaster_tag_t)in[k].tag;
        if (ty == KM_BYTES || ty == KM_BIGNUM) {
            uint8_t *d = NULL;
            if (in[k].b.len) {
                d = malloc(in[k].b.len);
                if (!d) { /* best effort */ }
                else memcpy(d, in[k].b.data, in[k].b.len);
            }
            arr[k].blob.data = d;
            arr[k].blob.data_length = in[k].b.len;
        } else if (ty == KM_ULONG || ty == KM_ULONG_REP || ty == KM_DATE) {
            arr[k].long_integer = in[k].i;
        } else if (ty == KM_BOOL) {
            arr[k].boolean = in[k].i != 0;
        } else {
            arr[k].integer = (uint32_t)in[k].i;
        }
    }
    out->params = arr;
    out->length = n;
    return 0;
}

static int dup_blob(const uint8_t *src, uint32_t len, const uint8_t **dst, size_t *dlen) {
    uint8_t *d = malloc(len ? len : 1);
    if (!d) return -1;
    if (len) memcpy(d, src, len);
    *dst = d;
    *dlen = len;
    return 0;
}

// Fill common request header.
static void req_init(km_indata *in, uint32_t cmd) {
    memset(in, 0, sizeof(*in));
    in->cmd = cmd;
    in->pid = (uint32_t)getpid();
}


// FIXME(configure): the keymaster@3.0 wrapper calls this at device creation and
// rejects the whole device if it is not KM_ERROR_OK, and the TA refuses every op
// with KEYMINT_NOT_CONFIGURED (-64) until a configure succeeds. Live-tested
// 2026-10-22: this OS_VERSION/OS_PATCHLEVEL payload is rejected -38
// (KM_ERROR_INVALID_ARGUMENT) for every value tried (0, 16.x, 10, 8), while the
// stock blob's configure (same wrapper, same OS params) succeeds -- so stock
// forwards a different payload to the TA. The correct configure wire format
// still needs RE, and the transport must hold one TA session open (per-op
// open/close drops the configured state). See gatekeeper-keymaster-re memory.
static keymaster_error_t km_configure(const keymaster2_device_t *dev __unused,
                                       const keymaster_key_param_set_t *params) {
    (void)params;  // stock derives version/patchlevel itself; we send computed ones
    km_param p[2];
    p[0].tag = KM_TAG_OS_VERSION;    p[0].i = os_version();    p[0].b.data = NULL; p[0].b.len = 0;
    p[1].tag = KM_TAG_OS_PATCHLEVEL; p[1].i = os_patchlevel(); p[1].b.data = NULL; p[1].b.len = 0;

    if (km_tz_open() != 0) return KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    km_indata in; req_init(&in, KM_C_CONFIGURE);
    in.params = p; in.n_params = 2;
    km_outdata out;
    int rc = km_tz_transact(&in, &out);
    keymaster_error_t e = (rc == 0) ? (keymaster_error_t)out.err : KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    if (rc == 0) km_outdata_release(&out);
    km_tz_close();
    return e;
}

static keymaster_error_t km_add_rng_entropy(const keymaster2_device_t *dev __unused,
                                            const uint8_t *data, size_t data_length) {
    if (data_length == 0) return KM_ERROR_OK;
    if (data == NULL) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    if (km_tz_open() != 0) return KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    km_indata in; req_init(&in, KM_C_ADD_RNG_ENTROPY);
    in.bin0_set = true; in.bin0.data = data; in.bin0.len = (uint32_t)data_length;
    km_outdata out;
    int rc = km_tz_transact(&in, &out);
    keymaster_error_t e = (rc == 0) ? (keymaster_error_t)out.err : KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    if (rc == 0) km_outdata_release(&out);
    km_tz_close();
    return e;
}

static void split_characteristics(const km_outdata *out,
                                  keymaster_key_characteristics_t *chars) {
    // The TA returns one param set; expose it as hw_enforced (sw_enforced empty).
    // A refinement would split by KM_TAG hardware/software rules.
    memset(chars, 0, sizeof(*chars));
    if (chars) from_km_params(out->params, out->n_params, &chars->hw_enforced);
}

static keymaster_error_t km_generate_key(const keymaster2_device_t *dev __unused,
                                         const keymaster_key_param_set_t *params,
                                         keymaster_key_blob_t *key_blob,
                                         keymaster_key_characteristics_t *characteristics) {
    if (params == NULL || key_blob == NULL) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    km_param p[KM_MAX_PARAMS];
    uint32_t n = to_km_params(params, p, KM_MAX_PARAMS);
    n = inject_ts(p, n, KM_MAX_PARAMS, KM_TAG_CREATION_DATETIME_T);

    if (km_tz_open() != 0) return KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    km_indata in; req_init(&in, KM_C_GENERATE_KEY);
    in.params = p; in.n_params = n;
    km_outdata out;
    int rc = km_tz_transact(&in, &out);
    keymaster_error_t e = KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    if (rc == 0) {
        e = (keymaster_error_t)out.err;
        if (e == KM_ERROR_OK && out.bin1) {
            if (dup_blob(out.bin1, out.bin1_len, &key_blob->key_material,
                         &key_blob->key_material_size) != 0)
                e = KM_ERROR_MEMORY_ALLOCATION_FAILED;
            else if (characteristics)
                split_characteristics(&out, characteristics);
        } else if (e == KM_ERROR_OK) {
            e = KM_ERROR_UNKNOWN_ERROR;  // ok but no blob
        }
        km_outdata_release(&out);
    }
    km_tz_close();
    return e;
}

static keymaster_error_t km_get_key_characteristics(
        const keymaster2_device_t *dev __unused, const keymaster_key_blob_t *key_blob,
        const keymaster_blob_t *client_id, const keymaster_blob_t *app_data,
        keymaster_key_characteristics_t *characteristics) {
    if (key_blob == NULL || characteristics == NULL) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    if (km_tz_open() != 0) return KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    km_indata in; req_init(&in, KM_C_GET_KEY_CHARACTERISTICS);
    in.key_set = true;  in.key.data = key_blob->key_material; in.key.len = (uint32_t)key_blob->key_material_size;
    in.bin0_set = true; if (client_id) { in.bin0.data = client_id->data; in.bin0.len = (uint32_t)client_id->data_length; }
    in.bin1_set = true; if (app_data) { in.bin1.data = app_data->data; in.bin1.len = (uint32_t)app_data->data_length; }
    km_outdata out;
    int rc = km_tz_transact(&in, &out);
    keymaster_error_t e = KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    if (rc == 0) {
        e = (keymaster_error_t)out.err;
        if (e == KM_ERROR_OK) split_characteristics(&out, characteristics);
        km_outdata_release(&out);
    }
    km_tz_close();
    return e;
}

static keymaster_error_t km_begin(const keymaster2_device_t *dev __unused,
                                  keymaster_purpose_t purpose, const keymaster_key_blob_t *key,
                                  const keymaster_key_param_set_t *in_params,
                                  keymaster_key_param_set_t *out_params,
                                  keymaster_operation_handle_t *operation_handle) {
    if (key == NULL || operation_handle == NULL) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    km_param p[KM_MAX_PARAMS];
    uint32_t n = to_km_params(in_params, p, KM_MAX_PARAMS);
    n = inject_ts(p, n, KM_MAX_PARAMS, KM_TAG_BEGIN_TIMESTAMP);

    if (km_tz_open() != 0) return KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    km_indata in; req_init(&in, KM_C_BEGIN);
    in.int0_set = true; in.int0 = (uint32_t)purpose;
    in.key_set = true;  in.key.data = key->key_material; in.key.len = (uint32_t)key->key_material_size;
    in.params = p; in.n_params = n;
    km_outdata out;
    int rc = km_tz_transact(&in, &out);
    keymaster_error_t e = KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    if (rc == 0) {
        e = (keymaster_error_t)out.err;
        if (e == KM_ERROR_OK) {
            *operation_handle = out.long0_set ? (keymaster_operation_handle_t)out.long0 : 0;
            if (out_params) from_km_params(out.params, out.n_params, out_params);
        }
        km_outdata_release(&out);
    }
    km_tz_close();
    return e;
}

static keymaster_error_t km_update(const keymaster2_device_t *dev __unused,
                                   keymaster_operation_handle_t operation_handle,
                                   const keymaster_key_param_set_t *in_params,
                                   const keymaster_blob_t *input, size_t *input_consumed,
                                   keymaster_key_param_set_t *out_params,
                                   keymaster_blob_t *output) {
    if (input_consumed == NULL || output == NULL) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    km_param p[KM_MAX_PARAMS];
    uint32_t n = to_km_params(in_params, p, KM_MAX_PARAMS);

    if (km_tz_open() != 0) return KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    km_indata in; req_init(&in, KM_C_UPDATE);
    in.long0_set = true; in.long0 = (uint64_t)operation_handle;
    in.bin0_set = true;  if (input) { in.bin0.data = input->data; in.bin0.len = (uint32_t)input->data_length; }
    in.params = p; in.n_params = n;
    km_outdata out;
    int rc = km_tz_transact(&in, &out);
    keymaster_error_t e = KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    if (rc == 0) {
        e = (keymaster_error_t)out.err;
        if (e == KM_ERROR_OK) {
            *input_consumed = out.int0_set ? (size_t)out.int0 : (input ? input->data_length : 0);
            output->data = NULL; output->data_length = 0;
            if (out.bin0) dup_blob(out.bin0, out.bin0_len, &output->data, &output->data_length);
            if (out_params) from_km_params(out.params, out.n_params, out_params);
        }
        km_outdata_release(&out);
    }
    km_tz_close();
    return e;
}

static keymaster_error_t km_finish(const keymaster2_device_t *dev __unused,
                                   keymaster_operation_handle_t operation_handle,
                                   const keymaster_key_param_set_t *in_params,
                                   const keymaster_blob_t *input, const keymaster_blob_t *signature,
                                   keymaster_key_param_set_t *out_params,
                                   keymaster_blob_t *output) {
    if (output == NULL) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    km_param p[KM_MAX_PARAMS];
    uint32_t n = to_km_params(in_params, p, KM_MAX_PARAMS);

    if (km_tz_open() != 0) return KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    km_indata in; req_init(&in, KM_C_FINISH);
    in.long0_set = true; in.long0 = (uint64_t)operation_handle;
    in.bin0_set = true;  if (input) { in.bin0.data = input->data; in.bin0.len = (uint32_t)input->data_length; }
    in.bin1_set = true;  if (signature) { in.bin1.data = signature->data; in.bin1.len = (uint32_t)signature->data_length; }
    in.params = p; in.n_params = n;
    km_outdata out;
    int rc = km_tz_transact(&in, &out);
    keymaster_error_t e = KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    if (rc == 0) {
        e = (keymaster_error_t)out.err;
        if (e == KM_ERROR_OK) {
            output->data = NULL; output->data_length = 0;
            if (out.bin0) dup_blob(out.bin0, out.bin0_len, &output->data, &output->data_length);
            if (out_params) from_km_params(out.params, out.n_params, out_params);
        }
        km_outdata_release(&out);
    }
    km_tz_close();
    return e;
}

static keymaster_error_t km_abort(const keymaster2_device_t *dev __unused,
                                  keymaster_operation_handle_t operation_handle) {
    if (km_tz_open() != 0) return KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    km_indata in; req_init(&in, KM_C_ABORT);
    in.long0_set = true; in.long0 = (uint64_t)operation_handle;
    km_outdata out;
    int rc = km_tz_transact(&in, &out);
    keymaster_error_t e = (rc == 0) ? (keymaster_error_t)out.err : KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    if (rc == 0) km_outdata_release(&out);
    km_tz_close();
    return e;
}

// import_key 0x04: int0=key_format, bin0=key material, par=params (+creation
// datetime, as generate). OUT bin1=key blob, par=characteristics.
static keymaster_error_t km_import_key(const keymaster2_device_t *dev __unused,
                                       const keymaster_key_param_set_t *params,
                                       keymaster_key_format_t key_format,
                                       const keymaster_blob_t *key_data,
                                       keymaster_key_blob_t *key_blob,
                                       keymaster_key_characteristics_t *characteristics) {
    if (params == NULL || key_data == NULL || key_blob == NULL) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    km_param p[KM_MAX_PARAMS];
    uint32_t n = to_km_params(params, p, KM_MAX_PARAMS);
    n = inject_ts(p, n, KM_MAX_PARAMS, KM_TAG_CREATION_DATETIME_T);

    if (km_tz_open() != 0) return KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    km_indata in; req_init(&in, KM_C_IMPORT_KEY);
    in.int0_set = true; in.int0 = (uint32_t)key_format;
    in.bin0_set = true; in.bin0.data = key_data->data; in.bin0.len = (uint32_t)key_data->data_length;
    in.params = p; in.n_params = n;
    km_outdata out;
    int rc = km_tz_transact(&in, &out);
    keymaster_error_t e = KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    if (rc == 0) {
        e = (keymaster_error_t)out.err;
        if (e == KM_ERROR_OK && out.bin1) {
            if (dup_blob(out.bin1, out.bin1_len, &key_blob->key_material,
                         &key_blob->key_material_size) != 0)
                e = KM_ERROR_MEMORY_ALLOCATION_FAILED;
            else if (characteristics)
                split_characteristics(&out, characteristics);
        } else if (e == KM_ERROR_OK) {
            e = KM_ERROR_UNKNOWN_ERROR;
        }
        km_outdata_release(&out);
    }
    km_tz_close();
    return e;
}

// export_key 0x05: int0=format, key=blob, bin0=client_id, bin1=app_data.
// OUT bin0=exported key material (e.g. X.509 public key).
static keymaster_error_t km_export_key(const keymaster2_device_t *dev __unused,
                                       keymaster_key_format_t export_format,
                                       const keymaster_key_blob_t *key_to_export,
                                       const keymaster_blob_t *client_id,
                                       const keymaster_blob_t *app_data,
                                       keymaster_blob_t *export_data) {
    if (key_to_export == NULL || export_data == NULL) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    if (km_tz_open() != 0) return KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    km_indata in; req_init(&in, KM_C_EXPORT_KEY);
    in.int0_set = true; in.int0 = (uint32_t)export_format;
    in.key_set = true;  in.key.data = key_to_export->key_material; in.key.len = (uint32_t)key_to_export->key_material_size;
    in.bin0_set = true; if (client_id) { in.bin0.data = client_id->data; in.bin0.len = (uint32_t)client_id->data_length; }
    in.bin1_set = true; if (app_data) { in.bin1.data = app_data->data; in.bin1.len = (uint32_t)app_data->data_length; }
    km_outdata out;
    int rc = km_tz_transact(&in, &out);
    keymaster_error_t e = KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    if (rc == 0) {
        e = (keymaster_error_t)out.err;
        if (e == KM_ERROR_OK) {
            export_data->data = NULL; export_data->data_length = 0;
            if (out.bin0) dup_blob(out.bin0, out.bin0_len, &export_data->data, &export_data->data_length);
        }
        km_outdata_release(&out);
    }
    km_tz_close();
    return e;
}

// upgrade_key 0x10: int0=1, key=blob, par=upgrade params. OUT bin1=upgraded blob.
static keymaster_error_t km_upgrade_key(const keymaster2_device_t *dev __unused,
                                        const keymaster_key_blob_t *key_to_upgrade,
                                        const keymaster_key_param_set_t *upgrade_params,
                                        keymaster_key_blob_t *upgraded_key) {
    if (key_to_upgrade == NULL || upgraded_key == NULL) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    km_param p[KM_MAX_PARAMS];
    uint32_t n = to_km_params(upgrade_params, p, KM_MAX_PARAMS);

    if (km_tz_open() != 0) return KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    km_indata in; req_init(&in, KM_C_UPGRADE_KEY);
    in.int0_set = true; in.int0 = 1;  // stock passes constant 1
    in.key_set = true;  in.key.data = key_to_upgrade->key_material; in.key.len = (uint32_t)key_to_upgrade->key_material_size;
    in.params = p; in.n_params = n;
    km_outdata out;
    int rc = km_tz_transact(&in, &out);
    keymaster_error_t e = KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    if (rc == 0) {
        e = (keymaster_error_t)out.err;
        if (e == KM_ERROR_OK && out.bin1) {
            if (dup_blob(out.bin1, out.bin1_len, &upgraded_key->key_material,
                         &upgraded_key->key_material_size) != 0)
                e = KM_ERROR_MEMORY_ALLOCATION_FAILED;
        } else if (e == KM_ERROR_OK) {
            e = KM_ERROR_UNKNOWN_ERROR;
        }
        km_outdata_release(&out);
    }
    km_tz_close();
    return e;
}

// attest_key: inject the /efs/DAK provisioned keys + issuer certs as params; the
// TA signs the leaf (bin0), and we append the matching CA chain. See README.
static int read_dak(const char *base, uint8_t **buf, uint32_t *len) {
    char path[160];
    FILE *f = NULL;
    snprintf(path, sizeof(path), "/efs/DAK/%s", base);
    f = fopen(path, "rb");
    if (!f) {
        snprintf(path, sizeof(path), "/mnt/vendor/efs/DAK/%s", base);
        f = fopen(path, "rb");
    }
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 0x4000) { fclose(f); return -1; }
    uint8_t *b = malloc((size_t)n);
    if (!b) { fclose(f); return -1; }
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(b); return -1; }
    *buf = b;
    *len = (uint32_t)n;
    return 0;
}

// Read a /efs/DAK file and add it as a KM_BYTES param (buffer kept in bufs[]).
static void add_dak_param(km_param *p, uint32_t *n, uint32_t tag, const char *file,
                          uint8_t **bufs, uint32_t *nb) {
    uint8_t *b = NULL; uint32_t l = 0;
    if (*n >= KM_MAX_PARAMS || read_dak(file, &b, &l) != 0) return;
    p[*n].tag = tag; p[*n].i = 0; p[*n].b.data = b; p[*n].b.len = l;
    (*n)++;
    bufs[(*nb)++] = b;
}

static keymaster_error_t km_attest_key(const keymaster2_device_t *dev __unused,
                                       const keymaster_key_blob_t *key_to_attest,
                                       const keymaster_key_param_set_t *attest_params,
                                       keymaster_cert_chain_t *cert_chain) {
    if (key_to_attest == NULL || attest_params == NULL || cert_chain == NULL)
        return KM_ERROR_UNEXPECTED_NULL_POINTER;
    cert_chain->entries = NULL;
    cert_chain->entry_count = 0;

    km_param p[KM_MAX_PARAMS];
    uint32_t n = to_km_params(attest_params, p, KM_MAX_PARAMS);
    // Provisioned material (tags from RE): each key's .private whole + its issuer
    // cert (certN0.der). The TA picks the set matching the attested key's algo.
    uint8_t *bufs[6]; uint32_t nb = 0;
    add_dak_param(p, &n, 0x9000139d, "GAK_RSA.private", bufs, &nb);
    add_dak_param(p, &n, 0x9000139f, "gakrsacert0.der", bufs, &nb);
    add_dak_param(p, &n, 0x9000139e, "GAK_EC.private",  bufs, &nb);
    add_dak_param(p, &n, 0x900013a0, "gakeccert0.der",  bufs, &nb);
    add_dak_param(p, &n, 0x900013a2, "SAK_EC.private",  bufs, &nb);
    add_dak_param(p, &n, 0x900013a4, "sakeccert0.der",  bufs, &nb);

    keymaster_error_t e = KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
    km_outdata out;
    if (km_tz_open() == 0) {
        km_indata in; req_init(&in, KM_C_ATTEST_KEY);
        in.key_set = true; in.key.data = key_to_attest->key_material; in.key.len = (uint32_t)key_to_attest->key_material_size;
        in.params = p; in.n_params = n;
        int rc = km_tz_transact(&in, &out);
        if (rc == 0) {
            e = (keymaster_error_t)out.err;
            if (e == KM_ERROR_OK && out.bin0 && out.bin0_len) {
                // Chain = leaf (TA) + provisioned CA certs, as individual DER
                // files. int0 = algo (1=RSA, 3=EC); long0 group selects GAK/SAK.
                static const char *ec_gak[] = {"gakeccert0.der", "gakeccert1.der", "gakecroot.der"};
                static const char *ec_sak[] = {"sakeccert0.der", "sakeccert1.der"};
                static const char *rsa_gak[] = {"gakrsacert0.der", "gakrsacert1.der", "gakrsaroot.der"};
                const char **ca; uint32_t nca;
                if (out.int0 == 1) { ca = rsa_gak; nca = 3; }
                else if (out.long0_set && out.long0 == 0) { ca = ec_sak; nca = 2; }
                else { ca = ec_gak; nca = 3; }

                keymaster_blob_t *ents = calloc(1 + nca, sizeof(*ents));
                if (ents) {
                    uint8_t *leaf = malloc(out.bin0_len);
                    if (leaf) { memcpy(leaf, out.bin0, out.bin0_len); ents[0].data = leaf; ents[0].data_length = out.bin0_len; }
                    uint32_t idx = 1;
                    for (uint32_t k = 0; k < nca; k++) {
                        uint8_t *c = NULL; uint32_t cl = 0;
                        if (read_dak(ca[k], &c, &cl) == 0) { ents[idx].data = c; ents[idx].data_length = cl; idx++; }
                    }
                    cert_chain->entries = ents;
                    cert_chain->entry_count = idx;
                } else {
                    e = KM_ERROR_MEMORY_ALLOCATION_FAILED;
                }
            } else if (e == KM_ERROR_OK) {
                e = KM_ERROR_UNKNOWN_ERROR;
            }
            km_outdata_release(&out);
        }
        km_tz_close();
    }
    for (uint32_t i = 0; i < nb; i++) free(bufs[i]);
    return e;
}

// delete_key / delete_all_keys: stock returns UNIMPLEMENTED (blob-based keys).
static keymaster_error_t km_delete_key(const keymaster2_device_t *dev __unused,
                                       const keymaster_key_blob_t *key __unused) {
    return KM_ERROR_UNIMPLEMENTED;
}
static keymaster_error_t km_delete_all_keys(const keymaster2_device_t *dev __unused) {
    return KM_ERROR_UNIMPLEMENTED;
}

static int km_close(hw_device_t *dev) {
    free(dev);
    return 0;
}

static int km_open(const hw_module_t *module, const char *name, hw_device_t **dev) {
    if (module == NULL || name == NULL || dev == NULL) return -EINVAL;
    if (strcmp(name, KEYSTORE_KEYMASTER) != 0) return -EINVAL;

    keymaster2_device_t *km = calloc(1, sizeof(*km));
    if (km == NULL) return -ENOMEM;
    km->common.tag = HARDWARE_DEVICE_TAG;
    km->common.version = KEYMASTER_DEVICE_API_VERSION_2_0;
    km->common.module = (hw_module_t *)module;
    km->common.close = km_close;
    km->flags = KEYMASTER_SUPPORTS_EC;
    km->configure = km_configure;
    km->add_rng_entropy = km_add_rng_entropy;
    km->generate_key = km_generate_key;
    km->get_key_characteristics = km_get_key_characteristics;
    km->begin = km_begin;
    km->update = km_update;
    km->finish = km_finish;
    km->abort = km_abort;
    km->delete_key = km_delete_key;
    km->delete_all_keys = km_delete_all_keys;
    km->import_key = km_import_key;
    km->export_key = km_export_key;
    km->upgrade_key = km_upgrade_key;
    km->attest_key = km_attest_key;
    // (attest_key reads /efs/DAK provisioned keys/certs and appends the CA chain)
    *dev = &km->common;
    return 0;
}

static struct hw_module_methods_t km_methods = { .open = km_open };

// .name matches the stock keystore.mdfpp.so exactly ("keymaster_mdfpp_module").
struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .module_api_version = KEYMASTER_MODULE_API_VERSION_2_0,
    .hal_api_version = HARDWARE_HAL_API_VERSION,
    .id = KEYSTORE_HARDWARE_MODULE_ID,
    .name = "keymaster_mdfpp_module",
    .author = "LineageOS a9y18qlte",
    .methods = &km_methods,
};
