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

SushiRuntime is a C++17 runtime library for **task-based parallel computing on
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

The code targets C++17. Headers such as `bit.hpp`, `span.hpp`, `result.hpp`, and
`sycl_compat.hpp` provide `<bit>`, `<span>`, `std::expected`-style results, and
portable `host_task` invocation as compatibility shims; on a C++20/23 toolchain
these thin out to standard aliases.

---

## 2. The layered architecture

The code is organized as a stack of layers under `include/SushiRuntime/` (public
API) and `src/` (implementations). Higher layers depend on lower ones; lower
layers never depend up.

```
        ┌─────────────────────────────────────────────────────────┐
        │  api/         Runtime · API::Graph · DynamicGraph       │  fluent entry point,
        │               RunReport · Error · Errc                  │  error types
        ├─────────────────────────────────────────────────────────┤
        │  execution/   RuntimeContext · IExecutionEngine         │  owns everything,
        │               ExecutionEngine · CompiledGraph           │  drives a run
        │               ExecutionMetrics                          │
        ├─────────────────────────────────────────────────────────┤
        │  graph/       TaskGraph · Node · DependencyTracker      │  build + analyze DAG
        ├─────────────────────────────────────────────────────────┤
        │  scheduler/   TaskScheduler · Task/RangeTask            │  run ready tasks
        │               EventPolling · WorkloadCoordinator        │  (work stealing)
        ├──────────────────────┬──────────────────────────────────┤
        │  memory/             │  topology/                       │  resources +
        │  USMAllocator        │  HardwareDiscovery · NUMAMapper  │  hardware model
        │  BitmaskMemoryPool   │  ITopologyProvider               │
        │  EpochManager (EBR)  │  INumaDistanceModel · affinity   │
        ├──────────────────────┴──────────────────────────────────┤
        │  core/   RefCounted · sushi_ptr · logger · common       │  foundations
        │          Error · Result · RunReport · compat shims      │
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

## 3. The public API layer (`api/`)

`include/SushiRuntime/api/runtime.hpp` is the intended entry point for users who
do not need to reach into the lower layers.

- **`API::Runtime`** — owns a `RuntimeContext` and provides the graph and data
  factories. `Runtime::create(config = {})` returns a `Runtime`;
  `runtime.graph()` returns an `API::Graph`; `runtime.buffer<T>(n)` /
  `runtime.state<T>(n)` allocate typed handles; `runtime.advanced()` exposes a
  narrow read-only view of internal state (device count, profiling/rebalancer
  flags, transfer bytes), and `runtime.unsafe_context()` is the deliberate escape
  hatch that returns the underlying `RuntimeContext` for direct queue or allocator
  access.

- **`Buffer<T>` / `State<T>`** — owning handles to shared USM, pinned to one
  device so co-allocated data shares a context. `State<T>` is double-buffered
  (current/next) for stepped workloads. Passing a handle to `add()` registers the
  task's reads and writes automatically.

- **`API::Graph`** — chainable builder. Everything spelled `add()` is ordered by
  the data it names; the shapes whose data cannot be inferred are spelled
  `add_untracked()` so choosing one is deliberate rather than accidental:
  - `add(state, fn)` — a stepped update rule; `fn(i, cur, next)` reads the
    current field and writes the next, with the dependency tracked.
  - `add(out, in0[, in1], fn)` — a coupled rule reading one or two other fields
    (Jacobi: every read sees the previous step's data).
  - `add(reads, writes, n, fn)` — per-element kernel with explicit access lists.
  - `add(reads, writes, n, range_fn)` — range kernel; `fn(0, n, handler)` drives
    the handler itself, ordered by the declared lists.
  - `add(reads, writes, raw_kernel_fn)` — raw callback for full control, ordered.
  - `add(reads, writes, extent, fn)` / `add(reads, writes, nd_range, fn)` — the
    multidimensional and tiled shapes, ordered.
  - `add(extent, In(a), Out(b), fn)` — captures register the access *and* hand
    the kernel the raw pointers.
  - `add_reduce(input, output, n, op[, identity])` — a reduction whose
    combination order is a function of the element layout alone, so the result
    is bit-identical across runs, worker counts and steal patterns. It folds
    fixed 256-element tiles left to right and repeats over the partials; each
    level is one ordinary tracked node reading the previous level's scratch
    window. `Sum`, `Minimum` and `Maximum` are provided; any trivially copyable
    binary functor works.
  - `add_segmented_reduce(values, segment_offsets, output, count, op[, identity])`
    — the same guarantee over a CSR-style ragged array, one work-item per
    segment. No atomics and no cross-work-item combining, at the cost of load
    balance when segment lengths differ sharply.
  - `add_untracked(n, fn)` / `add_untracked(n, range_fn)` /
    `add_untracked(raw_kernel_fn)` / `add_untracked(extent, fn)` /
    `add_untracked(nd_range, fn)` — the same launch shapes with no declared data.
    A per-element or raw callable captures its USM pointers by value, so nothing
    in its type says what it touches and the runtime cannot infer the ordering.
    These record no regions: the task is both a root and a leaf. Correct for
    self-contained work, wrong the moment another task shares data with it.
  - `run()` / `run(n)` — compile (once) and replay 1 or `n` steps; returns a
    `RunReport`; throws `SushiException` on error.
  - `run_for(duration)` / `run_until(predicate)` — replay until a wall-clock
    budget elapses or a condition holds.
  - `try_run()` / `try_run(n)` — non-throwing; return `Result<RunReport, Error>`.
  - `run_async()` — start one step and return without waiting, so the caller can
    do work that needs no workers (extracting the previous step's state for a
    renderer or an audio system) while this one runs. Returns a move-only
    `RunHandle` whose `wait()` finishes the step's lifecycle — the State advance
    happens there, not at submit — and returns the `RunReport`. Its destructor
    waits if `wait()` was not called, since the compiled plan must not be
    destroyed while workers hold pointers into it. One step at a time: starting
    another while a handle is pending is rejected before anything touches the
    plan, because re-arming in-degrees under a running DAG would corrupt it.
  - `cancel()` — thread-safe; stops an in-progress run from another thread, the
    run returns with `RunReport::cancelled` set (a clean stop, not an error).
  - `size()` — task count.

- **`API::DynamicGraph`** — the mutable, region-partitioned graph from
  `runtime.dynamic_graph()`, for streaming workloads. `region(key)` returns an
  `API::Region` (which reuses `Graph`'s `add()` surface) to record work on a
  partition; `drop(key)` removes one; `has_region(key)`, `region_count()`, and
  `size()` report structure. `run()` / `run(n)` / `run_for` / `run_until` /
  `try_run` / `cancel` mirror `Graph`. The composer recomposes only what changed
  between steps, and `compile_count()` advances once per mutation, not per region;
  cross-region ordering follows ascending region key (lower key = producer).

- **`RunReport`** — filled after each `run()` / `try_run()`:
  - `is_successful`, `cancelled`, `total_tasks_executed`, `total_duration_ms`,
    `graph_analysis_duration_ms`.
  - `numa_statistics` — `NumaNodeStatistics` per NUMA node: CPU/GPU task counts,
    `tasks_stolen_intra_numa`, `tasks_stolen_inter_numa`.
  - `distributed_statistics` — `DistributedStatistics`: offloaded task count,
    `remote_nodes_involved`.
  - `host_to_device_bytes` / `device_to_host_bytes` — bytes the runtime itself
    copied across the link during the run window, via `Buffer::read_range` /
    `write_range`. Implicit shared-USM page migration by the driver is invisible to
    SYCL and is deliberately **not** counted; only explicit, runtime-issued
    transfers are.
  - `node_timings` / `worker_timings` — present only when the runtime was created
    with profiling on (`Runtime::create({}, cfg)`, `RuntimeConfig::profiling`).
    `NodeTiming` gives per-node device vs host time and an invocation count keyed by
    a stable plan index; `WorkerTiming` splits each worker's run into
    busy/stealing/polling/idle for stall attribution. Off (the default), the queues
    and the dispatch hot path read no timestamps and these stay empty.
  - `scheduling_overhead_ms` is filled from the workers' completion-polling time
    when profiling is on, else zero. `network_latency_wait_ms` stays reserved.

- **`Errc`** — error codes: `ok`, `invalid_graph`, `compile_failed`,
  `allocation_failed`, `kernel_failed`, `device_lost`.

- **`Error`** — carries an `Errc` plus a string message.

- **`SushiException : std::runtime_error`** — thrown by `run()`.

- **`Result<T, E>`** (`result.hpp`) — a C++17 stand-in for `std::expected<T,E>`,
  used throughout the lower layers to return errors without exceptions.

`API::Runtime`, `Core::Error`, `Core::Errc`, `Core::RunReport`, and
`Core::SushiException` are promoted into the `SushiRuntime` namespace so callers
need not qualify through `API::` or `Core::`.

---

## 4. The end-to-end execution model

A program's lifecycle is always the same three phases, and they must not overlap:

```
   build the whole graph   ──►   compile()   ──►   execute()
   (Phase 1, single thread)      (Phase 2)         (Phase 3, blocks)
