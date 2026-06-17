# SushiRuntime Architecture

This document describes how SushiRuntime is built, layer by layer, down to the
data structures and concurrency techniques that make it work. It is written for
contributors and reviewers who need an accurate mental model of the runtime —
every class, constant, and algorithm named here exists in the headers under
`include/SushiRuntime/`.

> If you only want to *use* the library, read [`INTRODUCTION.md`](INTRODUCTION.md)
> first. This document is about how the inside works.

---

## 1. What SushiRuntime is

SushiRuntime is a C++23 runtime library for **task-based parallel computing on
heterogeneous hardware** (CPU + GPU) via SYCL. You describe work as a graph of
tasks and declare the memory each task reads and writes; the runtime infers a
safe execution order, then runs the independent parts in parallel across the
devices it discovers.

Four design commitments shape every layer:

1. **Implicit dependencies.** Tasks declare read/write pointers, not edges. The
   runtime detects read-after-write (RAW), write-after-read (WAR), and
   write-after-write (WAW) hazards and serializes only what must be serialized.
2. **Topology awareness.** Hardware (CPUs, GPUs, NUMA nodes) is discovered and
   devices are mapped to the NUMA node they are physically attached to, so work
   and the memory it touches can stay close.
3. **Lock-free hot paths.** Scheduling, dependency resolution, reference
   counting, and memory reclamation use atomics — not mutexes — wherever
   contention matters.
4. **Zero-cost optionality.** The distributed offload layer and the CUDA backend
   are compile-time options that add nothing to a standalone build when off.

---

## 2. The layered architecture

The code is organized as a stack of layers under `include/SushiRuntime/` (public
API) and `src/` (implementations). Higher layers depend on lower ones; lower
layers never depend up.

```
        ┌─────────────────────────────────────────────────────────┐
        │  execution/   RuntimeContext · IExecutionEngine           │  owns everything,
        │               ExecutionEngine · CompiledGraph             │  drives a run
        ├─────────────────────────────────────────────────────────┤
        │  graph/       TaskGraph · Node · DependencyTracker        │  build + analyze DAG
        ├─────────────────────────────────────────────────────────┤
        │  scheduler/   TaskScheduler · Task/RangeTask              │  run ready tasks
        │               EventPolling · WorkloadCoordinator          │  (work stealing)
        ├──────────────────────┬──────────────────────────────────┤
        │  memory/             │  topology/                         │  resources +
        │  USMAllocator        │  HardwareDiscovery · NUMAMapper    │  hardware model
        │  BitmaskMemoryPool   │  ITopologyProvider                 │
        │  EpochManager (EBR)  │  INumaDistanceModel · affinity     │
        ├──────────────────────┴──────────────────────────────────┤
        │  core/   RefCounted · sushi_ptr · logger · common · OpID  │  foundations
        └─────────────────────────────────────────────────────────┘
           distributed/ (optional, SR_ENABLE_DISTRIBUTED) sits beside
           execution/scheduler and is invisible when the flag is off.
```

The **dependency-inversion seams** are deliberate: `RuntimeContext` accepts an
injected `ITopologyProvider` and `INumaDistanceModel`; the scheduler exposes
`ITelemetryProvider` / `ITaskMigrator` / `IRebalanceStrategy`; the allocator uses
`IAllocatorStrategy`; the distributed layer uses `ITransport` and
`IOffloadPolicy`. Every one of these has a production default and a
test/diagnostic substitute, which is how multi-NUMA and multi-node behavior is
exercised deterministically on a single desktop.

---

## 3. The end-to-end execution model

A program's lifecycle is always the same three phases, and they must not overlap:

```
   build the whole graph   ──►   compile()   ──►   execute()
   (Phase 1, single thread)      (Phase 2)         (Phase 3, blocks)
```

