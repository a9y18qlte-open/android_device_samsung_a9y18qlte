/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Stand-in for the qosd socket that Samsung's netd serves on stock.
 *
 * libsec-ril sends traffic-control commands for dedicated (VoLTE) bearers
 * to /dev/socket/qosd and never reads a reply. Its QoS thread retries each
 * command for about a second when nothing listens there, which holds back
 * the dedicated bearer notification to the IMS stack. This accepts the
 * commands and logs them.
 */

#define LOG_TAG "qosd"

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cutils/sockets.h>
#include <log/log.h>

#define MAX_CLIENTS 16

int main(void) {
    struct pollfd fds[MAX_CLIENTS + 1];
    int nfds = 1;
    char buf[1024];
    int listenFd = android_get_control_socket("qosd");

    if (listenFd < 0 || listen(listenFd, 8) < 0) {
        ALOGE("cannot listen on the init socket: %s", strerror(errno));
        return 1;
    }
    ALOGI("listening");

    fds[0].fd = listenFd;
    fds[0].events = POLLIN;
    for (;;) {
        if (poll(fds, nfds, -1) < 0) {
            if (errno == EINTR) continue;
            ALOGE("poll: %s", strerror(errno));
            return 1;
        }
        if (fds[0].revents & POLLIN) {
            int c = accept(listenFd, NULL, NULL);
            if (c >= 0) {
                if (nfds <= MAX_CLIENTS) {
                    fds[nfds].fd = c;
                    fds[nfds].events = POLLIN;
                    nfds++;
                } else {
                    close(c);
                }
            }
        }
        for (int i = 1; i < nfds; i++) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            ssize_t n = read(fds[i].fd, buf, sizeof(buf) - 1);
            if (n <= 0) {
                close(fds[i].fd);
                fds[i] = fds[--nfds];
                i--;
                continue;
            }
            buf[n] = '\0';
            for (ssize_t j = 0; j < n; j++) {
                if (buf[j] == '\0') buf[j] = '|';
            }
            ALOGI("cmd: %s", buf);
        }
    }
}
