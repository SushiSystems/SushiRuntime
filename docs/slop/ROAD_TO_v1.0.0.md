# Road to v1.0.0

This document consolidates the open items from `docs/slop/` against the current
state of the project, records which planned work was implemented (and how), flags
items that diverged from the original plan, and adds findings from a direct review
of the current code and documentation.

Sources read: `TODO.md`, `v1_api_plan.md`, `Idea.md`, `build_system_issues.md`,
`CLI_REDESIGN.md`, `INSTALLER_PLAN.md`, `cpp_standard_migration.md`,
`DOCS_UPDATE_MAP.md`.

---

## 1. Validated as Implemented

The following items from the planning documents are confirmed in the current code
and documentation.

### Build system (`build_system_issues.md`, `v1_api_plan.md` Work Package 1)

- Hardcoded `C:/vcpkg` replaced with `VCPKG_ROOT` env var; fallback to `C:/vcpkg`
  only if it exists on disk. No drive path is forced.
- Intel oneAPI root resolved from `CMPLR_ROOT`; no hardcoded `Program Files` path.
- `--gcc-toolchain` is `SR_GCC_TOOLCHAIN` (CMake option, default `/usr`, settable
  empty to disable), not a hardcoded version-specific path.
- `pkgconf.exe` path resolved relative to `VCPKG_ROOT/installed/x64-windows/tools`,
  not hardcoded.
- CUDA compute capability is `SR_CUDA_ARCH` (default `61`, configurable per target
  GPU). The old hardcoded `sm_61` is gone.
- Three SYCL toolchains: `intel-llvm` (primary), `adaptivecpp` (secondary),
  `oneapi` (supported for profiling). `SR_SYCL_TOOLCHAIN` selects; auto-inferred
  from the compiler name if a compiler is provided.

### Error infrastructure (`v1_api_plan.md` Work Package 2)

- `Errc` enum, `Error` struct, `SushiException`, and `Result<T, E>` implemented
  in `core/`.
- `run()` throws `SushiException`; `try_run()` returns `Result<RunReport, Error>`
  without throwing.
- `RunReport` struct: timing, steal statistics, per-NUMA-node breakdown, and
  distributed statistics.
- SYCL async exception handler wired into queue construction so device faults land
  in the `Error` channel rather than being silently dropped.

### Fluent API (`v1_api_plan.md` Work Package 3)

- `Runtime::create()` factory and `API::Graph` chainable builder implemented in
  `include/SushiRuntime/api/runtime.hpp`.
- Three `add()` overloads: per-element (`fn(size_t i)`), range
  (`fn(size_t begin, size_t end, sycl::handler&)`), raw kernel
  (`fn(sycl::handler&)`). All return `*this` for chaining.
- `run()` and `try_run()` compile and execute the graph, return `RunReport`.
- `API::Runtime`, `Core::Error`, `Core::Errc`, `Core::RunReport`,
  `Core::SushiException` promoted to the `SushiRuntime` namespace.

### Scheduler and memory (`TODO.md` items 1–28, all marked COMPLETED)

All 28 original issues are implemented. Key confirmed items:

- Persistent worker pool; `arm_for_execution()` re-arms between runs with no
  thread churn.
- NUMA-aware hierarchical stealing: `victim_order_[]` pre-built from the distance
  model; local victims are tried before remote ones.
- `BitmaskMemoryPool` with `ThreadLocalMagazine` hot path, `LOW_WATERMARK`
  proactive refill, TCMalloc-style bulk-claim CAS (up to 64 slots per CAS).
- Remote-free MPSC routing: frees of foreign pointers return to the owning slot's
  queue, not the freeing thread's magazine.
- EBR with thread-exit orphan handoff: retiring threads donate their `retire_list`
  to `global_orphan_list_` for surviving threads to drain.
- 1024 `DependencyTracker` shards (SplitMix64 hash), edge deduplication; RAR
  stays parallel.
- MPMC injector for external submissions, separate from per-worker Chase-Lev deques.
- `IAllocatorStrategy` (Strategy pattern); `Strategies::Shared/Device/Host`.
- Priority queue in the injector (`priority_submissions`) for high-priority bypass.
- Adaptive backoff in `EventPolling` (up to `MAX_BACKOFF = 64`, yields past 16).
- AdaptiveCpp lazy-event stall detection (`STALL_DRIVE_THRESHOLD = 32`).
- Topology injection seams: `ITopologyProvider`, `INumaDistanceModel`,
  `RemappingTopologyProvider`, `MatrixDistanceModel` for deterministic multi-NUMA
  testing on single-socket hardware.

### CLI (`CLI_REDESIGN.md`, `INSTALLER_PLAN.md`)

