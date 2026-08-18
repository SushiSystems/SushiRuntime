# SushiRuntime — Open Work

---

## Correctness (blockers before v1.0.0)

**#29 — ThreadLocalMagazine teardown ordering — RESOLVED, then superseded**

The fix landed as described: a flush took the registry lock shared and verified
ownership, so it no-oped once its allocator was gone, and the remaining hole was
one level out — the lock was a namespace-scope `std::shared_timed_mutex`, and a
`thread_local` destructor runs at *thread* exit, which for a thread outliving
`main()` is after static destruction. Locking a destroyed mutex in the very path
that exists to make teardown safe is the same use-after-free, moved up a level.
It was changed to allocate once and never free.

As of 2026-08-18 the magazine, the pool and the allocator they belonged to are
deleted (commit `6e4b87b`), so the hazard is gone by construction rather than by
fix. The reasoning is worth keeping because it applies to any thread-local the
runtime grows later. The regression that guarded it survives, retargeted at the
thread-local state EBR still keeps, as
`Regression_Sentinel.AThreadOutlivingTheRuntimeTearsDownCleanly`.

**#30 — EventPolling 256-event cap — RESOLVED**

`ActiveEventList` no longer drops events. It keeps 256 entries inline (no
allocation in the common case) and spills the overflow into a heap vector; its
`push()` never fails and never drops. The 256 is now an inline fast-path capacity,
not a hard cap, so the deadlock this item described cannot occur.

**#33 — EventPolling `Task*` → `Node*` downcast — RESOLVED**

`Task` gained `on_complete(ReadySink&)` and `record_completion(device_ns)`, and
`Node` overrides both: it walks its own inline, spillover and cross-region
successors, decrements their in-degrees, and reports the freed ones. `EventPolling`
no longer downcasts, no longer names `Graph::Node`, and no longer includes
`graph/node.hpp` — the scheduler header now compiles without the graph layer,
which was the actual defect, since a lower layer was including a higher one.

`ReadySink` is a reused buffer, not a callback interface, and that choice was
measured rather than assumed. A virtual call per freed successor read well on
paper — one predicted indirect branch against three atomics — and cost **4.6x** on
`Performance_WideFanout` (286 → 1303 ns/task): the loss is not the call, it is that
the call cannot be inlined and it sits in the middle of a loop that previously
inlined end to end through the concrete queue. Collecting into a buffer leaves one
virtual call per completion and keeps the push inlined; across five matched samples
per build it is within run-to-run noise (baseline mean 308.3 ns/task, after 314.1,
with the two ranges overlapping and the medians favouring the new code).

The five remaining downcasts in `src/scheduler/scheduler.cpp` are deliberately
left. Each reaches for a different `Node` member — `offload_state`, `is_sentinel`,
`bindings_`, `metadata.numa_affinity`, `sycl_deps` — and virtualising all of them
would turn `Task` into an interface that encodes every graph concern, which trades
one coupling for a worse one. They are confined to a `.cpp`, so no header
propagates the dependency.

---

## API surface (before the v1.0.0 freeze)

**Fluent API dependency tracking — RESOLVED (Option A)**

Every launch shape now has a tracked overload — `add(Reads, Writes, n, range_fn)`,
`add(Reads, Writes, raw_kernel)` and `add(Reads, Writes, NdRange<N>, fn)` were the
three that were missing — and the five dependency-blind overloads are spelled
`add_untracked(...)`. Inference was never possible (a per-element callable captures
its USM pointers by value, so its type says nothing about what it touches), so
Option A is delivered as "always offer a tracked form, and make the untracked one
name itself" rather than as automatic detection. Rationale and the sweep are in
[`PHYSICS_SUBSTRATE_IMPLEMENTATION.md`](PHYSICS_SUBSTRATE_IMPLEMENTATION.md) §1.

**`Core::Result<T,E>` as the permanent public type — DECIDED, keep**

