# in_process_transport

The in-memory adapter of the `Transport` port: a master endpoint and any number of worker endpoints
that pass version 4 frames inside one process, through a fault policy a test sets. It is built with
the distributed layer, under `SR_ENABLE_DISTRIBUTED`, and every name it declares is in
`SushiRuntime::Distributed`. Its headers are installed.

It depends on `core` and `transport` through its header, and on `log`, `wire` and `frame_transport`
in its source. Nothing in `distributed` includes it; the tests of `distributed` and of this module
use it as the fake of a link.

## Public entry points

| Header | Holds |
| --- | --- |
| `in_process_transport.hpp` | `InProcessFabric`, `InProcessTransport`, `InProcessMasterOptions`, `InProcessWorkerOptions`, `FaultPolicy`, `FaultedFrame`, `FaultAction`, `FrameDirection` |
| `in_process_transport_log_category.hpp` | `K_IN_PROCESS_TRANSPORT_LOG_CATEGORY` |

## in_process_transport.hpp

One `InProcessFabric` joins one master endpoint and any number of worker endpoints, made with
`InProcessTransport::make_master(fabric)` and `make_worker(fabric)`. The fabric numbers worker
endpoints from 0 in the order they are made; that index, not a `PeerID`, is what
`set_partitioned()` and a `FaultPolicy` name, because it stays the same across reconnects.

A `FaultPolicy` sees every frame as it enters the fabric, control or stream, as a `FaultedFrame`:
direction, worker index, type, flags and payload. A partition discards frames in both directions
and tells neither side, which is a half-open link; the connection ends only when the master's
liveness check closes the peer.

`control_queue_frames` in `InProcessMasterOptions` and `InProcessWorkerOptions` bounds the control
frames queued per peer; a `send()` past the bound returns `QueueFull`. A bound of 0 refuses every
control frame, which is how a test makes the link refuse a send.
