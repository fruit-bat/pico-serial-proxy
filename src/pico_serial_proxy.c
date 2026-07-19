// SPDX-License-Identifier: MIT
// Implementation of a small PTY <-> real tty proxy with data callbacks.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "../include/pico-serial-proxy.h"

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/types.h>

#define BUFFER_SIZE 1024

static int baud_rate_to_speed(int baud, speed_t *speed) {
    switch (baud) {
        case 0: *speed = B0; return 0;
        case 50: *speed = B50; return 0;
        case 75: *speed = B75; return 0;
        case 110: *speed = B110; return 0;
        case 134: *speed = B134; return 0;
        case 150: *speed = B150; return 0;
        case 200: *speed = B200; return 0;
        case 300: *speed = B300; return 0;
        case 600: *speed = B600; return 0;
        case 1200: *speed = B1200; return 0;
        case 1800: *speed = B1800; return 0;
        case 2400: *speed = B2400; return 0;
        case 4800: *speed = B4800; return 0;
        case 9600: *speed = B9600; return 0;
        case 19200: *speed = B19200; return 0;
        case 38400: *speed = B38400; return 0;
        case 57600: *speed = B57600; return 0;
        case 115200: *speed = B115200; return 0;
        case 230400: *speed = B230400; return 0;
        default: return -1;
    }
}

static int configure_serial_fd(int fd, int baud_rate) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        return -1;
    }
    cfmakeraw(&tty);
    if (baud_rate > 0) {
        speed_t speed;
        if (baud_rate_to_speed(baud_rate, &speed) != 0) {
            errno = EINVAL;
            return -1;
        }
        cfsetispeed(&tty, speed);
        cfsetospeed(&tty, speed);
    }
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        return -1;
    }
    return 0;
}

static ssize_t write_all(int fd, const void *buffer, size_t len) {
    const uint8_t *ptr = (const uint8_t *)buffer;
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t written = write(fd, ptr, remaining);
        if (written <= 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        ptr += written;
        remaining -= (size_t)written;
    }
    return (ssize_t)len;
}

static void proxy_state_init(pico_serial_proxy_t *proxy) {
    if (!proxy) {
        return;
    }

    proxy->real_fd = -1;
    proxy->virt_master_fd = -1;
    proxy->virt_slave_fd = -1;
    proxy->virt_tty_path = NULL;
    proxy->cb_context = NULL;
    proxy->data_cb = NULL;
    proxy->lifecycle_cb = NULL;
    proxy->stop_requested = 0;
}

int pico_serial_proxy_init(
    pico_serial_proxy_t *proxy,
    const char *real_tty_path,
    int baud_rate,
    void *cb_context,
    pico_serial_proxy_data_cb_t data_cb,
    pico_serial_proxy_lifecycle_cb_t lifecycle_cb
) {
    if (!proxy || !real_tty_path || !data_cb) {
        errno = EINVAL;
        return -1;
    }

    proxy_state_init(proxy);

    proxy->real_fd = open(real_tty_path, O_RDWR | O_NOCTTY);
    if (proxy->real_fd < 0) {
        return -1;
    }

    if (configure_serial_fd(proxy->real_fd, baud_rate) != 0) {
        close(proxy->real_fd);
        proxy->real_fd = -1;
        return -1;
    }

    proxy->virt_master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (proxy->virt_master_fd < 0) {
        close(proxy->real_fd);
        proxy->real_fd = -1;
        return -1;
    }
    if (grantpt(proxy->virt_master_fd) < 0 || unlockpt(proxy->virt_master_fd) < 0) {
        close(proxy->real_fd);
        close(proxy->virt_master_fd);
        proxy_state_init(proxy);
        return -1;
    }

    char *virt_tty = ptsname(proxy->virt_master_fd);
    if (!virt_tty) {
        close(proxy->real_fd);
        close(proxy->virt_master_fd);
        proxy_state_init(proxy);
        return -1;
    }

    proxy->virt_tty_path = strdup(virt_tty);
    if (!proxy->virt_tty_path) {
        close(proxy->real_fd);
        close(proxy->virt_master_fd);
        proxy_state_init(proxy);
        return -1;
    }

    proxy->virt_slave_fd = open(proxy->virt_tty_path, O_RDWR | O_NOCTTY);
    if (proxy->virt_slave_fd < 0) {
        free(proxy->virt_tty_path);
        proxy_state_init(proxy);
        return -1;
    }

    if (configure_serial_fd(proxy->virt_slave_fd, 0) != 0) {
        close(proxy->real_fd);
        close(proxy->virt_master_fd);
        close(proxy->virt_slave_fd);
        free(proxy->virt_tty_path);
        proxy_state_init(proxy);
        return -1;
    }

    proxy->cb_context = cb_context;
    proxy->data_cb = data_cb;
    proxy->lifecycle_cb = lifecycle_cb;
    proxy->stop_requested = 0;

    if (proxy->lifecycle_cb) {
        proxy->lifecycle_cb(proxy->cb_context, PICO_SERIAL_PROXY_EVENT_STARTED, proxy->virt_tty_path);
    }

    return 0;
}