```

1. **Construct a `RuntimeContext`** (or call `Runtime::create()`). It probes
   hardware, builds a `sycl::queue` per device, sets up USM allocators, and
   starts a *persistent* worker pool (the `ExecutionEngine`'s `TaskScheduler`).
2. **Build a `TaskGraph`** (or use `API::Graph::add()`). Each `add_task` /
   `add_node` call appends a `Node` and runs it through the `DependencyTracker`,
   which wires hazard edges as nodes arrive.
3. **`graph.compile()`.** Links leaf nodes to a sentinel completion node, finds
   the roots (in-degree 0), and transfers ownership into a `CompiledGraph`. It
   submits nothing to hardware.
4. **`ctx.get_execution_engine().execute(compiled)`** (or the fluent `g.run()` /
   `run(n)`). Injects the roots into the scheduler, arms it, and blocks until the
   sentinel fires. The fluent layer compiles the graph once and replays the same
   `CompiledGraph` for each step — resetting node in-degrees between steps — so a
   multi-step run pays the build cost only once.

   `execute()` is literally `submit()` followed by `complete()`: the work is done
   by the persistent worker pool either way, and the only thing `execute()` adds
   is the blocking. Splitting the two is what `run_async()` is built on — no extra
   thread is involved, because there was never a thread to add. `submit()` takes
   the engine's non-reentrancy flag and `complete()` releases it, including on the
   path where a task exception is rethrown, so one failing kernel does not close
   the engine permanently. `IExecutionEngine` reports the capability through
   `supports_asynchronous_run()` rather than assuming it, so a strategy with no
   meaningful start/finish split (a dry-run or replay engine) is not forced to
   fake one.

> **Threading rule:** Phase 1 (`add_node`/`add_task`) and Phase 2 (`compile()`)
> are **not** thread-safe relative to each other. Build the entire graph, then
> compile, then execute.

**Late-bound replay.** The compiled plan is static in topology, but three
per-node properties may change between steps without recompiling: the
**iteration size**, the **first element** it iterates, and whether the node
**runs at all**. A node may carry a `Graph::LateBindings
{ enabled, count }` block (a `std::shared_ptr` the fluent layer re-points each
step); the scheduler skips a disabled node by routing it through the poller as
instantly complete (its successors still fire, so the DAG drains), and the kernel
reads `count` for its range. The fluent surface exposes this through `API::when()`
/ `API::sized()` (see the `Dynamic` tag). This generalises the distributed layer's
`live_reads_`/`live_writes_` late-pointer mechanism to size and enablement, so an
ECS-style graph that spawns/destroys entities or sleeps subsystems per frame
compiles once — `Graph::compile_count()` stays 1. Bindings may change size/enable
but **not** which resources a node touches, since that would invalidate the
dependency edges fixed at compile.

**Counts the device produces.** `sized()` is polled on the driver thread at the
step boundary, so it can only carry a number the host already knows. A
producer/consumer chain does not have one: a broadphase writes its pair count
mid-step and the narrowphase must iterate exactly that many. `LateBindings` there-
fore also carries a `device_count` pointer — a host-readable slot a predecessor
kernel writes — set by `API::sized_from_device(counter)`.

This is sound because of *when* a node's command group is built. `execute_task`
constructs it on a worker at dispatch, and a node is only dispatched once
`EventPolling::poll_events` has seen every predecessor's event complete and called
`event.wait()` on it. By that point the predecessor's USM writes are visible, so
the count can simply be read. The result is one composition per tick with no
mid-step readback and no launch at worst-case width, and `compile_count()` still
1 while the count changes every step.

Two constraints are enforced rather than documented: the counter must be shared
(host-addressable) USM, rejected at the call site otherwise; and a live count
above the node's compiled capacity fails the run rather than truncating, since
silently dropping work is the failure this seam exists to prevent. The counter's
element is registered as one of the consuming task's reads, so the producer is
necessarily a predecessor — the ordering is structural, not a convention.

**Where the node starts.** `count` says how many elements, `base` says from
where. Graph colouring is the case that needs it: each colour is a contiguous
slice of one constraint buffer, and every slice shifts as soon as the constraint
set changes, so a node pinned to a baked offset has to reach its own work through
an index-indirection buffer — an extra load per element for something the launch
can carry for free. `API::based_at(provider)` sets it from the host and
`API::based_at_device(slot)` from a slot a predecessor wrote, mirroring
`sized()` / `sized_from_device()` exactly. The runtime applies the offset, so the
callable still receives absolute indices and a kernel written without knowing
about bases stays correct. Base and count are resolved and validated **together**
at dispatch, because only their combination says whether the node stays inside
the buffers it was compiled for; a window that runs off the end fails the run.

**Dynamic region graph.** Late binding varies size and enablement but keeps a
fixed topology; a streaming open world also needs the *set* of work to change —
cells stream in and out as the player moves. `Runtime::dynamic_graph()` returns an
`API::DynamicGraph`, which partitions work into **regions** keyed by a caller id
(`region(key)` records tasks with the static graph's `add()` surface; `drop(key)`
removes one). A `Region` is implemented in terms of `API::Graph` by private
inheritance, so it reuses every `add()` overload and adds no second recording path.
Mutations apply at a step boundary, never against the running DAG.

The composition is incremental in two layers. Each region compiles once to a
cached sub-graph (`TaskGraph::take_nodes()`) whose **interior** edges — the
dependencies between tasks of the same region — are baked into the node objects
and never touched again while the region is unchanged. Dependencies that **cross**
a region boundary live in a separate layer on each node, grouped into a
`RegionLink Node::region`: `region.boundary_in_degree` adds to the intra count the
node already carries, and `region.boundary_successors` is walked by the scheduler's
`trigger_successors` alongside the inline/spillover intra successors. The composer
owns this boundary layer. When a region streams in or out, only its own nodes are
(re)built and only the allocations it *shares* with other regions are re-stitched:
the composer un-wires those bases' boundary edges, rebuilds the changed region, and
re-derives just those bases' cross-region edges. A node's live `in_degree` is
re-armed each step as `region.intra_in_degree + region.boundary_in_degree`
(`Node::arm_in_degree`), and the entry/root set and the
completion sentinel's leaf links are maintained incrementally as boundary edges
appear and vanish — so a commit costs O(changed region + affected boundary pins)
and never re-scans the untouched world.

Cross-region edges are derived per shared allocation by walking the regions that
touch it in ascending key order, threading the live writers and pending readers
through so RAW/WAR/WAW are honoured while two regions that only read the same
buffer stay parallel. A region's boundary interface is the entry pins (first
touchers, which receive incoming edges) and exit pins (unshadowed writes/reads,
which source the next region's edges), computed once per region build.

Each pin carries the **byte intervals** it touches, and every hazard test is an
interval overlap, so two regions writing disjoint element ranges of one
allocation gain no edge at all. That is what makes one-region-per-simulation-
island viable: islands are disjoint index ranges of a shared entity column rather
than separate allocations, so an allocation-granular boundary would serialize
islands that never meet — and per-island allocations would cost a copy on every
island merge and split. Where interval arithmetic would get expensive the layer
rounds toward keeping a pin live (a writer is retired only when one later write
interval fully contains it, and a node counts as an entry pin unless an intra
predecessor overlaps it), because an extra cross edge costs parallelism while a
missing one is a data race. The lower-keyed
region is therefore the producer; key the producer below the consumer for a hard
cross-region dataflow dependency, exactly as the static graph relies on add() order
within one graph. `DynamicGraph::compile_count()` stays 1 across an unmutated run
and advances by one per mutation, not per region. The runtime owns this mechanism;
the policy of *which* region streams when belongs to the consumer.

**Native CPU tasks.** A host task that submits no device work need not go through
a SYCL queue. When a `Node` reports `submits_device_work() == false` (set by the
fluent `Graph::add_host()`), the scheduler runs its `execute_native()` callable
directly on the worker thread and reports an empty event — the same "ready inline"
seam host tasks already use — so its successors release through the normal path
with no queue acquired. A node is only dispatched after its predecessor events
complete, so the shared USM it reads is visible host-side. The default is the safe
SYCL route (`submits_device_work() == true`); only `add_host()` opts in, so a host
task that does submit a kernel is never misrouted.

**Residency.** A `Buffer`/`State` allocates either host-addressable **shared** USM
(the default) or device-resident **device** USM (`Residency::Device`, via
`RuntimeContext::malloc_device_on`), chosen once at construction behind the same
handle API. Device residency keeps a large field on its device across steps with
no host migration; because device USM is not host-addressable, `operator[]` (and
the bulk `host()` view) throw a deterministic `invalid_access` error on a
device-resident handle, and the host instead uses `read_range()`/`write_range()`,
which copy a sub-range through the allocation's owning queue. Dependency tracking and
queue routing are identical for both — the owner map already records the device,
so kernels dispatch to it and the range copies use the same queue.

---

## 5. Core (`core/`)

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
Log level is compile-time gated. The format system is a custom `sr_format()`
built on variadic templates, not `std::format`.

### Error handling

Three headers form the error layer:

- **`error.hpp`**: `Errc` enum (`ok` / `invalid_graph` / `compile_failed` /
  `allocation_failed` / `kernel_failed` / `device_lost`), `Error` struct (`Errc`
  code + string message), `SushiException : std::runtime_error`.
- **`result.hpp`**: `Result<T, E>` — a C++17 value-based error carrier modelled
  on `std::expected`; `ErrorWrapper<E>`; `make_error()` factory. Used throughout
  the stack so callers can choose between throwing and non-throwing call sites.
- **`run_report.hpp`**: `RunReport` struct (see §3) plus `NumaNodeStatistics`,
  `DistributedStatistics`, and the profiling rows `NodeTiming` / `WorkerTiming`.

### Compatibility shims

- **`sycl_compat.hpp`**: `detail::enqueue_host_task(handler, fn)` — selects
  `host_task` on standard SYCL or `AdaptiveCpp_enqueue_custom_operation` on
  AdaptiveCpp via C++17 `void_t` detection.
- **`bit.hpp`**: `popcount_u64()` and `countr_zero_u64()` — wraps `__builtin_*`
  or `std::popcount` / `std::countr_zero` depending on the standard level in use.
- **`span.hpp`**: `SushiRuntime::span<T>` — aliases `std::span` on C++20,
  provides a minimal stand-in on C++17.

### `OpID` — open, hashed operation identity

`OpID` (in `graph/task_types.hpp`) is a 32-bit **FNV-1a** hash of a string,
computed at compile time via the `"..."_op` user-defined literal
(`"blas.gemm"_op`). It replaces rigid enums so any compute domain can register
named operations, and it is the *only* thing identifying a kernel across the wire
in the distributed layer (see §12).

---

## 6. Topology (`topology/`)

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
- **`HwlocDistanceModel`** loads the SLIT matrix from hwloc once at construction
  into a flat array; `get_distance()` is then an O(1) array lookup.
- **Injection seam.** `RuntimeContext` takes an `ITopologyProvider` (device
  enumeration) and an `INumaDistanceModel` (distance source). Left null, it uses
  `HwlocTopologyProvider` + `HwlocDistanceModel`. The substitutes —
  `RemappingTopologyProvider` (spreads real devices across N synthetic NUMA nodes,
  core *i* → node *i % N*) and `MatrixDistanceModel` (a hand-built distance
  matrix) — let multi-NUMA routing and NUMA-aware stealing be tested on
  single-socket hardware. This is the same substitution pattern the distributed
  layer uses for the network (`InProcessFabric` / `InProcessTransport`).
- **`affinity`** pins a thread to a logical core (Windows + POSIX), best-effort:
  an invalid core logs a warning rather than throwing.

---

## 7. Memory (`memory/`)

Allocation tuned for parallel, heterogeneous workloads. Three cooperating pieces.

### 7.1 `USMAllocator` + strategies

`USMAllocator` is the public allocation facade (`allocate`, `deallocate`,
`get_device_of`). Allocation behavior is pluggable via `IAllocatorStrategy`
(Strategy pattern, for OCP):

- `Strategies::Shared` → `sycl::aligned_alloc_shared` — CPU+device visible,
  **poolable**.
- `Strategies::Device` → `sycl::aligned_alloc_device` — device-only, not pooled.
- `Strategies::Host` → `sycl::aligned_alloc_host` — host-pinned, not pooled.

`RuntimeContext::malloc_shared/malloc_device/free_usm` wrap this and additionally
**record the owning queue** for each allocation (see §10), which is what lets the
scheduler dispatch a kernel to the exact device/context that owns its data.

A global `FlushCallback` (`g_flush_callback` atomic) routes a thread magazine's
cached slots back to the owning `USMAllocator` (e.g. on thread exit or bank
eviction). The callback runs under a shared lock (`g_flush_mutex`); an allocator's
destructor takes that lock exclusively while it nulls its registry slot and frees
its pages, so a flush can never touch a freed page and a flush that starts after
teardown safely no-ops (`owns_id` also rejects a registry-index collision). This
is what lets a thread that outlives its `RuntimeContext` still exit cleanly.

### 7.2 `BitmaskMemoryPool` + `ThreadLocalMagazine` — the fast path

For poolable (Shared) allocations there is a two-tier, mostly lock-free pool:

- **`ThreadLocalMagazine`** (`thread_local`) is the hot path. `NUM_BUCKETS = 4`
  size classes, each a stack of up to `BATCH_SIZE = 256` pointers. `pop` and
  `push` are a few instructions with **no atomics and no locks**. It holds a small
  set of **per-allocator banks** (`MAX_ALLOCATORS`) so interleaving allocations
  across allocators (multi-GPU / multi-context) no longer flushes the whole
  magazine on every switch — the former "flush cliff". A `cur_bank_` cache keeps
  the single-allocator case to one comparison; touching more allocators than the
  bank count evicts (flushes) the least-recently-used bank.
  `free_capacity(bucket_idx, alloc_id)` returns how many slots that allocator's
  bank can still absorb, used by `bulk_claim_capped` to size the refill exactly.
- **`BitmaskMemoryPool`** is the backing store. Memory is carved into
  `MemoryPage`s of 64 fixed-size slots tracked by a single `uint64_t` atomic
  `bitmask`. Refilling a magazine uses **bulk claim**: `bulk_claim` takes the
  whole page in one CAS to `~0ULL` (TCMalloc-style, 64× fewer atomics);
  `bulk_claim_capped` takes only as many free bits as the magazine can absorb
  (using `countr_zero_u64` / `popcount_u64`), which reduces CAS contention when
  many threads refill at once.
- **Remote frees.** Every `MemoryPage` records its `owning_slot`. When a thread
  frees a pointer that belongs to *another* thread's slot, the free is routed
  back to the owning slot's MPSC remote-free queue rather than polluting the
  freeing thread's magazine. The owner lazily drains that queue on its next
  refill. This kills the "consumer magazine fills with foreign pointers → flush
  spike" pathology and is the path the `CrossThreadFreeIsReclaimed` test guards.

Requests larger than the top bucket (4096 B) bypass the pool entirely and go
straight to SYCL.

### 7.3 `EpochManager` — epoch-based reclamation (EBR)

Lock-free data structures (the dependency tracker's access history, in
particular) can't free a node the instant it's unlinked — another thread might
still be reading it. EBR solves this:

- A global epoch counter (`global_epoch_`) and one cache-line-aligned
  `ThreadEpochSlot` per thread. Slot capacity is
  `MAX_EPOCH_THREADS = MAX_WORKER_THREADS + 32 = 288` to cover the scheduler
  workers plus auxiliary threads. `EpochGuard` (RAII) publishes the current epoch
  on `enter()` and clears it on `exit()`.
- `retire(ptr)` appends to a thread-local `retire_list`. Every
  `RETIRE_THRESHOLD = 128` retires, `check_and_reclaim()` computes the minimum
  epoch any thread is still in and frees everything retired in an earlier epoch.
- `RetiredNode` storage is itself pooled per thread (`FREE_POOL_CAP = 512`) so
  retire/reclaim doesn't hit the global allocator on the hot path. The pool is
  managed via `acquire_node()` / `release_node()` / `drain_free_pool()` on
  `EpochThreadLocalState`.
- **Thread-exit handoff:** when a thread dies, its outstanding `retire_list` is
  donated to a global orphan list (`global_orphan_list_`) and reclaimed by
  surviving threads — no leaks. (A subtle consequence, captured in a regression
  test: payloads retired near program end can be freed during a *later* test's
  reclamation, so test counters must outlive the test frame — hence `shared_ptr`
  counters in the EBR tests.)

---

## 8. Graph (`graph/`)

Builds and analyzes the task DAG.

### 8.1 `Node` — one task

`Node` derives from `Scheduler::Task` and carries:

- `TaskMetadata metadata` (name, `TaskType`, `OpID`, `priority`, `numa_affinity`,
  and `K_MAX_PARAMS = 8` inline scalar `params` with `set_param<T>` /
  `get_param<T>` template helpers);
- the work: `TaskWork work` (`void(sycl::handler&)`, kernel mode) and/or
  `HostWork host_work` (`sycl::event(sycl::queue&, const std::vector<sycl::event>&)`,
  host mode), with an `ExecutionMode` (`KERNEL` / `HOST`);
- `std::atomic<int32_t> in_degree` — incoming dependency count;
- successors: an **inline array** of `MAX_INLINE_SUCCESSORS = 16` plus a lock-free
  CAS-pushed `successor_list` for spillover, so small fan-out is heap-free;
- `io_reads_` / `io_writes_` — the raw USM pointers, retained on **every** build
  so the scheduler can route a kernel to the queue whose context owns the data;
- `SmallEventVector sycl_deps` — SYCL event dependencies with `STATIC_CAP = 8`
  inline before spilling to a vector;
- `bool is_fused` — marks the node as the product of kernel fusion; set by the
  API layer, reserved for a future fusion pass;
- non-core concerns are grouped into cohesive sub-structs so each subsystem owns
  its own slice rather than editing one flat node: `RegionLink region` (the dynamic
  graph's cross-region boundary layer — `intra_in_degree`, `boundary_in_degree`,
  `boundary_successors`, leaf/sentinel flags) and `NodeProfiling profiling`
  (`invocations` / `device_ns` / `host_ns`, written only when profiling is on), both
  inline;
- `std::unique_ptr<NodeExtensions> ext_` — one **unconditional** pointer to lazily
  allocated optional state, so `Node`'s layout never depends on a build flag. The
  distributed `OffloadState` (`offloadable`, `remote_op`, the buffer handles, the
  `attempts` retry cap) lives in `NodeExtensions` and is reached through
  `Node::offload()` (mutable, allocates on first use) and `Node::offload_state()`
  (read, nullable). A standalone build never allocates it and pulls in no distributed
  symbol; either way `sizeof(Node)` is identical, so the flag no longer changes the
  node's ABI.

`add_successor` is lock-free: it bumps an atomic index into the inline array, and
only on overflow allocates a `SuccessorNode` and CAS-pushes it onto the list.

### 8.2 `DependencyTracker` — lock-free hazard resolution

This is the heart of "implicit dependencies." It keeps a **per-allocation access
history** with no locks and orders accesses by **region overlap**, not bare
pointer equality:

- Accesses are tracked as `ResourceRegion { base, offset, length }` — a byte
  interval within one allocation (`core/resource_region.hpp`). A bare base
  pointer is the whole-buffer region, so pointer-keyed callers behave exactly as
  before; `Buffer::region(offset, count)` names a sub-range.
- The allocation space is **sharded** into `SHARD_COUNT = 1024` `ResourceState`s,
  each cache-line aligned (no false sharing). A region maps to a shard via a
  **SplitMix64** hash of its `base`, so every access to one allocation lands in
  the same chain and is comparable by overlap.
- Each shard's `ResourceState` holds an atomic `tail` pointing at the most recent
  `AccessNode` (`{ task, is_write, region, prev }`), forming an immutable
  backward-linked history.
- `resolve(node, region, is_write)` walks the history and orders the node after
  every earlier access whose region **overlaps**, adding exactly the edges
  required:
  - **RAW**: a read depends on every prior write that overlaps it.
  - **WAR / WAW**: a write depends on the overlapping preceding reads *and*
    writes.
  - **RAR stays parallel**: two reads do **not** depend on each other.
  - **Disjoint sub-regions stay parallel**: two accesses to non-overlapping
    ranges of the same allocation are not ordered (interior vs halo, an array's
    two halves). Distinct allocations never serialize.
  - The walk stops early only at a prior writer that **fully covers** the region
    (earlier accesses to that range are then ordered behind it transitively); a
    partially-overlapping writer does not end the walk, so no sub-region edge is
    dropped.
  - Edges are **deduplicated** (sharing several buffers between the same pair adds
    one edge) and a node never depends on itself.

`add_dependency(from, to)` increments `to`'s `in_degree` and registers `to` as a
successor of `from`. The observable is `in_degree`: the count the scheduler must
drive to zero before a node is runnable. The history nodes are reclaimed via EBR
on `reset()`.

### 8.3 `TaskGraph` and `compile()`

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
- in Master mode (distributed builds), also **stamps** which nodes are offloadable
  and initializes `offload.attempts`.

---

## 9. Scheduler (`scheduler/`)

Runs the ready tasks. A work-stealing scheduler with a persistent worker pool.

### 9.1 `TaskScheduler`

- One **work-stealing `Deque`** per worker (`worker_queues[MAX_WORKER_THREADS]`,
  defined in `scheduler/work_stealing_deque.hpp`). A worker pushes/pops its own
  deque LIFO and **steals** from others' deque ends FIFO when idle. The ring is a
  fixed `CAPACITY = 262144` slots; because `push` is single-producer (owner-only),
  a full ring spills the surplus into an owner-private overflow buffer and `pop`
  drains it back in as space frees — a task is **never dropped**, since a dropped
  task would leave its successors' `in_degree` above zero and deadlock the run.
- A global **injector**: a mutex-guarded `pending_submissions` deque for external
  submissions plus a `priority_submissions` `std::priority_queue` so a
  high-`priority` task can jump ahead of a backlog (the `PriorityBypass` tests).
- Worker threads are **persistent**: `start()` spawns them once;
  `arm_for_execution()` re-arms the `running` flag for each new graph run without
  recreating threads; `stop()` sets `shutdown_requested` (separate from `running`,
  for clean drain) then joins. (This is the "no thread churn between runs" design.)
- Completed-task counters are per-worker and `alignas(CACHE_LINE_SIZE)` to avoid
  false sharing. `get_local_steal_count()` / `get_remote_steal_count()` expose the
  steal split for telemetry and benchmarks.

### 9.2 NUMA-aware hierarchical stealing

Built once at construction (`build_steal_topology`):

- `worker_numa_[i]` = the NUMA node of the core worker *i* is pinned to;
- `victim_order_[i]` = the other workers, **ordered by ascending NUMA distance**
  (from the injected `INumaDistanceModel`), so node-local victims come first;
- `local_tier_len_[i]` = how many leading entries are node-local.

`find_task_to_steal` walks `victim_order_` in order, so a thief exhausts cheap
same-node victims before reaching across nodes. `local_steals_` / `remote_steals_`
counters expose the split (asserted in the steal-locality and NUMA-effect
benchmarks). `steal_victim_order()` is a test introspection hook.

### 9.3 `EventPolling` — turning device completion into readiness

The CPU side can't block on every GPU kernel. After a worker submits a kernel it
records the `sycl::event` in an `ActiveEventList` (O(1) swap-remove). The list
keeps an inline `INLINE_CAP = 256` fast path and **spills to the heap** beyond it,
so `push` never fails: dropping an event would orphan its successors and deadlock
the run. `poll_events` checks statuses with **adaptive backoff** (a
`backoff_counter` up to `MAX_BACKOFF = 64`, yielding past 16) so an idle poller
doesn't spin.

Two completion paths exist within an `ActiveEventList` entry:

- **Async events** (`ready = false`): status is queried each poll cycle; once
  complete, `trigger_successors` fires.
- **Synchronous host work** (`ready = true`): the host task returned an empty
  event. The entry is immediately eligible; `trigger_successors` fires on the next
  poll tick without a status query.

**AdaptiveCpp lazy-event drive.** AdaptiveCpp's DAG is driven lazily — a `wait()`
is needed to flush submitted work. `stall_polls_` counts consecutive poll cycles
where no event made progress. When `stall_polls_` reaches
`STALL_DRIVE_THRESHOLD = 32`, the poller forces a blocking `wait()` on the oldest
outstanding event to break the stall, then resets the counter.

`set_completion_observer()` (distributed builds only) installs a per-node callback
fired by `trigger_successors`; the `DistributedCoordinator` uses this to bump
buffer versions and release remote successors in sync with local ones.

### 9.4 `Task` / `RangeTask` — divisible work

`Task` is the scheduler's unit of work (kernel/host modes, `get_priority`,
`split`/`is_splittable`). `RangeTask` is the divisible primitive: an idle worker
that picks up a splittable task carves off half via an atomic compare-exchange on
the start index (`split()`), down to a minimum grain of **1024** elements, and
pushes the carved chunk back for another worker. `Node` itself is not splittable.

### 9.5 `WorkloadCoordinator` — optional thermal/throttle rebalancing

A background thread (a ~5 ms heartbeat) that drives a swappable
`IRebalanceStrategy` over an `ITelemetryProvider` + `ITaskMigrator` (both
implemented by `TaskScheduler`). The shipped `ThermalProxyStrategy` migrates work
**away from a worker whose queue is growing while its completion rate stalls** —
the signature of a thermally-throttled or slow core — and toward the least-loaded
worker, while **leaving a long-but-fast-draining queue alone**. The strategy is
replaceable at runtime (`set_strategy`).

It is on by default but **opt-out**: `Runtime::rebalancer(false)` (via
`ExecutionEngine::set_rebalancer_enabled`) stops the background thread. Disable it
for a low-jitter real-time frame or a deterministic/replay run, where mid-run task
migration is itself a source of run-to-run variation; leave it on for batch
throughput.

---

## 10. Execution (`execution/`)

Ties hardware and graphs together.

### `RuntimeContext`

The top-level owner (Pimpl, for a stable ABI; non-copyable, non-movable). It:

- probes hardware and builds a `sycl::queue` per deduplicated device;
  `device_count()` returns the count after deduplication — AdaptiveCpp may
  expose an extra OpenMP host device that is folded in;
- hands out queues by index (`get_queue`), by NUMA node (`get_numa_local_queue`,
  round-robin within a node), or **by the pointer that data lives on**
  (`get_queue_for_ptr`);
- owns the USM allocators (global and per-NUMA) and the `malloc_shared` /
  `malloc_device` / `free_usm` helpers that **record each allocation's owning
  queue**;
- exposes `numa_distance` and `cpu_core_to_numa` (used by the scheduler's steal
  topology);
- provides `wait_all()` — a global barrier across all queues, used in tests and
  teardown;
- owns the `ExecutionEngine` and tears everything down in the correct order on
  `shutdown()` / destruction.

When built with `SR_ENABLE_DISTRIBUTED`, `RuntimeContext` also carries a
`RuntimeRole` enum (`Standalone` / `Master` / `Worker`) and exposes methods for
buffer registration (`register_distributed_buffer`, `bump_distributed_buffer`,
`unregister_distributed_buffer`, `distributed_buffer`), role query
(`distributed_role`, `distributed_supports`), and telemetry
(`distributed_offloaded_count`, `distributed_declined_count`,
`distributed_redispatched_count`, `distributed_alive_workers`,
`distributed_set_liveness_timeout`). These are all zero-cost when the macro is off.

**The cross-context invariant.** USM is pinned to the device/context it was
allocated on. Running a kernel on a sibling device's queue (e.g. CPU USM on a
CUDA queue) is an illegal cross-context access. This is why `Node` retains
`io_reads_`/`io_writes_` and why the scheduler routes a kernel to the queue that
owns its data. Allocating through `RuntimeContext` (not bare
`sycl::malloc_shared`) is what makes that routing possible.

A single kernel can run on only one context, so a node whose buffers straddle a
device boundary cannot be expressed correctly. On a node's first dispatch the
scheduler checks (via `context_index_for_ptr`) that all its regions share one
context and, if not, fails the run with a precise `invalid_graph` error rather than
letting the kernel fault — the result is cached per node, so replay pays nothing.
Genuine cross-device execution (a logical array split across devices, data moving
with the work) is a domain-decomposition concern deferred to WP-5; until then this
guard enforces co-location, the contract the layer above must honour.

### `IExecutionEngine` / `ExecutionEngine`

`IExecutionEngine` is the "how to run a graph" strategy seam, separate from
`TaskGraph` (builds the DAG) and `RuntimeContext` (owns resources), so alternative
strategies (dry-run, profiling, incremental update) can be added without touching
either. Replay is already used by the fluent layer.

`IExecutionEngine` exposes `execute(CompiledGraph&)` — non-consuming, so one
`CompiledGraph` can be run many times — plus an rvalue convenience overload for
one-shot runs, and `metrics()`. **`ExecutionMetrics`** is the return type of
`metrics()`:
- `local_steals`, `remote_steals` — steal counts since the last
  `arm_for_execution` call;
- `tasks_completed` — total nodes executed in the last run.

The fluent run driver snapshots `metrics()` before and after a run to populate
the `RunReport` steal counters.

The default `ExecutionEngine` owns one persistent `TaskScheduler`: `execute()`
injects the compiled graph's roots, arms the scheduler, and blocks on a single
**reusable completion latch** until the sentinel signals; it never recreates
threads. The latch (a mutex + condition_variable owned once by the engine) replaces
the per-step `std::promise`/`std::future` pair the old path allocated on every step
of a replayed run — for a million-step run that was millions of heap allocations and
a thread hand-off per step, pure jitter on a real-time frame. The sentinel's
`host_work` is wired once per compiled plan (it captures only the stable latch and
scheduler pointers), so a replay reuses the closure instead of rebuilding it; a
recompile makes a fresh sentinel that re-wires.

**Error isolation.** A task exception aborts only its own run, not the pool. The
worker records the first exception (`run_failed_` + `run_error_`) instead of
latching `shutdown_requested`; every remaining node then drains through the
completion path without executing, so the DAG still reaches the sentinel. The
sentinel reads `run_failed()`, re-arms the pool, and delivers the error through the
completion latch (the caller rethrows it). The pool stays alive and re-armable, so a
fresh graph runs normally after a failed one.

**Cancellation.** `Graph::cancel()` (thread-safe, callable while another thread
blocks in `run`/`run_for`/`run_until`) sets `run_cancelled_`, which drives the
same drain-without-executing path — but the run ends *cleanly* (the sentinel
calls `set_value`, no exception). The multi-step driver checks `is_cancelled()`
and stops before the next step, flagging `RunReport::cancelled`. Unlike a failure,
the flag is cleared per *run* (`reset_cancellation()` at the driver's start), not
per step, since a cancel aborts the whole multi-step run.

---

## 11. Concurrency & memory-ordering principles

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

## 12. Distributed (`distributed/`, optional)

Guarded entirely behind `SR_ENABLE_DISTRIBUTED` (CMake) / `SR_DISTRIBUTED`
(macro). When off, none of it compiles — zero code, zero dependencies, zero ABI.
Full operational detail is in [`DISTRIBUTED_GUIDE.md`](DISTRIBUTED_GUIDE.md); the
architectural essentials:

- **The core constraint:** compiled code can't travel the wire. So nothing is
  serialized except *references*: a task ships its `OpID` (a kernel already
  compiled into every node) plus `BufferHandle`s (`GlobalBufferID` + `version` +
  `size_bytes`). Closures and pointers never cross the network.

- **`protocol.hpp`** defines the wire format: a `[4B length][1B type][payload]`
  frame. `MsgType` enum: `REGISTER_HELLO` / `REGISTER_ACK` / `TASK_DISPATCH` /
  `BUFFER_REQUEST` / `BUFFER_PUSH` / `TASK_RESULT` / `HEARTBEAT` / `PING` /
  `PONG`. `ByteWriter` and `ByteReader` are the serializer/deserializer pair used
  by all message encode/decode paths.

- **`ITransport`** abstracts the wire with `send` / `on_message` / `start` /
  `stop` / `peer_count`. Two implementations:
  - `InProcessFabric` — a shared switchboard that owns N endpoints for a
    same-process multi-node simulation. Each `InProcessTransport` holds a reference
    to the fabric and a `node_id`; the fabric routes between them. This is the
    network analogue of `RemappingTopologyProvider` — the same substitution pattern
    used in topology for deterministic multi-NUMA testing.
  - `TcpTransport` — a dependency-free raw-socket transport (POSIX + Winsock) for
    real clusters. Factory: `TcpTransport::make_master(port)` /
    `TcpTransport::make_worker(host, port)`.

- **`KernelRegistry`** maps `OpID` to `RemoteKernel` — the local function that
  executes when a dispatched task arrives. Workers register their kernels on
  startup; the master sends only the `OpID`.

- **`BufferRegistry`** is the master-authoritative versioned buffer cache. A write
  bumps the `version`, so stale worker caches re-pull. `GlobalBufferID` + `version`
  is what crosses the wire; workers resolve handles to local USM.

- **`WorkerStats`** (`i_offload_policy.hpp`) carries per-worker live state for the
  offload policy: `worker_id`, `alive`, `inflight` tasks, `free_mem` bytes,
  `rtt_micros` (round-trip time estimate).

- **`DistributedCoordinator`** (master, Pimpl) is the only distributed object the
  scheduler touches: for each node `compile()` flagged offloadable, the dispatch
  loop calls `try_offload()` behind a single `[[unlikely]]` branch. It serves the
  versioned `BufferRegistry`, and on `TASK_RESULT` fires the
  `set_completion_observer()` callback registered on `EventPolling`, so the
  scheduler releases successors in the same path as local completion. A liveness
  monitor (default 5 s, configurable) triggers a `ResubmitHook` on a dead worker
  — re-injecting orphaned nodes into the scheduler for local or re-remote
  execution. Telemetry: `offloaded_count()`, `declined_count()`,
  `redispatched_count()`, `alive_worker_count()`.

- **`WorkerAgent`** (worker, Pimpl) is a thin shell around a normal local
  `RuntimeContext`: HELLO with its `OpID` set, run dispatched kernels through the
  existing NUMA-aware scheduler, reply with results. `set_heartbeat_interval()`
  sets the PING period (default 1 s; pass 0 to disable for unit tests).

- **`IOffloadPolicy`** decides where (or whether) to offload. The default
  `LeastLoadedOffloadPolicy` ranks live workers by estimated completion cost
  (RTT + transfer time + queue backlog via `WorkerStats`), gates on free device
  memory, and keeps payloads below `min_offload_bytes` (default 64 KiB) local.
  Declining is always safe: the task runs locally.

A declined or failed offload is a transparent local fallback — the standalone
execution path is never compromised by the distributed layer being present.

---

## 13. A request traced through every layer

To make the layering concrete, here is one device task —
`graph.add_task(meta, {A,B}, {C}, kernel)` — from build to completion:

1. **api/** — if using the fluent API, `API::Graph::add()` wraps this into an
   `add_task` call and records pre/post `ExecutionMetrics` timestamps for the
   `RunReport`.
2. **graph/** — `add_task` creates a `Node`, stores the kernel and
   `io_reads_={A,B}`, `io_writes_={C}` as `ResourceRegion`s (bare pointers become
   whole-buffer regions). `DependencyTracker::resolve` is called for A, B (reads)
   and C (write): each region's `base` hashes (SplitMix64) to one of 1024 shards;
   the history there yields the prior writers/readers whose regions overlap; edges
   are added by incrementing this node's `in_degree` and registering it as their
   successor.
3. **graph/** — `compile()` finds this node is a root only if nothing wrote A/B/C
   before it; links it toward the sentinel; moves it into `CompiledGraph`.
4. **execution/** — `execute()` injects roots; the `ExecutionEngine` arms the
   `TaskScheduler`.
5. **scheduler/** — once `in_degree == 0`, a worker pops the node. Because it is a
   KERNEL node, it is submitted on the queue that **owns** A/B/C (via the
   context's owner record), and the returned `sycl::event` is recorded in that
   worker's `ActiveEventList`.
6. **scheduler/** — `EventPolling` notices the event complete (or, on AdaptiveCpp,
   hits `STALL_DRIVE_THRESHOLD = 32` and forces a `wait()` to drive the lazy DAG),
   calls `trigger_successors`: each successor's `in_degree` is decremented; whoever
   hits zero pushes that successor onto its local deque. If a worker is idle, it
   **steals** — preferring node-local victims via `victim_order_`.
7. **scheduler/** — when the sentinel's predecessors all complete, the sentinel
   fires and `ExecutionEngine::execute` unblocks. `metrics()` returns the steal
   counts for `RunReport`. Memory retired during the run is reclaimed by EBR once
   no thread is still in an older epoch.

---

## 14. Build-time configuration

| CMake option | Default | Effect |
|--------------|---------|--------|
| `SR_SYCL_TOOLCHAIN` | intel-llvm | SYCL toolchain: `intel-llvm`, `adaptivecpp`, or `oneapi` |
| `SR_GPU_BACKEND` | auto | GPU compute backend: `auto`, `cuda`, `rocm`, `intel`, `cpu`, or `none` |
| `SR_CUDA_ARCH` | 61 | CUDA compute capability for the nvptx64 backend (e.g. 80, 86) |
| `SR_ENABLE_CUDA` | OFF | **Legacy alias** for `SR_GPU_BACKEND=cuda`; kept for back-compat |
| `SR_ENABLE_DISTRIBUTED` | OFF | compile the master-worker layer |
| `SR_BUILD_TESTS` | OFF | build the test suite (`sr build` turns this ON; pass `--no-test` to skip) |
| `SR_BUILD_BENCHMARKS` | ON | build the heavy `sr_benchmark_tests` binary |

### GPU backend selection

`SR_GPU_BACKEND` is the single knob that picks the accelerator target; it
resolves in `cmake/gpu/` to exactly one vendor strategy:

- **`auto`** (default) — probe the host *compiler* toolchain (not just a driver):
  `nvcc` → `cuda`, else `hipcc`/`rocminfo` → `rocm`, else the portable
  **`cpu`** (OpenCL / `spir64` JIT) path. The apt CUDA and ROCm toolkits do not
  add themselves to `PATH`, so auto-detect also searches their well-known
  prefixes (`/usr/local/cuda*`, `/opt/rocm*`).
- **`cuda`** — SYCL CUDA (nvptx64 `sm_${SR_CUDA_ARCH}`).
- **`rocm`** — AMD ROCm/HIP (amdgcn).
- **`intel`** — Intel GPU (Level Zero).
- **`cpu`** / **`none`** — CPU/OpenCL fallback (`none` also skips any offload triple).

The CLI wraps this: `sr build` passes `SR_GPU_BACKEND=auto` and reads the
resolved backend back, so the vendor logic lives in one place (`cmake/gpu/`)
rather than being duplicated in Python. `SR_CUDA_ARCH` is itself auto-detected
from `nvidia-smi` when a CUDA GPU is present.

Three SYCL toolchains are supported, selected by `SR_SYCL_TOOLCHAIN`:
**intel-llvm** (`clang++ -fsycl` from the intel/llvm nightly bundle, primary,
vendor-neutral, used by the Docker image), **AdaptiveCpp** (`acpp`, secondary,
vendor-neutral), and **Intel oneAPI DPC++** (`icpx`/`icx-cl`, supported, for
profiling). AdaptiveCpp and oneAPI both provide the `add_sycl_to_target` CMake
helper, so the source list stays toolchain-neutral; on the `intel-llvm` path the
helper is a no-op stub and `-fsycl` is applied globally.

---

## 15. Invariants worth never breaking

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

## 16. Directory map

```
include/SushiRuntime/      Public API (the headers this document describes)
  api/         Runtime (fluent entry point), Buffer/State handles, API::Graph,
               API::DynamicGraph / Region, RunReport, Error, Errc
  core/        RefCounted, sushi_ptr, logger, common constants, OpID, export macro,
               Error/Result/RunReport, compat shims (bit, span, sycl_compat)
  topology/    HardwareDiscovery, NUMAMapper, ITopologyProvider/INumaDistanceModel,
               Hwloc + Remapping/Matrix substitutes, affinity
  memory/      USMAllocator + strategies, BitmaskMemoryPool + magazine, EpochManager
  graph/       TaskGraph, Node, DependencyTracker, task_types (OpID/TaskMetadata)
  scheduler/   TaskScheduler, Task/RangeTask, EventPolling, WorkloadCoordinator,
               rebalancer_interfaces, ThermalProxyStrategy
  execution/   RuntimeContext, IExecutionEngine/ExecutionEngine, CompiledGraph,
               ExecutionMetrics
  distributed/ (optional) protocol, ByteWriter/ByteReader, transports
               (InProcessFabric/InProcessTransport, TcpTransport), KernelRegistry,
               BufferRegistry, coordinator, worker agent, IOffloadPolicy, WorkerStats
src/                       Implementations of the above
tests/                     functional/ (unit, integration, regression) + benchmark/
examples/                  fluent tutorials: quickstart + *_demo (always built);
                           distributed_demo (distributed builds only)
docs/                      guides + these architecture/process docs
```

For how to run any of this, see the [CLI guide](CLI_GUIDE.md); for how to write
against it, the [introduction](INTRODUCTION.md).
