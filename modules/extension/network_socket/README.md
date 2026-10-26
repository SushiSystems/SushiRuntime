# network_socket

The module holds one TCP connection and one listening socket, the one place the distributed layer
meets the operating system's socket interface. `socket_posix.cpp` and `socket_windows.cpp` hold the
two platforms; each compiles to nothing on the other. It is an internal module: its headers reach
the modules that name it and the tests, and are never installed. It is built with the distributed
layer, under `SR_ENABLE_DISTRIBUTED`, and every name it declares is in `SushiRuntime::Distributed`.

It depends on `core`, `transport` and `log`, and on `ws2_32` on Windows.

## Public entry points

| Header | Holds |
| --- | --- |
| `socket.hpp` | `Socket`, `Listener`, `SocketResult` |
| `network_socket_log_category.hpp` | `K_NETWORK_SOCKET_LOG_CATEGORY`, the module's category, `sushiruntime.network_socket` |
| `socket_option_report.hpp` | `Detail::report_no_delay_failure`, the line a failed `TCP_NODELAY` writes |

## What the module logs

A connection, connected or accepted, asks the OS for `TCP_NODELAY` once. When the OS refuses, the
module logs one `WARN` with the peer (the listener's address for an accepted connection, since the
accept call does not read the peer's) and the OS error, and the connection goes on with Nagle's
delay. The line is written once per connection by construction. Every other failure is returned
as a `SocketResult` and logged by whoever holds it.
