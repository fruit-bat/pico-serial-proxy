// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PICO_SERIAL_PROXY_DIRECTION_HOST_TO_DEVICE = 0,
    PICO_SERIAL_PROXY_DIRECTION_DEVICE_TO_HOST = 1,
} pico_serial_proxy_direction_t;

/**
 * Callback invoked when the proxy forwards data in either direction.
 *
 * @param context User-provided opaque context.
 * @param direction Direction of the data flow.
 * @param data Byte buffer containing forwarded data.
 * @param len Number of bytes in the data buffer.
 */
typedef void (*pico_serial_proxy_data_cb_t)(
    void *context,
    pico_serial_proxy_direction_t direction,
    const uint8_t *data,
    size_t len
);

/**
 * Proxy lifecycle events.
 */
typedef enum {
    PICO_SERIAL_PROXY_EVENT_STARTED = 0,
    PICO_SERIAL_PROXY_EVENT_STOPPED = 1,
    PICO_SERIAL_PROXY_EVENT_ERROR = 2,
} pico_serial_proxy_event_t;

/**
 * Callback invoked when the proxy changes state or reports an error.
 *
 * @param context User-provided opaque context.
 * @param event Lifecycle event type.
 * @param message Optional human-readable message (error text or virtual PTY path).
 */
typedef void (*pico_serial_proxy_lifecycle_cb_t)(
    void *context,
    pico_serial_proxy_event_t event,
    const char *message
);

/**
 * Proxy state object.
 *
 * This struct is owned by the caller and stores all per-instance proxy state.
 */
typedef struct {
    int real_fd;
    int virt_master_fd;
    int virt_slave_fd;
    char *virt_tty_path;
    void *cb_context;
    pico_serial_proxy_data_cb_t data_cb;
    pico_serial_proxy_lifecycle_cb_t lifecycle_cb;
    volatile sig_atomic_t stop_requested;
} pico_serial_proxy_t;

/**
 * Initialize a proxy instance.
 *
 * @param proxy Pointer to a proxy state object to initialize.
 * @param real_tty_path Path to the physical serial device.
 * @param baud_rate Baud rate for the real device (0 leaves current speed unchanged).
 * @param cb_context Opaque context forwarded to callbacks.
 * @param data_cb Callback invoked for each forwarded data chunk.
 * @param lifecycle_cb Optional callback invoked for proxy lifecycle events.
 * @return 0 on success, -1 on error with errno set.
 */
int pico_serial_proxy_init(
    pico_serial_proxy_t *proxy,
    const char *real_tty_path,
    int baud_rate,
    void *cb_context,
    pico_serial_proxy_data_cb_t data_cb,
    pico_serial_proxy_lifecycle_cb_t lifecycle_cb
);

/**
 * Run the proxy loop for a proxy instance.
 *
 * This function blocks until the proxy stops or encounters an error.
 *
 * @param proxy Initialized proxy instance.
 * @return 0 on clean exit, -1 on error with errno set.
 */
int pico_serial_proxy_run(pico_serial_proxy_t *proxy);

/**
 * Request a clean shutdown for a proxy instance.
 *
 * May be called from another thread or a signal handler.
 *
 * @param proxy Proxy instance to stop.
 */
void pico_serial_proxy_request_stop(pico_serial_proxy_t *proxy);

/**
 * Get the virtual PTY path created by the proxy instance.
 *
 * @param proxy Proxy instance.
 * @return Virtual PTY path or NULL if not initialized.
 */
const char *pico_serial_proxy_get_virt_tty(const pico_serial_proxy_t *proxy);

/**
 * Clean up and close resources associated with a proxy instance.
 *
 * @param proxy Proxy instance to shut down.
 */
void pico_serial_proxy_shutdown(pico_serial_proxy_t *proxy);

#ifdef __cplusplus
}
#endif