1. **Construct a `RuntimeContext`.** It probes hardware, builds a `sycl::queue`
   per device, sets up USM allocators, and starts a *persistent* worker pool
   (the `ExecutionEngine`'s `TaskScheduler`).
2. **Build a `TaskGraph`.** Each `add_task` / `add_node` call appends a `Node`
   and runs it through the `DependencyTracker`, which wires hazard edges as nodes
   arrive.
3. **`graph.compile()`.** Links leaf nodes to a sentinel completion node, finds
   the roots (in-degree 0), and transfers ownership into a `CompiledGraph`. It
   submits nothing to hardware.
4. **`ctx.get_execution_engine().execute(compiled)`.** Injects the roots into the
   scheduler, arms it, and blocks until the sentinel fires.

> **Threading rule:** Phase 1 (`add_node`/`add_task`) and Phase 2 (`compile()`)
> are **not** thread-safe relative to each other. Build the entire graph, then
> compile, then execute.

---

## 4. Core (`core/`)

The foundations every other layer is built on.

### `RefCounted` + `sushi_ptr`

Intrusive, atomic reference counting. The count lives inside the object
(`RefCounted` base), so `sushi_ptr<T>` is cheaper than `std::shared_ptr` (no
separate control block, one cache line). `make_sushi<T>(...)` constructs an owned
object; copy bumps the count atomically, move transfers it, `leak()` detaches the
raw pointer without touching the count, and `release()` drops one reference.
`Node`, `Task`, and `USMAllocator` are all `RefCounted`.

### `common.hpp` — the constants that size the runtime

| Constant | Value | Meaning |
|----------|-------|---------|
| `CACHE_LINE_SIZE` | 64 | alignment used to kill false sharing |
| `MAX_WORKER_THREADS` | 256 | upper bound on scheduler workers |
| `MAX_SUPPORTED_XPU` | 16 | device cap |
| `MAX_NUMA_NODES` | 64 | NUMA node cap |
| `DEFAULT_ALIGNMENT` | 128 | default USM alignment |

`Backend` enum: `SYCL`, `CUDA`, `HOST`. The namespace alias `sr = SushiRuntime`
is also defined here.

### `logger`

An async, thread-safe logger (`SR_LOG_INFO`, `SR_LOG_ERROR`, …) with a synchronous
mode (`SR_LOGGER_SET_SYNC_MODE(true)`, used by the test main so logs don't
interleave with GoogleTest output) and a throwing assert macro (`SR_THROW_IF`).
Log level is compile-time gated.

### `OpID` — open, hashed operation identity

`OpID` (in `graph/task_types.hpp`) is a 32-bit **FNV-1a** hash of a string,
computed at compile time via the `"..."_op` user-defined literal
(`"blas.gemm"_op`). It replaces rigid enums so any compute domain can register
named operations, and it is the *only* thing identifying a kernel across the wire
in the distributed layer (see §11).

---

## 5. Topology (`topology/`)

Discovers and models the machine so the scheduler and allocators can keep work
and memory close.

- **`HardwareDiscovery`** enumerates SYCL devices into `HardwareNode`s, recording
  `logical_id`, `numa_node_id`, the `sycl::device`, name, `is_gpu`,
  `PCIDomainAddress` (PCI BDF), `global_mem_size`, and `max_compute_units`. A
  `HardwareFilterConfig` (`include_cpu` / `include_gpu` / `include_accelerator`)
  narrows discovery.
- **`NUMAMapper`** uses **hwloc** to map each device to its nearest NUMA node by
  walking PCI topology, and reports inter-node distances on the ACPI SLIT
  convention: **10 = local, > 10 = remote** (typically 21).
- **Injection seam.** `RuntimeContext` takes an `ITopologyProvider` (device
  enumeration) and an `INumaDistanceModel` (distance source). Left null, it uses
  `HwlocTopologyProvider` + `HwlocDistanceModel`. The substitutes —
  `RemappingTopologyProvider` (spreads real devices across N synthetic NUMA nodes,
  core *i* → node *i % N*) and `MatrixDistanceModel` (a hand-built distance
  matrix) — let multi-NUMA routing and NUMA-aware stealing be tested on
  single-socket hardware. This is the same substitution pattern the distributed
  layer uses for the network (`InProcessTransport`).
- **`affinity`** pins a thread to a logical core (Windows + POSIX), best-effort:
  an invalid core logs a warning rather than throwing.

---

## 6. Memory (`memory/`)

Allocation tuned for parallel, heterogeneous workloads. Three cooperating pieces.

### 6.1 `USMAllocator` + strategies

`USMAllocator` is the public allocation facade (`allocate`, `deallocate`,
`get_device_of`). Allocation behavior is pluggable via `IAllocatorStrategy`
(Strategy pattern, for OCP):

- `Strategies::Shared` → `sycl::aligned_alloc_shared` — CPU+device visible,
  **poolable**.
- `Strategies::Device` → `sycl::aligned_alloc_device` — device-only, not pooled.
- `Strategies::Host` → `sycl::aligned_alloc_host` — host-pinned, not pooled.

`RuntimeContext::malloc_shared/malloc_device/free_usm` wrap this and additionally
**record the owning queue** for each allocation (see §9), which is what lets the
scheduler dispatch a kernel to the exact device/context that owns its data.

### 6.2 `BitmaskMemoryPool` + `ThreadLocalMagazine` — the fast path

For poolable (Shared) allocations there is a two-tier, mostly lock-free pool:

- **`ThreadLocalMagazine`** (`thread_local`) is the hot path. `NUM_BUCKETS = 4`
  size classes (**64, 256, 1024, 4096** bytes), each a stack of up to
  `BATCH_SIZE = 256` pointers. `pop` and `push` are 2–3 instructions with **no
  atomics and no locks**. A `LOW_WATERMARK` (BATCH_SIZE/4 = 64) lets the caller
  refill proactively before a bucket empties on the critical path. A
  `current_alloc_id` guards correctness when multiple `USMAllocator` instances are
  interleaved (a mismatch flushes the magazine — the "multi-allocator flush
  cliff", acceptable for the common single-allocator case).
- **`BitmaskMemoryPool`** is the backing store. Memory is carved into
  `MemoryPage`s of 64 fixed-size slots tracked by a single `uint64_t` atomic
  `bitmask`. Refilling a magazine uses **bulk claim**: `bulk_claim` takes the
  whole page in one CAS to `~0ULL` (TCMalloc-style, 64× fewer atomics);
  `bulk_claim_capped` takes only as many free bits as the magazine can absorb
  (using `std::countr_zero`/`std::popcount`), which reduces CAS contention when
  many threads refill at once.
- **Remote frees.** Every `MemoryPage` records its `owning_slot`. When a thread
  frees a pointer that belongs to *another* thread's slot, the free is routed
  back to the owning slot's MPSC remote-free queue rather than polluting the
  freeing thread's magazine. The owner lazily drains that queue on its next
  refill. This kills the "consumer magazine fills with foreign pointers → flush
  spike" pathology and is the path the `CrossThreadFreeIsReclaimed` test guards.

Requests larger than the top bucket (4096 B) bypass the pool entirely and go
straight to SYCL.

### 6.3 `EpochManager` — epoch-based reclamation (EBR)

Lock-free data structures (the dependency tracker's access history, in
particular) can't free a node the instant it's unlinked — another thread might
still be reading it. EBR solves this:

- A global epoch counter (`global_epoch_`) and one cache-line-aligned
  `ThreadEpochSlot` per thread. `EpochGuard` (RAII) publishes the current epoch on
  `enter()` and clears it on `exit()`.
- `retire(ptr)` appends to a thread-local `retire_list`. Every
  `RETIRE_THRESHOLD = 128` retires, `check_and_reclaim()` computes the minimum
  epoch any thread is still in and frees everything retired in an earlier epoch.
- `RetiredNode` storage is itself pooled per thread (`FREE_POOL_CAP = 512`) so
  retire/reclaim doesn't hit the global allocator on the hot path.
- **Thread-exit handoff:** when a thread dies, its outstanding `retire_list` is
  donated to a global orphan list (`global_orphan_list_`) and reclaimed by
  surviving threads — no leaks. (A subtle consequence, captured in a regression
  test: payloads retired near program end can be freed during a *later* test's
  reclamation, so test counters must outlive the test frame — hence `shared_ptr`
  counters in the EBR tests.)

---

## 7. Graph (`graph/`)

Builds and analyzes the task DAG.

### 7.1 `Node` — one task

`Node` derives from `Scheduler::Task` and carries:

- `TaskMetadata metadata` (name, `TaskType`, `OpID`, `priority`, `numa_affinity`,
  and up to 8 inline scalar `params`);
- the work: `TaskWork work` (`void(sycl::handler&)`, kernel mode) and/or
  `HostWork host_work` (`sycl::event(sycl::queue&, const std::vector<sycl::event>&)`,
  host mode), with an `ExecutionMode` (`KERNEL` / `HOST`);
- `std::atomic<int32_t> in_degree` — incoming dependency count;
- successors: an **inline array** of `MAX_INLINE_SUCCESSORS = 16` plus a lock-free
  CAS-pushed `successor_list` for spillover, so small fan-out is heap-free;
- `io_reads_` / `io_writes_` — the raw USM pointers, retained on **every** build
  so the scheduler can route a kernel to the queue whose context owns the data;
- `SmallEventVector sycl_deps` — SYCL event dependencies with `STATIC_CAP = 8`
  inline before spilling to a vector.

`add_successor` is lock-free: it bumps an atomic index into the inline array, and
only on overflow allocates a `SuccessorNode` and CAS-pushes it onto the list.

### 7.2 `DependencyTracker` — lock-free hazard resolution

This is the heart of "implicit dependencies." It keeps a **per-pointer access
history** with no locks:

- The pointer space is **sharded** into `SHARD_COUNT = 1024` `ResourceState`s,
  each cache-line aligned (no false sharing). A pointer maps to a shard via a
  **SplitMix64** hash of its address.
- Each shard's `ResourceState` holds an atomic `tail` pointing at the most recent
  `AccessNode` (`{ task, is_write, prev }`), forming an immutable backward-linked
  history.
- `resolve(node, ptr, is_write)` walks the history to add exactly the edges
  required:
  - **RAW**: a read after a write depends on that write.
  - **WAR / WAW**: a write depends on the preceding reads *and* the preceding
    write.
  - **RAR stays parallel**: two reads of the same buffer do **not** depend on each
    other.
  - Edges are **deduplicated** (sharing several buffers between the same pair adds
    one edge) and a node never depends on itself.

`add_dependency(from, to)` increments `to`'s `in_degree` and registers `to` as a
successor of `from`. The observable is `in_degree`: the count the scheduler must
drive to zero before a node is runnable. The history nodes are reclaimed via EBR
on `reset()`.

### 7.3 `TaskGraph` and `compile()`

`TaskGraph` is the public builder (Pimpl). `add_node` adds raw kernel work with
optional read/write tracking; `add_task` adds a semantic task with metadata and
either a host-work body or a device body + host fallback.

`compile()` (Phase 2):
- links every leaf (no successors) to a single injected **sentinel** completion
  node, so "graph done" is "sentinel done";
- collects the **roots** (in-degree 0);
- moves all `Node`s into a `CompiledGraph { roots, sentinel, all_nodes }`, which
  *owns* every node (`sushi_ptr`) to keep them alive for the whole run — internal
  dependencies use raw `Node*`, so premature destruction would be use-after-free;
- in Master mode, also **stamps** which nodes are offloadable (see §11).

---

## 8. Scheduler (`scheduler/`)

Runs the ready tasks. A work-stealing scheduler with a persistent worker pool.

### 8.1 `TaskScheduler`

- One **work-stealing `Deque`** per worker (`worker_queues[MAX_WORKER_THREADS]`).
  A worker pushes/pops its own deque LIFO and **steals** from others' deque ends
  FIFO when idle.
- A global **injector**: a mutex-guarded `pending_submissions` deque for external
  submissions plus a `priority_submissions` `std::priority_queue` so a
  high-`priority` task can jump ahead of a backlog (the `PriorityBypass` tests).
- Worker threads are **persistent**: `start()` spawns them once;
  `arm_for_execution()` re-arms the `running` flag for each new graph run without
  recreating threads; `stop()` drains and joins. (This is the "no thread churn
  between runs" design.)
- Completed-task counters are per-worker and `alignas(CACHE_LINE_SIZE)` to avoid
  false sharing.

### 8.2 NUMA-aware hierarchical stealing

Built once at construction (`build_steal_topology`):

- `worker_numa_[i]` = the NUMA node of the core worker *i* is pinned to;
- `victim_order_[i]` = the other workers, **ordered by ascending NUMA distance**
  (from the injected `INumaDistanceModel`), so node-local victims come first;
- `local_tier_len_[i]` = how many leading entries are node-local.

`find_task_to_steal` walks `victim_order_` in order, so a thief exhausts cheap
same-node victims before reaching across nodes. `local_steals_` / `remote_steals_`
counters expose the split (asserted in the steal-locality benchmarks).
`steal_victim_order()` is a test introspection hook.

### 8.3 `EventPolling` — turning device completion into readiness

The CPU side can't block on every GPU kernel. After a worker submits a kernel it
records the `sycl::event` in an `ActiveEventList` (`MAX_EVENTS = 256`, O(1)
swap-remove). `poll_events` checks statuses with **adaptive backoff** (a
`backoff_counter` up to `MAX_BACKOFF = 64`, yielding past 16) so an idle poller
doesn't spin. When an event completes, `trigger_successors` does the core release
step: for each successor, `in_degree.fetch_sub(1, acq_rel)`; the worker that
drives it to zero `add_ref`s the successor and pushes it onto its local queue.
This is the exact same readiness logic the distributed completion hook mimics for
remote results.

### 8.4 `Task` / `RangeTask` — divisible work

`Task` is the scheduler's unit of work (kernel/host modes, `get_priority`,
`split`/`is_splittable`). `RangeTask` is the divisible primitive: an idle worker
that picks up a splittable task carves off half via an atomic compare-exchange on
the start index (`split()`), down to a minimum grain of **1024** elements, and
pushes the carved chunk back for another worker. `Node` itself is not splittable.

### 8.5 `WorkloadCoordinator` — optional thermal/throttle rebalancing

A background thread (a ~5 ms heartbeat) that drives a swappable
`IRebalanceStrategy` over an `ITelemetryProvider` + `ITaskMigrator` (both
implemented by `TaskScheduler`). The shipped `ThermalProxyStrategy` migrates work
**away from a worker whose queue is growing while its completion rate stalls** —
the signature of a thermally-throttled or slow core — and toward the least-loaded
worker, while **leaving a long-but-fast-draining queue alone**. The strategy is
replaceable at runtime (`set_strategy`).

---

## 9. Execution (`execution/`)

Ties hardware and graphs together.

### `RuntimeContext`

The top-level owner (Pimpl, for a stable ABI; non-copyable, non-movable). It:

- probes hardware and builds a `sycl::queue` per device;
- hands out queues by index (`get_queue`), by NUMA node (`get_numa_local_queue`,
  round-robin within a node), or **by the pointer that data lives on**
  (`get_queue_for_ptr`);
- owns the USM allocators (global and per-NUMA) and the `malloc_shared` /
  `malloc_device` / `free_usm` helpers that **record each allocation's owning
  queue**;
- exposes `numa_distance` and `cpu_core_to_numa` (used by the scheduler's steal
  topology);
- owns the `ExecutionEngine` and tears everything down in the correct order on
  `shutdown()` / destruction.

**The cross-context invariant.** USM is pinned to the device/context it was
allocated on. Running a kernel on a sibling device's queue (e.g. CPU USM on a
CUDA queue) is an illegal cross-context access. This is why `Node` retains
`io_reads_`/`io_writes_` and why the scheduler routes a kernel to the queue that
owns its data. Allocating through `RuntimeContext` (not bare
`sycl::malloc_shared`) is what makes that routing possible.

### `IExecutionEngine` / `ExecutionEngine`

`IExecutionEngine` is the "how to run a graph" strategy seam, separate from
`TaskGraph` (builds the DAG) and `RuntimeContext` (owns resources), so alternative
strategies (dry-run, profiling, replay) can be added without touching either.
The default `ExecutionEngine` owns one persistent `TaskScheduler`: `execute()`
injects the compiled graph's roots, arms the scheduler, and blocks on the
sentinel's completion promise; it never recreates threads.

---

## 10. Concurrency & memory-ordering principles

A few cross-cutting rules the codebase follows consistently:

- **Atomics over mutexes on hot paths.** Dependency edges, `in_degree`,
  successor lists, the bitmask pool, EBR, and refcounts are all lock-free. The
  only mutex on a critical path guards the global injector queue.
- **Cache-line alignment to kill false sharing.** `ResourceState`, `MemoryPage`,
  `ThreadLocalMagazine`, EBR `ThreadEpochSlot`, and per-worker counters are all
  `alignas(64)`.
- **Acquire/release discipline.** Successor release uses `fetch_sub(acq_rel)`;
  epoch publication uses release stores and acquire loads; the "drove in_degree to
  zero" check is the single synchronization point that makes a successor visible
  to exactly one worker.
- **Sharding to spread contention.** 1024 dependency shards, per-worker deques,
  per-thread magazines and retire lists — contention is partitioned, not locked.

---

## 11. Distributed (`distributed/`, optional)

Guarded entirely behind `SR_ENABLE_DISTRIBUTED` (CMake) / `SR_DISTRIBUTED`
(macro). When off, none of it compiles — zero code, zero dependencies, zero ABI.
Full operational detail is in [`DISTRIBUTED_GUIDE.md`](DISTRIBUTED_GUIDE.md); the
architectural essentials:

- **The core constraint:** compiled code can't travel the wire. So nothing is
  serialized except *references*: a task ships its `OpID` (a kernel already
  compiled into every node) plus `BufferHandle`s (`GlobalBufferID` + `version` +
  size). Closures and pointers never cross the network.
- **`ITransport`** abstracts the wire with a tiny `[4B length][1B type][payload]`
  frame. `InProcessTransport` runs the full multi-node protocol in one process
  (the network analogue of `RemappingTopologyProvider`); `TcpTransport` is a
  dependency-free raw-socket transport (POSIX + Winsock).
- **`DistributedCoordinator`** (master) is the only distributed object the
  scheduler touches: for each node `compile()` flagged offloadable, the dispatch
  loop calls `try_offload()` behind a single `[[unlikely]]` branch. It serves a
  **pull-based, master-authoritative, versioned** buffer cache (a write bumps the
  version, so stale worker caches re-pull), and on `TASK_RESULT` invokes the
  scheduler's completion hook — the network equivalent of `EventPolling` firing.
  A liveness monitor (default 5 s) re-dispatches a dead worker's tasks to a
  survivor or runs them locally.
- **`WorkerAgent`** (worker) is a thin shell around a normal local
  `RuntimeContext`: HELLO with its OpID set, run dispatched kernels through the
  existing NUMA-aware scheduler, reply with results.
- **`IOffloadPolicy`** decides where (or whether) to offload. The default
  `LeastLoadedOffloadPolicy` ranks live workers by estimated completion cost
  (RTT + transfer time + queue backlog), gates on free device memory, and keeps
  payloads below `min_offload_bytes` (default 64 KiB) local. Declining is always
  safe: the task runs locally.

A declined or failed offload is a transparent local fallback — the standalone
execution path is never compromised by the distributed layer being present.

---

## 12. A request traced through every layer

To make the layering concrete, here is one device task —
`graph.add_task(meta, {A,B}, {C}, kernel)` — from build to completion:

1. **graph/** — `add_task` creates a `Node`, stores the kernel and
   `io_reads_={A,B}`, `io_writes_={C}`. `DependencyTracker::resolve` is called for
   A, B (reads) and C (write): each pointer hashes (SplitMix64) to one of 1024
   shards; the history there yields the prior writers/readers; edges are added by
   incrementing this node's `in_degree` and registering it as their successor.
2. **graph/** — `compile()` finds this node is a root only if nothing wrote A/B/C
   before it; links it toward the sentinel; moves it into `CompiledGraph`.
3. **execution/** — `execute()` injects roots; the `ExecutionEngine` arms the
   `TaskScheduler`.
4. **scheduler/** — once `in_degree == 0`, a worker pops the node. Because it is a
   KERNEL node, it is submitted on the queue that **owns** A/B/C (via the
   context's owner record), and the returned `sycl::event` is recorded in that
   worker's `ActiveEventList`.
5. **scheduler/** — `EventPolling` notices the event complete, calls
   `trigger_successors`: each successor's `in_degree` is decremented; whoever hits
   zero pushes that successor onto its local deque. If a worker is idle, it
   **steals** — preferring node-local victims via `victim_order_`.
6. **scheduler/** — when the sentinel's predecessors all complete, the sentinel
   fires and `ExecutionEngine::execute` unblocks. Memory retired during the run is
   reclaimed by EBR once no thread is still in an older epoch.

---

## 13. Build-time configuration

| CMake option | Default | Effect |
|--------------|---------|--------|
| `SR_SYCL_TOOLCHAIN` | adaptivecpp | SYCL toolchain: `adaptivecpp`, `llvm`, or `oneapi` |
| `SR_CUDA_ARCH` | 61 | CUDA compute capability for the nvptx64 backend (e.g. 80, 86) |
| `SR_ENABLE_CUDA` | OFF | SYCL CUDA backend (arch via `SR_CUDA_ARCH`) |
| `SR_ENABLE_DISTRIBUTED` | OFF | compile the master-worker layer |
| `SR_BUILD_BENCHMARKS` | ON | build the heavy `sr_benchmark_tests` binary |

Three SYCL toolchains are supported, selected by `SR_SYCL_TOOLCHAIN`:
**AdaptiveCpp** (`acpp`, primary, vendor-neutral), **Clang/LLVM** (`clang++
-fsycl`, vendor-neutral, used by the Docker image), and **Intel oneAPI DPC++**
(`icpx`/`icx-cl`, secondary, for profiling). AdaptiveCpp and oneAPI both provide
the `add_sycl_to_target` CMake helper, so the source list stays toolchain-neutral;
on the bare-flag `llvm` path the helper is a no-op stub and `-fsycl` is applied
globally.

---

## 14. Invariants worth never breaking

1. **Build → compile → execute** are distinct phases; the first two are not
   thread-safe relative to each other.
2. **USM is context-bound.** A buffer is only valid on the queue/context that
   allocated it. Allocate through `RuntimeContext` when a graph task will touch
   it.
3. **Read/write declarations must be truthful.** The dependency tracker only
   knows what you declare; an undeclared write is an undetected race.
4. **The distributed layer is off by default and must stay zero-cost when off.**
   No distributed include or symbol may leak into a standalone build.
5. **Lock-free invariants:** never free a node another thread may still read
   without going through EBR; keep hot shared state cache-line aligned.

---

## 15. Directory map

```
include/SushiRuntime/      Public API (the headers this document describes)
  core/        RefCounted, sushi_ptr, logger, common constants, OpID, export macro
  topology/    HardwareDiscovery, NUMAMapper, ITopologyProvider/INumaDistanceModel,
               Hwloc + Remapping/Matrix substitutes, affinity
  memory/      USMAllocator + strategies, BitmaskMemoryPool + magazine, EpochManager
  graph/       TaskGraph, Node, DependencyTracker, task_types (OpID/TaskMetadata)
  scheduler/   TaskScheduler, Task/RangeTask, EventPolling, WorkloadCoordinator,
               rebalancer_interfaces, ThermalProxyStrategy
  execution/   RuntimeContext, IExecutionEngine/ExecutionEngine, CompiledGraph
  distributed/ (optional) protocol, transports, registries, coordinator, worker agent,
               offload policy
src/                       Implementations of the above
tests/                     functional/ (unit, integration, regression) + benchmark/
examples/                  distributed_demo (distributed builds only)
docs/                      guides + these architecture/process docs
```

For how to run any of this, see the [CLI guide](CLI_GUIDE.md); for how to write
against it, the [introduction](INTRODUCTION.md).