const char *pico_serial_proxy_get_virt_tty(const pico_serial_proxy_t *proxy) {
    return proxy ? proxy->virt_tty_path : NULL;
}

void pico_serial_proxy_shutdown(pico_serial_proxy_t *proxy) {
    if (!proxy) {
        return;
    }

    if (proxy->real_fd >= 0) {
        close(proxy->real_fd);
    }
    if (proxy->virt_master_fd >= 0) {
        close(proxy->virt_master_fd);
    }
    if (proxy->virt_slave_fd >= 0) {
        close(proxy->virt_slave_fd);
    }
    if (proxy->virt_tty_path) {
        free(proxy->virt_tty_path);
    }

    proxy_state_init(proxy);
}

int pico_serial_proxy_run(pico_serial_proxy_t *proxy) {
    if (!proxy || proxy->real_fd < 0 || proxy->virt_master_fd < 0 || proxy->data_cb == NULL) {
        errno = EINVAL;
        return -1;
    }

    printf("Proxy active.\n");
    printf("Real Device:   %s\n", "(real device)");
    if (proxy->virt_tty_path) {
        printf("Virtual TTY:   %s\n", proxy->virt_tty_path);
        printf("Connect your application to the Virtual TTY.\n\n");
        printf("HOST<->DEV\n");
    }
    
    char buffer[BUFFER_SIZE];
    fd_set read_fds;
    int max_fd = (proxy->real_fd > proxy->virt_master_fd) ? proxy->real_fd : proxy->virt_master_fd;

    const char *stop_reason = "shutdown";
    while (!proxy->stop_requested) {
        FD_ZERO(&read_fds);
        FD_SET(proxy->real_fd, &read_fds);
        FD_SET(proxy->virt_master_fd, &read_fds);

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                if (proxy->stop_requested) {
                    stop_reason = "requested stop";
                    break;
                }
                continue;
            }
            if (proxy->lifecycle_cb) {
                proxy->lifecycle_cb(proxy->cb_context, PICO_SERIAL_PROXY_EVENT_ERROR, strerror(errno));
            }
            perror("Select error");
            break;
        }

        if (FD_ISSET(proxy->real_fd, &read_fds)) {
            ssize_t bytes_read = read(proxy->real_fd, buffer, sizeof(buffer));
            if (bytes_read <= 0) {
                if (bytes_read < 0) {
                    if (proxy->lifecycle_cb) proxy->lifecycle_cb(proxy->cb_context, PICO_SERIAL_PROXY_EVENT_ERROR, strerror(errno));
                    perror("Read error from real device");
                }
                break;
            }
            if (write_all(proxy->virt_master_fd, buffer, (size_t)bytes_read) < 0) {
                if (proxy->lifecycle_cb) proxy->lifecycle_cb(proxy->cb_context, PICO_SERIAL_PROXY_EVENT_ERROR, strerror(errno));
                perror("Write error to virtual PTY");
                break;
            }
            else {
                proxy->data_cb(
                    proxy->cb_context, 
                    PICO_SERIAL_PROXY_DIRECTION_DEVICE_TO_HOST, 
                    (const uint8_t *)buffer, 
                    (size_t)bytes_read);
            }
        }

        if (FD_ISSET(proxy->virt_master_fd, &read_fds)) {
            ssize_t bytes_read = read(proxy->virt_master_fd, buffer, sizeof(buffer));
            if (bytes_read <= 0) {
                if (bytes_read < 0) {
                    if (proxy->lifecycle_cb) proxy->lifecycle_cb(proxy->cb_context, PICO_SERIAL_PROXY_EVENT_ERROR, strerror(errno));
                    perror("Read error from virtual PTY");
                }
                break;
            }
            if (write_all(proxy->real_fd, buffer, (size_t)bytes_read) < 0) {
                if (proxy->lifecycle_cb) proxy->lifecycle_cb(proxy->cb_context, PICO_SERIAL_PROXY_EVENT_ERROR, strerror(errno));
                perror("Write error to real device");
                break;
            }
            else {
                proxy->data_cb(
                    proxy->cb_context, 
                    PICO_SERIAL_PROXY_DIRECTION_HOST_TO_DEVICE, 
                    (const uint8_t *)buffer, 
                    (size_t)bytes_read);
            }
        }
    }

    if (proxy->lifecycle_cb) {
        proxy->lifecycle_cb(proxy->cb_context, PICO_SERIAL_PROXY_EVENT_STOPPED, stop_reason);
    }
    pico_serial_proxy_shutdown(proxy);
    return 0;
}

void pico_serial_proxy_request_stop(pico_serial_proxy_t *proxy) {
    if (!proxy) {
        return;
    }
    proxy->stop_requested = 1;
}
