// SPDX-License-Identifier: BSD-3-Clause
// QSEECom client API, matching the canonical AOSP/CAF QSEEComAPI.h
// (hardware/qcom/keymaster/QSEEComAPI.h). The public handle exposes only
// ion_sbuffer; the library's real struct is larger but clients touch nothing
// else. Prefer vendoring the full CAF header; this is the subset we link.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct QSEECom_handle {
    unsigned char *ion_sbuffer;  // shared ION buffer start
};

// Load an app image (path, fname) and allocate a shared buffer of sb_size.
int QSEECom_start_app(struct QSEECom_handle **clnt_handle, const char *path,
                      const char *fname, uint32_t sb_size);

// Unload and free the handle.
int QSEECom_shutdown_app(struct QSEECom_handle **handle);

// Send sbuf_len bytes at send_buf, receive into rcv_buf (max rbuf_len).
int QSEECom_send_cmd(struct QSEECom_handle *handle, void *send_buf,
                     uint32_t sbuf_len, void *rcv_buf, uint32_t rbuf_len);

// true = raise bus bandwidth around a command, false = drop it again.
int QSEECom_set_bandwidth(struct QSEECom_handle *handle, bool high);

#ifdef __cplusplus
}
#endif
