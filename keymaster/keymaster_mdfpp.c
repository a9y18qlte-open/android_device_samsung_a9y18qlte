// SPDX-License-Identifier: Apache-2.0
// OSS MDFPP keymaster (keymaster2) module for a9y18qlte. Speaks the skeymast
// KM_INDATA/KM_OUTDATA protocol over QSEECom (shared libskeymast_common codec),
// replacing the closed libkeymaster2_mdfpp.so glue. The skeymast TrustZone app
// is unchanged.
//
// Command ids and per-field mapping are from RE of libkeymaster_helper_vendor.so
// (see project notes). Verified live: add_rng_entropy, generate_key (+ decoded
// characteristics). The operation ops (begin/update/finish) use the TA operation
// handle carried in long0.
//
// TEST ONLY until A/B-compared with stock: a wrong key-blob format bricks keys.

#define LOG_TAG "keymaster_mdfpp_oss"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
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

// ---- OS version / patchlevel (must match stock, or key blobs won't verify) ----
// version = major*10000 + minor*100 + subminor, from ro.build.version.release.
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

// patchlevel = year*100 + month, from ro.build.version.security_patch (YYYY-MM-DD).
static uint32_t os_patchlevel(void) {
    char v[PROP_VALUE_MAX] = {0};
    if (__system_property_get("ro.build.version.security_patch", v) < 1) return 0;
    unsigned y = 0, m = 0;
    if (sscanf(v, "%u-%u", &y, &m) != 2) return 0;
    return y * 100 + m;
}

// ---- param conversion -------------------------------------------------------
// keymaster_key_param_set_t -> km_param[]. OS version/patchlevel are NOT added
// here: the skeymast TA binds them itself from its configured state (set via the
// configure command) and REJECTS client-supplied OS_VERSION/OS_PATCHLEVEL at
// generate_key (verified: injecting them returns KM_ERROR_UNKNOWN_ERROR).
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

// ---- keymaster2_device ops --------------------------------------------------

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
    // import_key/export_key/upgrade_key/attest_key: not wired yet (need the same
    // A/B byte capture as generate_key for their exact field use). Left NULL so
    // keystore reports them unavailable rather than misbehaving.
    *dev = &km->common;
    return 0;
}

static struct hw_module_methods_t km_methods = { .open = km_open };

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .module_api_version = KEYMASTER_MODULE_API_VERSION_2_0,
    .hal_api_version = HARDWARE_HAL_API_VERSION,
    .id = KEYSTORE_HARDWARE_MODULE_ID,
    .name = "MDFPP Keymaster OSS",
    .author = "LineageOS a9y18qlte",
    .methods = &km_methods,
};
