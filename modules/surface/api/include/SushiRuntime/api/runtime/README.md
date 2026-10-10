# api/runtime

The Runtime entry point, its configuration and its read-only view.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## Lifetime

One rule covers every handle. `Runtime`, `Graph`, `DynamicGraph`, `Buffer`, `State`, `Interop` and
`AdvancedControls` each hold a `shared_ptr` to the same `Execution::RuntimeContext`, and the context
is destroyed when the last of them is. A `Runtime` may therefore be destroyed before the graphs,
buffers and views it handed out; they keep working, and the worker threads and device queues stop
when the last handle goes.

`Runtime` itself has no destructor logic and owns nothing but its share. This page states no rule
between two handles, such as a graph and the buffers recorded in it; `api/graph/README.md` and
`api/data/README.md` hold those.

## runtime_config.hpp

### RuntimeConfig

`RuntimeConfig` is the one argument of `Runtime::create()`. It holds the device filter (`devices`)
and the co-tenancy contract: worker count, core placement, pinning policy, profiling, the background
rebalancer and the forced scheduler path. A runtime inside a game shares the machine with a render
thread, an audio callback and asset I/O, so every default is chosen for a tenant and not for an
owner.

The struct belongs to the api module. The layers below keep their own types,
`Topology::HardwareFilterConfig` and `Execution::RuntimeSettings`, and may not depend on this one;
`Runtime::settings_of()` is the single place that maps one to the other. A field added to
`RuntimeSettings` reaches the public surface only when it is added here and in that function.
`modules/engine/execution/README.md` documents what each setting does to the engine.

Logging is process state and has no field here; `set_log_level()` and `set_log_sink()` set it.

### DeviceFilter, PinPolicy, WorkerPool

The three aliases let a caller spell the whole entry path in one namespace. Each names the type of a
lower module, so a value passes to that module without a conversion.

## advanced_controls.hpp

### AdvancedControls

`AdvancedControls` is the supported way to read the few internal facts a caller needs (the device
list, profiling and rebalancer state, the worker pool, cumulative transfer bytes) without the whole
`RuntimeContext`. `Runtime::advanced()` returns it and nothing else can construct it. It shares
ownership of the context like every other handle; see Lifetime. For anything beyond this surface,
`Runtime::unsafe_context()` is the deliberate escape hatch.

### devices

`devices()` lists the devices in SYCL enumeration order, and entry `i` describes `DeviceIndex{i}`.
The runtime does not reorder them, so `DeviceIndex{0}`, the default of `buffer()` and `state()`, is
a CPU on one machine and a GPU on another. A program that needs a particular device reads the
`kind`, `backend` or `name` of each entry and picks the index. The reference the call returns stays
valid while any handle of the runtime lives.

### worker_pool

A caller that reserved cores for its own threads uses `worker_pool()` to verify that the reservation
took effect: a range that did not fit the machine is clamped, and `worker_pool()` reports the
clamped result, not the request.

## runtime.hpp

### create

`create()` calls `require_matching_version()` before it builds anything. The function is defined in
the header, so it compares the release the caller compiled against with the release of the shared
library the loader found, and a mismatch fails with a message naming both instead of failing later
in an unrelated call.

The two-argument `create(devices, config)` is deprecated. It copies `config`, replaces its `devices`
with the first argument and forwards.

### Copy and const

A `Runtime` moves and does not copy. A copy would be a second handle to the same context, which
reads as a second runtime and is not one. The factories and `set_rebalancer_enabled()` are `const`
because a `Runtime` is a handle: constness of the handle says nothing about the context it points
at, and a `const Runtime&` parameter can allocate and build graphs.

### dynamic_graph

`dynamic_graph()` is the mutable counterpart to `graph()`: work is grouped into regions
(`region(key)`) that can be added or dropped between steps, so a streaming open world re-stitches
only the cells that changed instead of recompiling the whole graph.

### buffer

`buffer()` allocates on the device its `DeviceIndex` names, and the buffer frees the memory when it
is destroyed. An index outside `[0, advanced().device_count())` throws `SushiException` with
`Errc::invalid_access`, and the message carries the index and the device count.

### state

`state()` is for data a stepped graph reads as "current" and writes as "next"; the runtime swaps the
two each step. It validates its `DeviceIndex` as `buffer()` does.

### set_rebalancer_enabled

The rebalancer is off unless `RuntimeConfig::rebalancer` asked for it. Running it trades run-to-run
reproducibility and the jitter floor for throughput, by migrating tasks between workers on a 5 ms
heartbeat. That is worth it for long batch work and wrong for a simulation frame.
`set_rebalancer_enabled()` is safe to call between runs. `rebalancer(bool)` is its deprecated name.

### unsafe_context

`unsafe_context()` exposes the internal `RuntimeContext` for uses the fluent surface and
`advanced()` do not cover. The name is a warning: code that reaches it couples to a non-public type
and may break across versions. The fluent API or `advanced()` is the better choice wherever either
is enough. The reference stays valid while any handle of the runtime lives.

The distributed layer sits above this one and enters through it: `Distributed::Cluster::attach()`
takes the Runtime and installs its master on this context, so no header here names a transport.
