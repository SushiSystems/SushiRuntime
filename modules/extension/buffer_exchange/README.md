# buffer_exchange

The module moves buffer bytes between a master and its workers: the registry that gives each shared
allocation a cluster-wide handle and version, and the exchange that serves pulls in chunks and
applies the chunks of a task's outputs. It is an internal module: its headers reach the modules that
name it and the tests, and are never installed. It is built with the distributed layer, under
`SR_ENABLE_DISTRIBUTED`, and every name it declares is in `SushiRuntime::Distributed`.

It depends on `core`, `transport` and `wire`.

## Public entry points

| Header | Holds |
| --- | --- |
| `buffer_registry.hpp` | `BufferRegistry` |
| `buffer_exchange.hpp` | `BufferExchange`, `BufferExchangeStatistics`, `ChunkStatus` |
