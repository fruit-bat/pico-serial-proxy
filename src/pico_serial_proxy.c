// SPDX-License-Identifier: MIT
// Implementation of a small PTY <-> real tty proxy with data callbacks.
#define _GNU_SOURCE
#define _XOPEN_SOURCE 700

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

struct proxy_state {
    int real_fd;
    int virt_master_fd;
    int virt_slave_fd;
    char *virt_tty_path;
    void *cb_context;
    pico_serial_proxy_data_cb_t data_cb;
    pico_serial_proxy_lifecycle_cb_t lifecycle_cb;
    volatile sig_atomic_t stop_requested;
};

static struct proxy_state state = {
    .real_fd = -1,
    .virt_master_fd = -1,
    .virt_slave_fd = -1,
    .virt_tty_path = NULL,
    .cb_context = NULL,
    .data_cb = NULL,
    .lifecycle_cb = NULL,
    .stop_requested = 0,
};

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

int pico_serial_proxy_init(const char *real_tty_path,
                           int baud_rate,
                           void *cb_context,
                           pico_serial_proxy_data_cb_t data_cb,
                           pico_serial_proxy_lifecycle_cb_t lifecycle_cb) {
    if (!real_tty_path || !data_cb) {
        errno = EINVAL;
        return -1;
    }

    state.real_fd = open(real_tty_path, O_RDWR | O_NOCTTY);
    if (state.real_fd < 0) {
        return -1;
    }

    if (configure_serial_fd(state.real_fd, baud_rate) != 0) {
        close(state.real_fd);
        state.real_fd = -1;
        return -1;
    }

    state.virt_master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (state.virt_master_fd < 0) {
        close(state.real_fd);
        state.real_fd = -1;
        return -1;
    }
    if (grantpt(state.virt_master_fd) < 0 || unlockpt(state.virt_master_fd) < 0) {
        close(state.real_fd);
        close(state.virt_master_fd);
        state.real_fd = -1;
        state.virt_master_fd = -1;
        return -1;
    }

    char *virt_tty = ptsname(state.virt_master_fd);
    if (!virt_tty) {
        close(state.real_fd);
        close(state.virt_master_fd);
        state.real_fd = -1;
        state.virt_master_fd = -1;
        return -1;
    }

    state.virt_tty_path = strdup(virt_tty);
    state.virt_slave_fd = open(state.virt_tty_path, O_RDWR | O_NOCTTY);
    if (state.virt_slave_fd < 0) {
        free(state.virt_tty_path);
        state.virt_tty_path = NULL;
        close(state.real_fd);
        close(state.virt_master_fd);
        state.real_fd = -1;
        state.virt_master_fd = -1;
        return -1;
    }

    if (configure_serial_fd(state.virt_slave_fd, 0) != 0) {
        close(state.real_fd);
        close(state.virt_master_fd);
        close(state.virt_slave_fd);
        free(state.virt_tty_path);
        state.real_fd = -1;
        state.virt_master_fd = -1;
        state.virt_slave_fd = -1;
        state.virt_tty_path = NULL;
        return -1;
    }

    state.cb_context = cb_context;
    state.data_cb = data_cb;
    state.lifecycle_cb = lifecycle_cb;
    state.stop_requested = 0;

    if (state.lifecycle_cb) {
        // Inform caller that proxy has been initialized and provide the virt path
        state.lifecycle_cb(state.cb_context, PICO_SERIAL_PROXY_EVENT_STARTED, state.virt_tty_path);
    }

    return 0;
}

const char *pico_serial_proxy_get_virt_tty(void) {
    return state.virt_tty_path;
}

void pico_serial_proxy_shutdown(void) {
    if (state.real_fd >= 0) { close(state.real_fd); state.real_fd = -1; }
    if (state.virt_master_fd >= 0) { close(state.virt_master_fd); state.virt_master_fd = -1; }
    if (state.virt_slave_fd >= 0) { close(state.virt_slave_fd); state.virt_slave_fd = -1; }
    if (state.virt_tty_path) { free(state.virt_tty_path); state.virt_tty_path = NULL; }
}

int pico_serial_proxy_run(void) {
    if (state.real_fd < 0 || state.virt_master_fd < 0 || state.data_cb == NULL) {
        errno = EINVAL;
        return -1;
    }

    printf("Proxy active.\n");
    printf("Real Device:   %s\n", "(real device)");
    if (state.virt_tty_path) {
        printf("Virtual TTY:   %s\n", state.virt_tty_path);
        printf("Connect your application to the Virtual TTY.\n\n");
    }

    char buffer[BUFFER_SIZE];
    fd_set read_fds;
    int max_fd = (state.real_fd > state.virt_master_fd) ? state.real_fd : state.virt_master_fd;

    const char *stop_reason = "shutdown";
    while (!state.stop_requested) {
        FD_ZERO(&read_fds);
        FD_SET(state.real_fd, &read_fds);
        FD_SET(state.virt_master_fd, &read_fds);

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                if (state.stop_requested) {
                    stop_reason = "requested stop";
                    break;
                }
                continue;
            }
            if (state.lifecycle_cb) {
                state.lifecycle_cb(state.cb_context, PICO_SERIAL_PROXY_EVENT_ERROR, strerror(errno));
            }
            perror("Select error");
            break;
        }

        if (FD_ISSET(state.real_fd, &read_fds)) {
            ssize_t bytes_read = read(state.real_fd, buffer, sizeof(buffer));
            if (bytes_read <= 0) {
                if (bytes_read < 0) {
                    if (state.lifecycle_cb) state.lifecycle_cb(state.cb_context, PICO_SERIAL_PROXY_EVENT_ERROR, strerror(errno));
                    perror("Read error from real device");
                }
                break;
            }
            // Notify callback: device -> host (host_to_device = false)
            state.data_cb(state.cb_context, false, (const uint8_t *)buffer, (size_t)bytes_read);
            if (write_all(state.virt_master_fd, buffer, (size_t)bytes_read) < 0) {
                if (state.lifecycle_cb) state.lifecycle_cb(state.cb_context, PICO_SERIAL_PROXY_EVENT_ERROR, strerror(errno));
                perror("Write error to virtual PTY");
                break;
            }
        }

        if (FD_ISSET(state.virt_master_fd, &read_fds)) {
            ssize_t bytes_read = read(state.virt_master_fd, buffer, sizeof(buffer));
            if (bytes_read <= 0) {
                if (bytes_read < 0) {
                    if (state.lifecycle_cb) state.lifecycle_cb(state.cb_context, PICO_SERIAL_PROXY_EVENT_ERROR, strerror(errno));
                    perror("Read error from virtual PTY");
                }
                break;
            }
            // Notify callback: host -> device (host_to_device = true)
            state.data_cb(state.cb_context, true, (const uint8_t *)buffer, (size_t)bytes_read);
            if (write_all(state.real_fd, buffer, (size_t)bytes_read) < 0) {
                if (state.lifecycle_cb) state.lifecycle_cb(state.cb_context, PICO_SERIAL_PROXY_EVENT_ERROR, strerror(errno));
                perror("Write error to real device");
                break;
            }
        }
    }
    if (state.lifecycle_cb) state.lifecycle_cb(state.cb_context, PICO_SERIAL_PROXY_EVENT_STOPPED, stop_reason);
    pico_serial_proxy_shutdown();
    return 0;
}

void pico_serial_proxy_request_stop(void) {
    state.stop_requested = 1;
}
