# frame_transport

The module orders the frames a link sends: control frames ahead of buffer streams, streams in turn,
a bounded queue, and the helper that encodes a message and queues it as one control frame. It is an
internal module: its headers reach the modules that name it and the tests, and are never installed.
It is built with the distributed layer, under `SR_ENABLE_DISTRIBUTED`, and every name it declares is
in `SushiRuntime::Distributed`.

It depends on `core`, `transport` and `wire`.

## Public entry points

| Header | Holds |
| --- | --- |
| `outbox.hpp` | `Outbox`, `PopStatus` |
| `frame_sending.hpp` | `send_message()` |

## frame_sending.hpp

`send_message(transport, peer, message, scratch)` encodes a wire message into a buffer the caller
reuses and queues it with `Transport::send()`; the three-argument form uses a buffer of its own.
The coordinator, the monitor, `MasterLink` and `DispatchExecutor` send every control frame through
it.
