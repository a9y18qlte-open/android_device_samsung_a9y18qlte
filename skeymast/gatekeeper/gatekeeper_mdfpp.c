// SPDX-License-Identifier: Apache-2.0
// OSS gatekeeper hw_module for the Samsung skeymast TA (a9y18qlte): enroll=0x201,
// verify=0x202, AOSP 58-byte handle / 69-byte token. See gatekeeper/README.md.

#define LOG_TAG "gatekeeper_mdfpp_oss"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <hardware/gatekeeper.h>
#include <hardware/hardware.h>
#include <log/log.h>

#include "km_asn1.h"
#include "km_transport.h"

#define PASSWORD_HANDLE_LEN 0x3a  // 58, AOSP password_handle_t
#define AUTH_TOKEN_LEN      0x45  // 69, AOSP hw_auth_token_t

static int gk_enroll(const struct gatekeeper_device *dev __unused, uint32_t uid,
                     const uint8_t *cur_hash, uint32_t cur_hash_len,
                     const uint8_t *cur_pwd, uint32_t cur_pwd_len,
                     const uint8_t *des_pwd, uint32_t des_pwd_len,
                     uint8_t **handle, uint32_t *handle_len) {
    if (des_pwd == NULL || des_pwd_len == 0 || handle == NULL || handle_len == NULL)
        return -EINVAL;

    int ret = -1;
    km_outdata out;
    memset(&out, 0, sizeof(out));
    if (km_tz_open() != 0) return -1;

    // INDATA: int0=uid, key[4]=current handle, bin0[2]=current pwd, bin1[3]=desired
    // pwd. Stock always sends all four (empty octet when absent), so always set.
    km_indata in;
    memset(&in, 0, sizeof(in));
    in.cmd = KM_CMD_ENROLL;
    in.pid = (uint32_t)getpid();
    in.int0_set = true;
    in.int0 = uid;
    in.key_set = true;
    in.key.data = cur_hash;
    in.key.len = cur_hash ? cur_hash_len : 0;
    in.bin0_set = true;
    in.bin0.data = cur_pwd;
    in.bin0.len = cur_pwd ? cur_pwd_len : 0;
    in.bin1_set = true;
    in.bin1.data = des_pwd;
    in.bin1.len = des_pwd_len;

    if (km_tz_transact(&in, &out) != 0) goto done;
    if (out.err != 0) { ALOGE("enroll TA err %d", out.err); ret = -EINVAL; goto done; }

    // OUTDATA bin0 = new handle; stock asserts exactly 58 bytes.
    if (out.bin0 == NULL || out.bin0_len != PASSWORD_HANDLE_LEN) {
        ALOGE("enroll bad handle len %u", out.bin0_len);
        ret = -EINVAL;
        goto done;
    }
    *handle = malloc(PASSWORD_HANDLE_LEN);
    if (*handle == NULL) { ret = -ENOMEM; goto done; }
    memcpy(*handle, out.bin0, PASSWORD_HANDLE_LEN);
    *handle_len = PASSWORD_HANDLE_LEN;
    ret = 0;

done:
    km_outdata_release(&out);
    km_tz_close();
    return ret;
}

static int gk_verify(const struct gatekeeper_device *dev __unused, uint32_t uid,
                     uint64_t challenge,
                     const uint8_t *enrolled, uint32_t enrolled_len,
                     const uint8_t *provided, uint32_t provided_len,
                     uint8_t **auth_token, uint32_t *auth_token_len,
                     bool *request_reenroll) {
    if (enrolled == NULL || enrolled_len == 0 || provided == NULL || provided_len == 0 ||
        auth_token == NULL || auth_token_len == NULL || request_reenroll == NULL)
        return -EINVAL;

    int ret = -1;
    km_outdata out;
    memset(&out, 0, sizeof(out));
    if (km_tz_open() != 0) return -1;

    // INDATA: int0=uid, long0=challenge, key[4]=enrolled handle, bin0[2]=provided pwd.
    km_indata in;
    memset(&in, 0, sizeof(in));
    in.cmd = KM_CMD_VERIFY;
    in.pid = (uint32_t)getpid();
    in.int0_set = true;
    in.int0 = uid;
    in.long0_set = true;
    in.long0 = challenge;
    in.key_set = true;
    in.key.data = enrolled;
    in.key.len = enrolled_len;
    in.bin0_set = true;
    in.bin0.data = provided;
    in.bin0.len = provided_len;

    if (km_tz_transact(&in, &out) != 0) goto done;

    // Throttle path: err -0x25b or -300 => tell gatekeeperd to retry later.
    // long0 carries the timeout; return it as a positive value.
    if (out.err == -0x25b || out.err == -300) {
        ret = (int)(out.long0_set && out.long0 > 0 ? out.long0 : 1);
        goto done;
    }
    if (out.err != 0) { ALOGE("verify TA err %d", out.err); ret = -EINVAL; goto done; }

    // OUTDATA bin0 = auth token; stock asserts exactly 69 bytes.
    if (out.bin0 == NULL || out.bin0_len != AUTH_TOKEN_LEN) {
        ALOGE("verify bad token len %u", out.bin0_len);
        ret = -EINVAL;
        goto done;
    }
    *auth_token = malloc(AUTH_TOKEN_LEN);
    if (*auth_token == NULL) { ret = -ENOMEM; goto done; }
    memcpy(*auth_token, out.bin0, AUTH_TOKEN_LEN);
    *auth_token_len = AUTH_TOKEN_LEN;
    // int0 != 0 means the TA wants a re-enroll (matches stock field read).
    *request_reenroll = out.int0_set ? (out.int0 != 0) : false;
    ret = 0;

done:
    km_outdata_release(&out);
    km_tz_close();
    return ret;
}

static int gk_close(hw_device_t *dev) {
    free(dev);
    return 0;
}

static int gk_open(const hw_module_t *module, const char *name, hw_device_t **dev) {
    if (module == NULL || name == NULL || dev == NULL) return -EINVAL;
    if (strcmp(name, HARDWARE_GATEKEEPER) != 0) return -EINVAL;

    gatekeeper_device_t *gk = calloc(1, sizeof(*gk));
    if (gk == NULL) return -ENOMEM;
    gk->common.tag = HARDWARE_DEVICE_TAG;
    gk->common.version = 1;
    gk->common.module = (hw_module_t *)module;
    gk->common.close = gk_close;
    gk->enroll = gk_enroll;
    gk->verify = gk_verify;
    *dev = &gk->common;
    return 0;
}

static struct hw_module_methods_t gk_methods = { .open = gk_open };

// .name matches the stock gatekeeper.mdfpp.so exactly ("gatekeeper") so any
// name-based probe sees no difference.
struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .module_api_version = GATEKEEPER_MODULE_API_VERSION_0_1,
    .hal_api_version = HARDWARE_HAL_API_VERSION,
    .id = GATEKEEPER_HARDWARE_MODULE_ID,
    .name = "gatekeeper",
    .author = "LineageOS a9y18qlte",
    .methods = &gk_methods,
};
