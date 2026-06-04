// SPDX-License-Identifier: MIT
// pico-serial-proxy public API
#ifndef PICO_SERIAL_PROXY_H
#define PICO_SERIAL_PROXY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*pico_serial_proxy_data_cb_t)(
    void *context,
    bool host_to_device,
    const uint8_t *data,
    size_t len
);

typedef enum {
    PICO_SERIAL_PROXY_EVENT_STARTED = 0,
    PICO_SERIAL_PROXY_EVENT_STOPPED = 1,
    PICO_SERIAL_PROXY_EVENT_ERROR = 2,
} pico_serial_proxy_event_t;

typedef void (*pico_serial_proxy_lifecycle_cb_t)(
    void *context,
    pico_serial_proxy_event_t event,
    const char *message // optional human-readable message for errors
);

// Initialize the proxy. `real_tty_path` is the physical device path.
// `baud_rate` sets the baud for the real device (0 means leave unchanged).
// `cb_context` is passed to `data_cb` when invoked. Returns 0 on success.
int pico_serial_proxy_init(const char *real_tty_path,
                           int baud_rate,
                           void *cb_context,
                           pico_serial_proxy_data_cb_t data_cb,
                           pico_serial_proxy_lifecycle_cb_t lifecycle_cb);

// Run the proxy loop (blocking). Returns 0 on clean exit, -1 on error.
int pico_serial_proxy_run(void);

// Request a clean shutdown from another thread or signal handler.
void pico_serial_proxy_request_stop(void);

// Get the virtual PTY path created by the proxy. Returns NULL until
// `pico_serial_proxy_init` has completed successfully.
const char *pico_serial_proxy_get_virt_tty(void);

// Clean up and close any open file descriptors.
void pico_serial_proxy_shutdown(void);

#endif // PICO_SERIAL_PROXY_H
