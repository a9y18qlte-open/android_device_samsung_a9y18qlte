/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

// libvdis_core uses the pre-4.12 ION ABI (handles, ION_IOC_SHARE, msm cache
// ioctls). Its imports are renamed to these by a blob fixup: buffers become
// dma-buf fds, with the fd standing in for the handle.

#include <errno.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <ion/ion.h>

// legacy msm ION_IOC_CUSTOM + ION_IOC_CLEAN_INV_CACHES
static constexpr unsigned long kIonIocCustom = 0xc0084906;
static constexpr unsigned int kIonIocCleanInvCaches = 0xc0144d01;

struct legacy_custom_data {
    unsigned int cmd;
    unsigned long arg;
};

// struct legacy_flush_data { handle; fd; vaddr; offset; length; }
struct legacy_flush_data {
    int handle;
    int fd;
    void *vaddr;
    unsigned int offset;
    unsigned int length;
};

static int dmabuf_sync(int fd) {
    struct dma_buf_sync s = {DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW};
    if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &s)) return -errno;
    s.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
    if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &s)) return -errno;
    return 0;
}

extern "C" {

int vds_alloc(int fd, size_t len, size_t align, unsigned int heap_mask, unsigned int flags,
              ion_user_handle_t *handle) {
    int buf_fd;
    int ret = ion_alloc_fd(fd, len, align, heap_mask, flags, &buf_fd);
    if (ret) return ret;
    *handle = buf_fd;
    return 0;
}

int vds_share(int /* fd */, ion_user_handle_t handle, int *share_fd) {
    int ret = dup(handle);
    if (ret < 0) return -errno;
    *share_fd = ret;
    return 0;
}

int vds_free(int /* fd */, ion_user_handle_t handle) {
    return close(handle) ? -errno : 0;
}

int vds_sync_fd(int /* fd */, int handle_fd) {
    return dmabuf_sync(handle_fd);
}

int vdsio(int fd, unsigned long request, void *arg) {
    if (request == kIonIocCustom) {
        auto *custom = static_cast<legacy_custom_data *>(arg);
        if (custom->cmd == kIonIocCleanInvCaches)
            return dmabuf_sync(reinterpret_cast<legacy_flush_data *>(custom->arg)->fd) ? -1 : 0;
    }
    return ioctl(fd, request, arg);
}

}  // extern "C"
