# SushiRuntime — Open Work

---

## Correctness (blockers before v1.0.0)

**#29 — ThreadLocalMagazine teardown ordering**

`ThreadLocalMagazine` is `thread_local`. Its destructor tries to return live USM
slots to the owning `BitmaskMemoryPool`. If `RuntimeContext` (and therefore the
allocator that owns those pages) has already been destroyed, this is a
use-after-free. Either prove that `RuntimeContext::shutdown()` drains all threads
before the allocator is torn down, making the destructor safe by ordering, or add
a liveness check in the destructor. This is a correctness risk; it must be resolved
before v1.0.0.

**#30 — EventPolling 256-event cap**

`ActiveEventList` has a fixed capacity of `MAX_EVENTS = 256`. When the list is
full and a new event arrives, the overflow path logs a warning and drops the event.
A dropped event's successors never become ready — a silent, permanent deadlock on
graphs with more than 256 concurrent in-flight events. Inspect the overflow path
and either: (a) block until a slot is free, or (b) grow the list dynamically. If
the current behavior is already safe, document it in code. If it is not, this is a
correctness bug.

**#33 — EventPolling `Task*` → `Node*` downcast**

`EventPolling` uses `is_graph_node()` to guard a `static_cast<Node*>` from a
`Task*`. This couples the scheduling hot path to a derived type. Inspect
`src/scheduler/scheduler.cpp`: if the cast is still there, add a virtual
`on_complete()` to `Task` so `EventPolling` stays type-blind and `Node` handles its
own successor release. If the code was already changed differently, close this item.

---

## API surface (before the v1.0.0 freeze)

**Fluent API dependency tracking — choose one option**

`API::Graph::add()` calls `add_node()` without read/write pointer lists. Tasks
added through the fluent API do not participate in automatic RAW/WAR/WAW dependency
detection unless a handle-based overload is used. The current state is undocumented,
which means a user who follows the README's "intended entry point" and expects
ordering will get silent mis-ordering on raw kernels. Pick one:

- **Option A.** Add `add(Reads{…}, Writes{…}, …)` overloads to `API::Graph` that
  forward pointer lists to `add_task()`. Makes dependency tracking work through
  the fluent path unconditionally.
- **Option B.** Document explicitly in `INTRODUCTION.md` and `README.md` that the
  fluent API does not infer dependencies from raw kernels and direct users who need
  it to `TaskGraph::add_task()`. The handle-based overloads (`In`, `Out`, `InOut`,
  `Reads`, `Writes`) already register accesses correctly; only the raw kernel
  overloads are silent.

Neither option involves a behavior change to the scheduler; both are documentation
or thin API additions.

**`Core::Result<T,E>` as the permanent public type**

`Core::Result<T,E>` was introduced as a C++17 shim for `std::expected`. A decision
is still open: keep `Result` as the official forever type (so the return shape never
breaks when the C++ standard is raised), or plan a migration to `std::expected` at
C++20/23. The public shape (`operator bool`, `.value()`, `.error()`, `*`) was kept
identical to `std::expected` to make either direction cheap. Document the decision
before the freeze.

---

## Release hygiene (before the v1.0.0 tag)

- **CMake version.** `project(SushiRuntime VERSION 0.1.0)` → `VERSION 1.0.0`.
  Create a matching git tag.
- **Orphaned comment.** `include/SushiRuntime/graph/task_graph.hpp` line 161
  contains a commented-out field that is a refactoring artifact. Remove it.
- **Reserved `RunReport` fields.** `scheduling_overhead_ms`,
  `network_latency_wait_ms`, `host_to_device_bytes`, `device_to_host_bytes`,
  `tasks_received_from_network`, `network_bytes_tx`, `network_bytes_rx` are always
  zero. Each should carry `@note Reserved: always 0 in v1.` in its Doxygen so users
  reading the header directly understand why the measurement is absent.
