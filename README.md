# pico-serial-proxy

Proxy library for serial-over-PTY forwarding.

This project provides a small reusable library that creates a virtual PTY and proxies data between a real serial device and the virtual terminal.

## Features

- Instance-based proxy state using `pico_serial_proxy_t`
- Support for multiple proxies in one process
- Callback hooks for forwarded data and lifecycle events
- Clean shutdown request support

## API overview

The proxy API now uses an explicit state object so each proxy can store its own state.

```c
pico_serial_proxy_t proxy;

if (pico_serial_proxy_init(&proxy,
                           "/dev/ttyUSB0",
                           115200,
                           cb_context,
                           data_cb,
                           lifecycle_cb) != 0) {
    perror("failed to initialize proxy");
    return 1;
}

const char *virt_path = pico_serial_proxy_get_virt_tty(&proxy);
printf("Virtual TTY: %s\n", virt_path);

pico_serial_proxy_run(&proxy);
```

### Key functions

- `pico_serial_proxy_init(pico_serial_proxy_t *proxy, const char *real_tty_path, int baud_rate, void *cb_context, pico_serial_proxy_data_cb_t data_cb, pico_serial_proxy_lifecycle_cb_t lifecycle_cb)`
- `pico_serial_proxy_run(pico_serial_proxy_t *proxy)`
- `pico_serial_proxy_request_stop(pico_serial_proxy_t *proxy)`
- `pico_serial_proxy_get_virt_tty(const pico_serial_proxy_t *proxy)`
- `pico_serial_proxy_shutdown(pico_serial_proxy_t *proxy)`

## Parallel proxy example

The library is designed to support side-by-side proxy instances:

```c
pico_serial_proxy_t proxy_a;
pico_serial_proxy_t proxy_b;

pico_serial_proxy_init(&proxy_a, "/dev/ttyUSB0", 115200, ctx_a, data_cb_a, lifecycle_cb_a);
pico_serial_proxy_init(&proxy_b, "/dev/ttyUSB1", 115200, ctx_b, data_cb_b, lifecycle_cb_b);
```

Each object is independent and manages its own file descriptors, virtual TTY path, callbacks, and stop state.

## Build

This library is built by the sibling `pico-kiss-protocol` app through CMake. From `pico-kiss-protocol/apps/monitor`, CMake adds `../../../pico-serial-proxy` as a subdirectory and links `pico_serial_proxy`.

## Related docs

- `pico-kiss-protocol/apps/monitor/README.md`: example monitor app documentation and usage

## Suite

This repository is part of a suite:

- [pico-serial-proxy](https://github.com/fruit-bat/pico-serial-proxy)
- [pico-kiss-protocol](https://github.com/fruit-bat/pico-kiss-protocol)
- [pico-rnode-protocol](https://github.com/fruit-bat/pico-rnode-protocol)