- Typer + rich Python CLI package; `sr` and `sushiruntime` on PATH via
  pipx/pip entry-points; `pyproject.toml`-based.
- `sr toolchain` selects SYCL toolchain; persists to `cli/config.local.toml`.
- Full command tree: `sr project build/test/run/clean/doxygen`, `sr docker
  build/run`, `sr setup detect/install/configure/verify/all`, `sr config show`,
  `sr env dump`.
- CTest label-based test suites: `unit`, `regression`, `integration`,
  `functional`, `stress`, `performance`, `benchmark`, `all`.
- `cli/dependencies.toml` single declarative manifest; installer reads it per
  platform. Adding a dependency means adding one TOML block.
- SOLID installer pipeline: `DetectStep → InstallDepsStep → ConfigureStep →
  VerifyStep`, each an independent `Step(ABC)` driven by injected
  `IPackageManager` and `IDependencySource`.
- `install.sh` and `install.ps1` at repo root; both work when piped from curl/irm.
- `--force` flag falls back to `clang++/clang` if the selected toolchain compiler
  is not found.

### Distributed layer (`Idea.md` distributed tracker)

Core is complete and all planned tests pass:

- Protocol, `ByteWriter/ByteReader`, `InProcessFabric`/`TcpTransport`,
  `KernelRegistry`, `BufferRegistry`, `DistributedCoordinator`, `WorkerAgent`.
- Worker liveness monitor (5 s default); heartbeat (1 s default); re-dispatch
  to surviving workers or local fallback on worker death.
- RTT/free-mem PING/PONG; `LeastLoadedOffloadPolicy` ranks by estimated
  completion cost (RTT + transfer time + queue backlog).
- Version-bump coherence: local writes auto-bump the registered buffer version;
  stale worker caches pull on miss.
- Tests: protocol unit, transport unit, registry unit, SYCL E2E (single offload,
  auto version-bump after local write, 2-worker fan-out, TCP loopback).

---

## 2. Decisions That Diverged from the Plan

These items were decided differently in `v1_api_plan.md` compared to what was
actually implemented, for reasons recorded in `cpp_standard_migration.md` and
memory notes.

| Topic | Plan | Current state | Reason |
|---|---|---|---|
| C++ standard | C++23 | **C++17** | Portability across all three SYCL toolchains including the CUDA nvptx64 device pass. Documented per-feature in `cpp_standard_migration.md`. |
| `std::expected` | `std::expected<T, E>` | `Core::Result<T, E>` shim | Follows from C++17. Public shape (`operator bool`, `.value()`, `.error()`, `*`) is kept identical to `std::expected` for a clean upgrade later. |
| Concepts | `template <PerElement F> concept ...` | SFINAE `is_*_v` traits + `enable_if_t` | Follows from C++17. Diagnostics are "no matching call" instead of a concept error, but semantics are identical. |
| Primary toolchain | AdaptiveCpp primary | **intel-llvm primary**, AdaptiveCpp secondary | intel-llvm provides the widest backend coverage and is what the Docker image and CI use. AdaptiveCpp is fully supported as a secondary path with its own test runner. |
| `std::source_location` in `Error` | `Error` had a `where` field with `source_location::current()` | Dropped; logger uses `__FILE__`/`__LINE__`/`__func__` macros | `std::source_location` is C++20. Will be restored when the standard is raised. |
| Umbrella header `sushi.hpp` | Single `#include <SushiRuntime/sushi.hpp>` entry point | **Not implemented** | See Outstanding §3. |

The full per-feature migration map (what to restore and where when C++20 or C++23
is adopted) is in `docs/slop/cpp_standard_migration.md`. That file should be moved
to a permanent location before v1.0.0 — see Outstanding §3.

---

## 3. Outstanding — from Planning Documents

### `TODO.md` PENDING items (#29–37)

**#29 — ThreadLocalMagazine destructor safety on thread exit**

`ThreadLocalMagazine` is `thread_local`. On thread exit, its destructor attempts
to return live USM slots to the owning `BitmaskMemoryPool`. If `RuntimeContext`
(and thus the allocator that owns those pages) has already been destroyed, this is
a use-after-free. EBR has the orphan handoff for `RetiredNode` objects, but there
is no equivalent gate in the magazine destructor. Before v1.0.0, either verify that
`RuntimeContext::shutdown()` guarantees all threads complete before the allocator is
torn down (making the destructor safe by ordering), or add a liveness check in the
magazine destructor. This is a correctness risk and needs explicit verification or
a fix.

**#30 — EventPolling `MAX_EVENTS = 256` fixed capacity**

