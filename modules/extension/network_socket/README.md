# network_socket

The module holds one TCP connection and one listening socket, the one place the distributed layer
meets the operating system's socket interface. `socket_posix.cpp` and `socket_windows.cpp` hold the
two platforms; each compiles to nothing on the other. It is an internal module: its headers reach
the modules that name it and the tests, and are never installed. It is built with the distributed
layer, under `SR_ENABLE_DISTRIBUTED`, and every name it declares is in `SushiRuntime::Distributed`.

It depends on `core` and `transport`, and on `ws2_32` on Windows.

## Public entry points

| Header | Holds |
| --- | --- |
| `socket.hpp` | `Socket`, `Listener`, `SocketResult` |
