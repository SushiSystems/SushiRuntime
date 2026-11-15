# buffer_exchange

The module moves buffer bytes between a master and its workers: `SharedBuffers`, which gives each shared
buffer a version and copies its bytes, and the exchange that serves pulls in chunks and
applies the chunks of a task's outputs. It is an internal module: its headers reach the modules that
name it and the tests, and are never installed. It is built with the distributed layer, under
`SR_ENABLE_DISTRIBUTED`, and every name it declares is in `SushiRuntime::Distributed`.

A buffer is a `GlobalHandle`. `wire_id_of()` turns a handle into the `GlobalBufferID` the wire still
carries, as a pure function of the handle; wire version 5 sends the handle itself and drops it.
`SharedBuffers` keeps no address: a copy asks the `HandleResolver` where the instance is.

It depends on `core`, `handle`, `task`, `transport` and `wire`.

## Public entry points

| Header | Holds |
| --- | --- |
| `shared_buffers.hpp` | `SharedBuffers`, `BufferAccessStatus` |
| `buffer_wire_id.hpp` | `wire_id_of()` |
| `buffer_exchange.hpp` | `BufferExchange`, `BufferExchangeStatistics`, `ChunkStatus` |