`ActiveEventList` has a fixed 256-slot capacity with O(1) swap-remove. What happens
when all 256 slots are occupied and a new kernel submission arrives is not documented
in the architecture. If the overflow path drops the event (as the original TODO
described), that node's successors never unblock — a silent deadlock on graphs with
more than 256 concurrent in-flight events. Before v1.0.0, the overflow path must be
confirmed as either: (a) block until a slot frees, or (b) dynamically extend the
list. If the current behavior is already safe, add a code comment. If it is not,
this is a correctness bug.

**#31 — `rand()` global lock in work-stealing** → **Resolved.** The current
implementation uses `victim_order_[]`, a pre-built NUMA-sorted victim list walked in
order. Random victim selection with `rand()` is not used.

**#32 — `shared_mutex` in deallocation path** → **Resolved.** Remote frees are
routed to the owning slot's MPSC queue; the hot deallocation path does not acquire
a shared read-lock.

**#33 — LSP violation: EventPolling casts `Task*` to `Node*`**

The original plan flagged that `EventPolling` uses `is_graph_node()` to guard a
`static_cast<Node*>` from a `Task*`, which couples the core scheduling path to a
derived type. The proposed fix was a virtual `on_complete()` on `Task` so
`EventPolling` stays type-blind. This has not been verified against the current
`src/scheduler/scheduler.cpp`. If the cast is still present as described,
the architectural concern stands; if the code was refactored differently, record
what actually changed and close this item.

**#34 — SRP: sentinel node injected in `compile()`, not `ExecutionEngine`**

`TaskGraph::compile()` currently links leaf nodes to a sentinel and returns a
`CompiledGraph`. The plan proposed moving sentinel injection into
`ExecutionEngine::execute()` so `compile()` returns a pure DAG. This was not done.
The current design is correct; this is a structural cleanliness issue, not a
correctness bug. Low priority for v1.0.0 but worth recording as technical debt.

**#35 — `BitmaskMemoryPool` statelessness** → **Likely resolved.** The current
architecture describes `BitmaskMemoryPool` as having `MemoryPage` state and owning
the thread-local magazine backing store. This should be confirmed against
`include/SushiRuntime/memory/memory_pool.hpp` and closed if the pool has non-trivial
member state.

**#36 — HOST tasks submitted through SYCL `host_task`, not a pure native thread**

Host tasks currently go through `q.submit()` with `host_task` (or
`AdaptiveCpp_enqueue_custom_operation` on AdaptiveCpp via `sycl_compat.hpp`).
This requires a live SYCL queue and adds SYCL submission overhead to what is
plain CPU work. `RuntimeContext` construction requires at least one queue, so the
"no queues available" failure path from the original TODO is largely mitigated.
Whether bypassing SYCL for host work matters at v1.0.0 workload sizes is a
judgment call. Low priority; document the current behavior and note the trade-off.

**#37 — No DAG cancellation mechanism**

