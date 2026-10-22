// SPDX-License-Identifier: Apache-2.0
// OSS transport to the Samsung skeymast trusted app over QSEECom.
// Reimplements nwd_tz_open / nwd_tz_run_cmd / pack / unpack from the stock
// gatekeeper.mdfpp.so (see ~/gatekeeper-re/PLAN.md).
#pragma once

#include "km_asn1.h"

#ifdef __cplusplus
extern "C" {
#endif

int km_tz_open(void);
int km_tz_close(void);

// Encode `in` to DER into the shared in-buffer, invoke the TA, decode the
// response into *out (caller calls km_outdata_release). `in->cmd`/`pid` must be
// set. Returns 0 on a completed round-trip; TA-level result is in out->err.
int km_tz_transact(const km_indata *in, km_outdata *out);

#ifdef __cplusplus
}
#endif