`Core::Result<T,E>` was introduced as a C++17 shim for `std::expected`, and the
keep-vs-migrate question was settled as BB-4.5: `Result` is the official forever
type, so the public return shape never breaks when the C++ standard is raised. Its
shape (`operator bool`, `.value()`, `.error()`, `*`) stays identical to
`std::expected`, which keeps a later internal migration cheap without making it a
public event. See `BACKBONE_WAVE_STATUS.md` §1.

---

## Release hygiene (before the v1.0.0 tag)

- **CMake version.** Done for now: `CMakeLists.txt` says
  `project(SushiRuntime VERSION 0.2.0)`, `include/SushiRuntime/version.hpp` agrees
  (`SUSHIRUNTIME_VERSION_STRING "0.2.0"`), and the tag `v0.2.0` exists. The bump to
  `1.0.0` plus a matching tag is the last step of the freeze, not open work now.
- **Reserved `RunReport` fields — done.** In
  `include/SushiRuntime/core/run_report.hpp`, the five genuinely unpopulated fields
  (`local_memory_allocations_bytes`, `tasks_received_from_network`,
  `network_bytes_tx`, `network_bytes_rx`, `network_latency_wait_ms`) now say
  "Reserved: always 0 in v1". The other three are populated and their Doxygen says
  what they measure rather than calling them reserved: `scheduling_overhead_ms`
  from `src/execution/execution_engine.cpp`, and `host_to_device_bytes` /
  `device_to_host_bytes` from the fluent layer in
  `include/SushiRuntime/api/graph/graph.hpp` — the assignment is in a header, not
  under `src/`, which is why a grep limited to `src/` reads as if nothing sets
  them. `Integration_Observability.ExplicitTransfersAreAccounted` covers both.

**Doxygen conformance sweep.** Every public function in `include/SushiRuntime/`
must conform to the rule in `CONTRIBUTING.md §4`: state why and how in plain
English, document every `@param` and `@return`, no separator comments. Do this as a
single behavior-free pass (no logic changes) before the API surface is frozen.

---

## Distributed layer — production hardening

The distributed core is complete and all tests pass. The following gaps are
documented in `DISTRIBUTED_GUIDE.md §12` as v1 limitations; they do not block the
tag but should remain accurately documented.

1. **Reconnect — still open.** A worker that drops mid-run cannot rejoin. The
   worker-death fallback covers the failure, but nothing re-establishes the TCP
   connection; there is no reconnect path anywhere in `src/distributed/`.
2. **Shutdown race — FIXED.** `TcpTransport::stop()`
   (`src/distributed/tcp_transport.cpp:440`) flips `running` to false, then calls
   `shutdown_sock()` on the listen socket and every peer socket *before* joining
   the accept and reader threads, and only closes the descriptors afterwards. A
   master waiting on `accept()` for workers that never arrive now wakes and exits.
3. **Pipelining — still open.** Communication and compute do not overlap. The
   worker has one job queue guarded by `job_cv`
   (`src/distributed/worker_agent.cpp:63`, waited on at line 525), so task N+1 is
   not received while N runs.
4. **Worker scheduler bypass — still open.** The worker hardcodes `ctx.get_queue(0)`
   for allocation, free and device introspection (`worker_agent.cpp` lines 253, 267,
   350, 479, 541), bypassing its own NUMA-aware scheduler.
5. **`TcpTransport` frame header — partly fixed.** The 5-byte header still carries a
   native-endian `uint32_t` length (`memcpy` at lines 216 and 251, no `htonl`/`ntohl`),
   so the protocol assumes a homogeneous-endianness cluster. It does now reject a
   frame larger than `MAX_FRAME_BYTES` and drop the peer (line 222), which closes
   the blind-allocation OOM.
6. **Unsynchronised `peers` vector — still open.** `accept_loop()` writes
   `peers[i]` (line 283) with no lock while `send()` (line 484) and `stop()`
   (line 450) read the same slots from other threads. Benign in practice on the
   platforms in use, but it is a data race.
7. **Endpoint configuration — still open.** Cluster addresses are direct CLI flags
   (`--master`/`--port`/`--workers`); `nodes.txt` appears nowhere in `src/`,
   `include/` or `cli/`.

---

## Lower-priority structural debt

**#34 — Sentinel injection in `compile()` instead of `ExecutionEngine`**