No cancellation token exists. Once a graph is submitted it runs to completion (or
hangs if EventPolling deadlocks under #30). An `atomic<bool> is_cancelled` on
`RuntimeContext` checked by workers and `EventPolling` would address this. Not a
v1.0.0 blocker for the expected single-graph use cases, but a known gap that
matters for long-running or error-recovery scenarios.

---

### `v1_api_plan.md` open items

**Umbrella header `sushi.hpp`**

A single `#include <SushiRuntime/sushi.hpp>` entry point was planned. Users
currently include `api/runtime.hpp` for the fluent API and additional headers for
the lower-level API. This is low effort to add and noticeably improves the
first-use experience.

**Work Package 4: Doxygen conformance sweep**

`CONTRIBUTING.md` now documents the Doxygen rule (why + how + every `@param` /
`@return`, no historical references). The pass across all existing public headers
in `include/SushiRuntime/` to bring every function into conformance has not been
done as a dedicated behavior-free commit. This is a code quality requirement before
v1.0.0 freezes the API surface.

---

## 4. Additional Concerns (Not in Planning Documents)

These issues come from a direct review of the current code and documentation.

### 4.1 Fluent API has no dependency tracking

`API::Graph::add()` calls `tg_->add_node()` without read/write pointer lists.
The fluent API does not perform implicit RAW/WAR/WAW dependency tracking — the
core feature of SushiRuntime. Tasks added through the fluent API execute in an
unspecified order unless the underlying SYCL event chain provides ordering. A user
who follows the documented "intended entry point" (`Runtime::create()` / `g.add()`)
and expects automatic dependency detection will get silent mis-ordering.

Two options, both acceptable for v1.0.0, but one must be chosen:

- **Option A:** Add `add(reads, writes, n, fn)` overloads to `API::Graph` that call
  `add_task()` with pointer lists. This is the correct fix if the fluent API is
  the intended primary path.
- **Option B:** Document clearly in `INTRODUCTION.md` and `README.md` that the
  fluent API does not track dependencies, and direct users who need dependency
  tracking to the `TaskGraph::add_task()` API. Demote the fluent API to a
  convenience layer for graphs where ordering does not matter or is controlled
  manually via SYCL events.

Leaving the current state undocumented is not an option.

### 4.2 CMake project version is 0.1.0

`project(SushiRuntime VERSION 0.1.0)` does not match the v1 positioning in the
documentation. Before a v1.0.0 release: bump to `VERSION 1.0.0` in
`CMakeLists.txt` and create a corresponding git tag.

### 4.3 `SR_BUILD_TESTS` defaults to OFF

`option(SR_BUILD_TESTS ... OFF)` in `CMakeLists.txt`. The CLI passes `ON` by
default, so this only affects users who drive CMake directly — they get a
library-only build with no test binary. The README's "Building with CMake directly"
section does not mention this. Either flip the default to `ON`, or add an explicit
note in the direct-CMake section.

### 4.4 `RunReport` reserved fields carry no data

`scheduling_overhead_ms`, `network_latency_wait_ms`, `host_to_device_bytes`,
`device_to_host_bytes`, `tasks_received_from_network`, `network_bytes_tx`,
`network_bytes_rx` are always `0`. `ARCHITECTURE.md` documents them as reserved,
but the struct Doxygen in `run_report.hpp` does not. Each reserved field should
have a `@note Reserved: always 0 in v1.` line so users reading the header directly
understand why the measurement is absent.

### 4.5 Orphaned comment in `task_graph.hpp`

`include/SushiRuntime/graph/task_graph.hpp` line 161:

```cpp
// SushiRuntime::Execution::RuntimeContext& ctx_; // Not used in implementation
```

This is a refactoring artifact visible in the public header. Remove it.

### 4.6 Distributed layer production gaps

The distributed layer is correct for all tested scenarios. The following gaps
are documented in `DISTRIBUTED_GUIDE.md §12` and remain as v1 limitations:

- **Reconnect:** a worker that drops mid-run cannot rejoin; the worker death
  fallback covers the failure, but the TCP connection is not re-established.
- **Shutdown race:** master `accept()` may block indefinitely if fewer workers
  than expected connect before `stop()` is called.
- **Worker scheduler bypass:** offloaded tasks are dispatched directly to
  `get_queue(0)` on the worker, bypassing the local NUMA-aware scheduler.
- **No transport security:** TCP transport is bare sockets for a trusted LAN.
- **Homogeneous cluster only:** raw trivially-copyable serialization assumes
  the same architecture and endianness on every node.

These do not need to be fixed before v1.0.0. They do need to remain accurately
documented. The `DISTRIBUTED_GUIDE.md §12` section already covers them.

### 4.7 `cpp_standard_migration.md` needs a permanent home

`docs/slop/cpp_standard_migration.md` is the authoritative per-feature checklist
for restoring C++20 and C++23 facilities when the toolchain baseline improves.
It should be moved to `docs/guides/CPP_STANDARD_MIGRATION.md` and referenced from
`CONTRIBUTING.md` so it is not lost among planning artifacts.

---

## 5. Pre-v1.0.0 Checklist

In priority order.

| Priority | Item | Notes |
|---|---|---|
| **Blocker** | Decide fluent API dependency tracking (§4.1) | Choose Option A or B; one of the two must be done before release |
| **Blocker** | Bump CMake version to 1.0.0 + git tag (§4.2) | |
| **Blocker** | Confirm EventPolling overflow path is safe (#30) | Code inspection; fix or document |
| **Blocker** | Verify ThreadLocalMagazine teardown order (#29) | Proof or fix |
| **Should** | `sushi.hpp` umbrella header | Single-include entry point; low effort |
| **Should** | Doxygen conformance sweep across `include/` | One behavior-free pass before API freeze |
| **Should** | `SR_BUILD_TESTS` default — flip or document (§4.3) | |
| **Should** | Reserved `RunReport` fields: add `@note Reserved` to each (§4.4) | |
| **Should** | Move `cpp_standard_migration.md` to `docs/guides/` (§4.7) | |
| **Should** | Remove orphaned comment in `task_graph.hpp` (§4.5) | |
| **Should** | Inspect `src/scheduler/scheduler.cpp` for `is_graph_node()` cast (#33) | Close or track |
| **Low** | Sentinel injection in `compile()` vs `ExecutionEngine` (#34) | Structural debt, not a bug |
| **Low** | Host tasks through SYCL `host_task` vs native thread (#36) | Document current behavior |
| **Low** | DAG cancellation mechanism (#37) | Not needed for v1 scope |
| **Doc only** | Distributed layer limitations already in `DISTRIBUTED_GUIDE.md §12` | Verify they are current |
