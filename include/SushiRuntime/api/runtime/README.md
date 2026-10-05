# api/runtime

The Runtime entry point, its configuration and the Cluster handle.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## cluster.hpp

### Cluster

Runtime::cluster() returns a Cluster after promoting the runtime to Master and wiring the transport.
The handle bundles the cluster's tuning knobs and telemetry so callers do not reach into
RuntimeContext's distributed_\* surface directly. Offloadable tasks are still added with
op_id-bearing task metadata; the handle governs the cluster, not the graph.

## runtime.hpp

### RuntimeConfig

RuntimeConfig is the co-tenancy contract: worker count, core placement, pinning policy, profiling,
and whether the background rebalancer runs. A runtime inside a game shares the machine with a render
thread, an audio callback and asset I/O, so every one of these defaults is chosen for a tenant and
not for an owner. Execution::RuntimeSettings documents each field.

RuntimeConfig is an alias, not a distinct type: RuntimeContext and TaskScheduler consume the same
settings and live below this layer, which may not depend on it. RuntimeConfig is the public
spelling; RuntimeSettings is where it lives.

### AdvancedControls

AdvancedControls is the supported way to read the few internal facts a caller legitimately needs
(device count, profiling and rebalancer state, cumulative transfer bytes) without exposing the whole
RuntimeContext. Runtime::advanced() returns it. It borrows the context, so it must not outlive the
Runtime. For anything beyond this surface, Runtime::unsafe_context() is the deliberate escape hatch.

### worker_pool

A caller that reserved cores for its own threads uses worker_pool() to verify that the reservation
took effect: a range that did not fit the machine is clamped, and worker_pool() reports the clamped
result, not the request.

### dynamic_graph

dynamic_graph() is the mutable counterpart to graph(): work is grouped into regions (region(key))
that can be added or dropped between steps, so a streaming open world re-stitches only the cells
that changed instead of recompiling the whole graph. It is defined out of line in dynamic_graph.hpp,
where DynamicGraph is a complete type.

### buffer

The buffer borrows the runtime's context for allocation and frees itself on destruction, so it must
not outlive the Runtime.

### state

state() is for data a stepped graph reads as "current" and writes as "next"; the runtime swaps the
two each step. Like a buffer, the State must not outlive the Runtime.

### rebalancer

The rebalancer is off unless RuntimeConfig::rebalancer asked for it. Running it trades run-to-run
reproducibility and the jitter floor for throughput, by migrating tasks between workers on a 5 ms
heartbeat. That is worth it for long batch work and wrong for a simulation frame. rebalancer() is
safe to call between runs.

### unsafe_context

unsafe_context() exposes the internal RuntimeContext for uses the fluent surface and advanced() do
not cover. The name is a warning: code that reaches it couples to a non-public type and may break
across versions. The fluent API or advanced() is the better choice wherever either is enough. The
returned reference is owned by this Runtime.

### cluster

cluster() is a thin fluent wrapper over RuntimeContext::enable_distributed_master: it wires the
transport and starts serving. Until it is called, the runtime is a fully offline single machine with
zero distributed overhead.