- **`SR_BUILD_TESTS` default.** The CMake option defaults to `OFF`, so a direct
  `cmake` invocation (without the CLI) produces a library-only build with no test
  binary. Either flip the default to `ON` or add an explicit note in the
  "Building with CMake directly" section of the README.
- **`cpp_standard_migration.md` location.** Move
  `docs/slop/cpp_standard_migration.md` to `docs/guides/CPP_STANDARD_MIGRATION.md`
  and add a reference from `CONTRIBUTING.md`. It is the authoritative per-feature
  checklist for restoring C++20 and C++23 facilities and should not be buried in
  planning artifacts.

**Doxygen conformance sweep.** Every public function in `include/SushiRuntime/`
must conform to the rule in `CONTRIBUTING.md §4`: state why and how in plain
English, document every `@param` and `@return`, no separator comments. Do this as a
single behavior-free pass (no logic changes) before the API surface is frozen.

---

## Distributed layer — production hardening

The distributed core is complete and all tests pass. The following gaps are
documented in `DISTRIBUTED_GUIDE.md §12` as v1 limitations; they do not block the
tag but should remain accurately documented.

1. **Reconnect.** A worker that drops mid-run cannot rejoin. The worker-death
   fallback covers the failure, but the TCP connection is not re-established.
2. **Shutdown race.** The master's `accept()` loop blocks indefinitely if fewer
   workers than expected connect before `stop()` is called (self-pipe or
   non-blocking accept would fix this).
3. **Pipelining.** Communication and compute do not overlap: task N+1 is not
   transmitted while the worker runs N.
4. **Worker scheduler bypass.** Offloaded tasks are dispatched directly to
   `get_queue(0)` on the worker, bypassing the worker's NUMA-aware scheduler.
5. **`TcpTransport` minor issues.** Native-endian length prefix (homogeneous-cluster
   assumption); light data race on the `peers` vector between accept and send
   (benign in practice).
6. **Endpoint configuration.** Cluster addresses are direct CLI flags
   (`--master`/`--port`/`--workers`); there is no `nodes.txt` file support.

---

## Lower-priority structural debt

**#34 — Sentinel injection in `compile()` instead of `ExecutionEngine`**

`TaskGraph::compile()` links leaf nodes to a sentinel and returns a
`CompiledGraph`. The cleaner split would move sentinel injection into
`ExecutionEngine::execute()` so `compile()` returns a pure DAG. The current design
is correct; this is structural debt only.

**#36 — Host tasks go through SYCL `host_task`**

Tasks with `ExecutionMode::HOST` are submitted via `q.submit()` +
`sycl::host_task` (or `AdaptiveCpp_enqueue_custom_operation` on AdaptiveCpp).
This adds SYCL submission overhead to plain CPU work. Whether bypassing SYCL for
host work matters at v1 workload sizes is a judgment call. Document the current
behavior and the trade-off.

**#37 — No DAG cancellation**

Once a graph is submitted it runs to completion. An `atomic<bool>` cancellation
flag on `RuntimeContext`, checked by workers and `EventPolling`, would let a caller
abort a long-running graph. Not needed for v1 use cases, but a known gap for
error-recovery or interactive scenarios.

---

## Future work (out of v1 scope)

**Python bindings.** A pybind11 (or nanobind) wrapper around the C++ fluent API
would allow direct interop with NumPy, PyTorch, and Jupyter. The binding entry
point would be `python/src/module.cpp` + `bind_runtime.cpp` + `bind_graph.cpp`,
gated by a `SR_BUILD_PYTHON_BINDINGS` CMake option. Zero-copy data transfer via
the Python Buffer Protocol or DLPack is the performance-critical piece. Not started.

**C++ standard migration.** The project targets C++17 for toolchain portability.
When the SYCL toolchain baseline supports it, features can be restored in two
stages. See `docs/guides/CPP_STANDARD_MIGRATION.md` for the per-feature checklist
(concepts, `std::span`, `std::barrier`, `std::format`, `std::source_location` at
C++20; `std::expected`, `std::println` at C++23).

