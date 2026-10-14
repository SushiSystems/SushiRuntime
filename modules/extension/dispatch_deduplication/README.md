# dispatch_deduplication

The module remembers the last task ids a worker was dispatched, so a frame the master sends twice
runs once. It is an internal module: its headers reach the modules that name it and the tests, and
are never installed. It is built with the distributed layer, under `SR_ENABLE_DISTRIBUTED`, and
every name it declares is in `SushiRuntime::Distributed`.

It depends on `core`.

## Public entry points

| Header | Holds |
| --- | --- |
| `dispatch_deduplicator.hpp` | `DispatchDeduplicator` |