`TaskGraph::compile()` links leaf nodes to a sentinel and returns a
`CompiledGraph`. The cleaner split would move sentinel injection into
`ExecutionEngine::execute()` so `compile()` returns a pure DAG. The current design
is correct; this is structural debt only.

**#36 — Host task execution — the original claim was wrong**

This item used to say `ExecutionMode::HOST` tasks are submitted through
`q.submit()` + `sycl::host_task` and pay SYCL submission overhead. They are not.
`src/scheduler/scheduler.cpp:810` branches on the mode and calls
`task->execute_host(q, deps)` directly — no handler, no submit. `Node`'s override
(`include/SushiRuntime/graph/node.hpp:614`) just invokes the stored
`host_work(q, deps)` callable and returns whatever event that produces, or a
default-constructed `sycl::event{}` when there is no callable. There is no SYCL
submission on the host path.

The real trade-off is the opposite one. Host work runs *inline on the worker
thread* that dispatched it, so it occupies a worker for its whole duration and
cannot overlap with anything else that worker could have run. The dispatcher
compares the returned event against `sycl::event{}`
(`scheduler.cpp:834`); an empty event means "already finished", and the entry is
pushed to the poller flagged ready so successors fire without fabricating a
barrier. A `host_work` lambda that does its own `queue.submit()` returns a real
event instead and is polled normally.

`SushiRuntime::detail::enqueue_host_task` in
`include/SushiRuntime/core/sycl_compat.hpp` is a separate, still-used facility —
roughly twenty call sites across `tests/` and `examples/` build host work *with*
`sycl::host_task` / `AdaptiveCpp_enqueue_custom_operation` when they want a real
event. It is not dead code and is not what the scheduler uses.

Nothing to fix here; the entry is kept so the false claim does not come back.

**#37 — No DAG cancellation — RESOLVED**

`Graph::cancel()` / `DynamicGraph::cancel()` exist (`api/graph/graph.hpp`,
`api/graph/dynamic_graph.hpp`), routing to `ExecutionEngine::cancel()`: the active
step drains without launching further work and the pool stays re-armable. Covered
by `Integration_Cancellation`.

---

## Future work (out of v1 scope)

**Dynamic-graph boundary refinements.** The dynamic region graph composes in
O(changed region + affected boundary pins). Sub-range boundary ordering is now
done — the entry/exit pins carry their byte intervals and every hazard test is an
interval overlap, so two regions writing disjoint element ranges of one buffer are
no longer serialized. One refinement remains: distributed offload is not stamped
for dynamic-graph nodes (the dynamic composer bypasses `TaskGraph::compile`, where
`stamp_offloadable` runs), so an offloadable op recorded on a region runs its local
fallback under `SR_ENABLE_DISTRIBUTED` instead of shipping to a worker. It does not
block the single-node real-time target.

**Python bindings — ABANDONED, not deferred.** Decided 2026-06-25 and recorded as
L15 in [`SIMULATION_ENGINE_SUBSTRATE_PLAN.md`](SIMULATION_ENGINE_SUBSTRATE_PLAN.md):
the audience is industry, not researchers, so a binding layer would only cost time.
The C++ fluent API is the one and only frontend. This entry previously described a
pybind11 wrapper as future work, which contradicted the locked decision. Nothing
here is planned; the architecture should not preclude a later scripting layer, but
none is built.

**C++ standard migration.** The project targets C++17 for toolchain portability.
When the SYCL toolchain baseline supports it, features can be restored in two
stages: concepts, `std::span`, `std::barrier`, `std::format` and
`std::source_location` at C++20; `std::expected` and `std::println` at C++23. The
per-feature checklist this entry used to link (`docs/guides/CPP_STANDARD_MIGRATION.md`)
does not exist — there is no `docs/guides/` directory and no file of that name
anywhere in the tree — so the link is removed rather than repointed. The shims
that stand in today are listed in `CONTRIBUTING.md §4` (`core/bit.hpp`,
`core/span.hpp`, `core/result.hpp`); note that `Result<T, E>` is a permanent type
by the BB-4.5 decision above, not a migration target.

