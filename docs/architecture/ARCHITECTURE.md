# SushiRuntime Architecture

This document describes how SushiRuntime is built, layer by layer, down to the
data structures and concurrency techniques that make it work. It is written for
contributors and reviewers who need an accurate mental model of the runtime —
every class, constant, and algorithm named here exists in the headers under
`include/SushiRuntime/`.

> If you only want to *use* the library, read [`INTRODUCTION.md`](../getting_started/INTRODUCTION.md)
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

The code is organized as modules under `modules/<tier>/<module>/`, each with its public
headers in `include/SushiRuntime/<module>/` and its implementation in `source/`. A
module depends on lower tiers and on the same-tier modules that
`cmake/SushiRuntimeTiers.cmake` lists; the build refuses any other dependency. The
diagram groups the modules by the layer they form; section 16 has the full map.

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
        │  UsmRegistry         │  HardwareDiscovery · NUMAMapper  │  hardware model
        │  EpochManager (EBR)  │  ITopologyProvider               │
        │                      │  INumaDistanceModel · affinity   │
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

`modules/surface/api/include/SushiRuntime/api/runtime/runtime.hpp` is the intended entry point for users who
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
  - `add(reads, writes, n, range_kernel)` — range kernel; `fn(0, n, handler)` drives
    the handler itself, ordered by the declared lists.
  - `add(reads, writes, raw_kernel)` — raw callback for full control, ordered.
  - `add(reads, writes, extent, fn)` / `add(reads, writes, nd_range, fn)` — the
    multidimensional and tiled shapes, ordered.
  - `add(extent, In(a), Out(b), fn)` — captures register the access *and* hand
    the kernel the raw pointers.
  - `recorder()` — the interface a domain library records its own node kinds through:
    `rule()` groups the nodes of one call under one registration index, `kernel(...)` and
    `resolve(...)` record them, `scratch(...)` gives them storage that lives as long as the
    graph. The runtime carries no reduction, scan or sort; SushiBLAS builds them here.
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
  - `run(report)` / `run(n, report)` / `try_run(report)` / `try_run(n, report)` —
    the same runs into a `RunReport` the caller owns, for a frame loop that does
    not want a report allocated per tick. The report is reset and refilled in
    place, so its vector storage is reused from the second run on; the throwing
    forms return a reference to it and the non-throwing ones a pointer. Reset
    also happens on the rejection paths, so a caller that skipped the error
    cannot read the previous run's numbers back out of a run that never
    happened. Steady state is allocation-free with profiling off — a profiling
    run rebuilds its per-node labels by design.
  - `run_async()` — start one step and return without waiting, so the caller can
    do work that needs no workers (extracting the previous step's state for a
    renderer or an audio system) while this one runs. Returns a move-only
    `RunHandle` whose `wait()` finishes the step's lifecycle — the State advance
    happens there, not at submit — and returns the `RunReport`; `wait(report)`
    and `try_wait(report)` settle into a caller-owned one instead. Its destructor
    waits if `wait()` was not called, since the compiled plan must not be
    destroyed while workers hold pointers into it. One step at a time: starting
    another while a handle is pending is rejected before anything touches the
    plan, because re-arming in-degrees under a running DAG would corrupt it.
  - `cancel()` — thread-safe; stops an in-progress run from another thread, the
    run returns with `RunReport::cancelled` set (a clean stop, not an error).
  - `size()` — task count.
  - `set_profiling_requested(bool)` / `profiling_requested()` — opts this graph,
    and only this graph, into per-dispatch node event records
    (`RunReport::node_events`/`clock`, §5's `RunReport`). Defaults from
    `RuntimeSettings::profiling`; the setter marks the plan dirty, so the next
    `run()` recompiles with the flag stamped into `CompiledGraph::profiling` and
    dispatches this graph's device nodes on the runtime's lazily built profiling
    queue instead of the plain one. A runtime with no graph ever requesting it
    never builds that queue.

- **`API::DynamicGraph`** — the mutable, region-partitioned graph from
  `runtime.dynamic_graph()`, for streaming workloads. `region(key)` returns an
  `API::Region` (which reuses `Graph`'s `add()` surface) to record work on a
  partition; `drop(key)` removes one; `has_region(key)`, `find_region(key)`,
  `region_count()`, and `size()` report structure without marking anything for
  a rebuild. `run()` / `run(n)` / `run_for` / `run_until` /
  `try_run` / `cancel` mirror `Graph`, including the caller-owned report
  overloads. The composer recomposes only what changed
  between steps, and `compile_count()` advances once per mutation, not per region;
  cross-region ordering follows ascending region key (lower key = producer).
  `set_profiling_requested(bool)` / `profiling_requested()` mirror `Graph`'s
  pair, but the setter marks every live region for a full recompose instead of
  one dirty flag: a `DynamicGraph`'s node index (`Graph::Node::registration_index`,
  read as `NodeEventRecord::node_index`) is not fixed at `add()` time the way a
  static `Graph`'s is — it is renumbered at every recompose, densely, ordered by
  ascending region key and then by `add()` order within a region. Dropping and
  re-adding a region therefore never leaves a gap: the whole live region set is
  renumbered from zero on the next commit.

- **`RunReport`** — filled after each `run()` / `try_run()`. `reset()` returns it
  to its as-constructed state while keeping its vectors' capacity, which is what
  makes the caller-owned overloads above cheap:
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
  - `node_timings` / `worker_timings` — present only when the graph that produced
    this report requested profiling (`Graph`/`DynamicGraph::set_profiling_requested`,
    defaulting from `RuntimeConfig::profiling`). `NodeTiming` gives per-node
    device vs host time and an invocation count keyed by a stable plan index;
    `WorkerTiming` splits each worker's run into busy/stealing/polling/idle for
    stall attribution. Off (the default), the queues and the dispatch hot path
    read no timestamps and these stay empty. Both derive from `node_events`
    below, so there is one capture path.
  - `node_events` / `clock` — one `Core::NodeEventRecord` per node dispatch
    (`node_index` the public registration, `invocation` the zero-based dispatch
    count within the run, `submit_ns`/`start_ns`/`end_ns` on the backend's event
    clock) and the `Core::ClockPairing` that lets a host node's steady-clock
    timestamps convert onto that same clock. Empty unless the graph requested
    profiling; `reset()` clears both while keeping `node_events`' capacity, so a
    caller-owned report reused every tick allocates only when a run's node count
    exceeds its previous high-water mark.
  - `scheduling_overhead_ms` is filled from the workers' completion-polling time
    when profiling is on, else zero. `network_latency_wait_ms` stays reserved.

  A profiled graph does not tax an unprofiled one: profiling dispatches on a
  second, lazily built queue per device (`RuntimeContext::profiling_queue`),
  sharing the plain queue's context, so a graph that never asks for it pays
  nothing beyond one branch at queue selection. Building that queue is not
  free — measured on an RTX 3080 Ti's CUDA adapter, submitting to a
  profiling-enabled queue costs about 24 microseconds more per submission than
  the plain one, which is the reason the two queues are kept separate instead of
  always profiling and filtering afterwards.

- **`Errc`** — error codes: `ok`, `invalid_graph`, `compile_failed`,
  `allocation_failed`, `kernel_failed`, `device_lost`.

- **`Error`** — carries an `Errc` plus a string message.

- **`SushiException : std::runtime_error`** — thrown by `run()`.

- **`Result<T, E>`** (`result.hpp`) — a C++17 stand-in for `std::expected<T,E>`,
  used throughout the lower layers to return errors without exceptions.
  **Permanent, not provisional.** It appears in the public return type of every
  `try_*` entry point, so the alternative — waiting for C++23 and swapping to
  `std::expected` — would either break every one of those signatures at the
  language bump or have to be done before the API freeze anyway. Its shape
  already mirrors `std::expected`, so if the baseline ever moves the migration is
  an alias, not a signature change, and consumers see nothing. The decision is
  closed: `Core::Result` is what the API returns.

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

**The in-order chain (layer 1).** `Graph::build_plan()` also classifies the plan
(`Execution::classify_chain`, `execution/chain_plan.hpp`). A plan is chainable when
every node submits device work (a kernel, or host work that submits one), is not a
distributed offload candidate, names at least one USM region, and every region
belongs to one device queue in the context's registry. Anything else, or
`RuntimeConfig::force_scheduler`, keeps the scheduler path above. No node reads a
device-written value on the host between nodes: device-sized nodes resolve their
count inside the kernel (see "Counts the device produces"), so a plan built from
`sized_from_device` or from a device-sized rule a domain library records chains.

A chainable plan runs through `Execution::ChainRun` (`execution/chain_run.hpp`) on
the calling thread: each step submits the nodes in a fixed topological order to
`RuntimeContext::chain_queue()`, an in-order queue per device on the plain queue's
context, with no worker hand-off and no polling. The per-step host work
(`before_step`, `rearm`, `after_step`) still runs between submissions, since it only
writes host pointers and host-set controls. `run(n)` and `try_run(n)` submit all
steps back to back and wait once; `run_for` and `run_until` wait after each step,
because their stop test may read results. A disabled node is skipped at submit. A
command group or host work on this path is built before its predecessors finish,
so it must not read a predecessor's result on the host; that is what
`sized_from_device` is for. With profiling requested the chain uses the profiling
flavour of the queue and writes one `NodeEventRecord` per submission. No worker runs
on this path, so its worker timings record no work. `RunReport::layer` names the
path. `run_async()` and `DynamicGraph` always take the scheduler.

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
`live_reads`/`live_writes` late-pointer mechanism to size and enablement, so an
ECS-style graph that spawns/destroys entities or sleeps subsystems per frame
compiles once — `Graph::compile_count()` stays 1. Bindings may change size/enable
but **not** which resources a node touches, since that would invalidate the
dependency edges fixed at compile.

**Counts the device produces.** `sized()` is polled on the driver thread at the
step boundary, so it can only carry a number the host already knows. A
producer/consumer chain does not have one: a broadphase writes its pair count
mid-step and the narrowphase must iterate exactly that many. `LateBindings` there-
fore also carries a `device_count` pointer — a host-readable slot a predecessor
kernel writes — set by `API::sized_from_device(counter)` on a `Residency::Host`
buffer.

The host never reads that slot. The node is dispatched at its capacity and its
kernel reads the count (and a `based_at_device` base) from the slot, returning at
once in every work-item past the live window, one bounds check each
(`Graph::submit_late_window`, spec
`docs/design/GRAPH_REPLAY.md` §D). A device-sized rule
works the same way: `Recorder::resolve` emits one resolver kernel that reads its
`LateCount`s (a host provider or plain capacity is resolved in the command group and
baked in) and lets the caller's fill write what the later passes need into device
scratch, and every `kernel(DeviceSized, ...)` pass reads its width from there
(`detail/emit_late_count_resolver.hpp`, `detail/emit_device_sized.hpp`).
The fold and tile order is the one the host-sized lowering had, so results are
bit-identical. The result is one composition per tick with no mid-step readback,
no host node between passes, and `compile_count()` still 1 while the count
changes every step.

**Late-bound device values.** A device-written value the host reads during a run
must live in `Residency::Host` memory. `sized_from_device` and `based_at_device`
reject a `Residency::Device` or `Residency::Shared` buffer at the call site with
`invalid_access`. A device-written count above the node's compiled capacity makes
the kernel skip its work and write a fault record in host USM
(`api/detail/device_fault.hpp`); the driver checks every record once the step has
finished (after each step on the scheduler, after each wait on the chain) and
fails the run with `invalid_graph`. A resolver that faults hands its passes a live
count of zero, so nothing reads or writes past a buffer. On the chain with a fixed
step count the failure surfaces at the run's single wait, after the later steps
ran. A host-provided count above capacity still fails in the command group, before
the node runs. The counter's element is registered as one of the consuming task's
reads, so the producer is necessarily a predecessor.

The predecessor has finished, but other kernels may still be running on the same
device when the consumer's command group is built. On the CUDA backend, shared
USM is CUDA managed memory, and on Windows (and on GPUs older than compute
capability 6.0) the CPU may not touch any managed allocation while any kernel runs
on that GPU; the read faults with 0xC0000006.
`Integration_DeviceCount.ChainedProducerCountsInOneComposition` hit exactly that:
stage 2 read its counter while stage 1's kernel still ran. Pinned host USM is not
managed memory, so the host can read it at an arbitrary moment; on Windows WDDM
this rests on the owner's RTX 3080 Ti run, not on an NVIDIA statement. The rule
is the same whichever control carries the value: a late-bound count or base must
be a `Residency::Host` allocation, and an interior pointer into one is accepted at
registration. The raw-pointer chain setters `and_sized_from_device` /
`and_based_at_device` take no buffer, so `add()` checks their pointer instead. It
asks the address for its kind first. An offset into a per-device plan array
answers unknown there, because SYCL resolves base addresses only, so the check
then finds the recorded allocation the address lies inside and asks the base. A
slot inside a non-host allocation and a slot this context never allocated are
different faults, and the `invalid_access` says which, before the graph changes.

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

**Node names.** `Graph::name_last(const char*)` labels the task most recently
added; the label reaches `Node::metadata` when the plan is built and surfaces as
`RunReport::NodeTiming::name`. It is a separate call rather than a trailing
argument because two launch shapes end in a parameter pack whose last element is
the callable, which a defaulted trailing name cannot survive — so the choice was
one spelling for every shape against a trailing argument for most of them and an
exception for the rest. An unlabelled node reports as `unnamed_task`. The pointer
is stored, not copied, so it must outlive the graph; a string literal is the
intended use.

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

A third residency, **host** (`Residency::Host`, via `RuntimeContext::malloc_host_on`),
is pinned host USM in the owning queue's context. Kernels on that device read and
write it, `host_accessible()` is true, and the host may read it while kernels run,
which shared USM does not allow on every backend (see "Late-bound device values"
above). It is the required residency for any value the host reads mid-run.
Kernels reach it across the bus and it is page-locked RAM, so it is for small
host-read values, not bulk kernel data.

---

## 5. Core (`core/`)

The foundations every other layer is built on.

### `RefCounted` + `sushi_ptr`

Intrusive, atomic reference counting. The count lives inside the object
(`RefCounted` base), so `sushi_ptr<T>` is cheaper than `std::shared_ptr` (no
separate control block, one cache line). `make_sushi<T>(...)` constructs an owned
object; copy bumps the count atomically, move transfers it, `leak()` detaches the
raw pointer without touching the count, and `release()` drops one reference.
`Node` and `Task` are both `RefCounted`.

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
  narrows discovery, and `require_fp64` drops devices without
  `sycl::aspect::fp64` so a double-precision workload fails at selection with a
  named device rather than inside a driver at kernel launch. When the filter
  leaves no device at all, the `RuntimeContext` constructor throws a `SushiException` carrying
  `Errc::unsupported` and the active filter — the machine-has-no-SYCL-device
  case, stated plainly.
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

Two pieces: who owns each USM pointer, and when a lock-free structure may free a
node.

### 7.1 `UsmRegistry` — which device owns a pointer

`RuntimeContext::malloc_shared` / `malloc_device` / `malloc_shared_on` /
`malloc_device_on` call SYCL directly and record the owning device index in a
`UsmRegistry` together with the allocation's length; `free_usm` resolves and
erases it in one step and frees against that device's queue. USM is bound to the
context it was allocated from, so both halves matter: a free against the wrong queue is undefined, and a kernel must run
where its data lives. `context_index_for_ptr` is the lookup the scheduler's
cross-context guard uses on a node's first dispatch (§10).

The length answers a question `owner_of` cannot: which allocation an interior
pointer belongs to. `containing_allocation` scans the table for it, which is
linear in live allocations, so it stays off the dispatch path — graph
registration calls it once per raw slot pointer a caller hands over (§4). An
address adopted from outside the runtime may arrive without a length; that entry
resolves at its own base and contains nothing, so a missing size is never read as
a zero-byte allocation.

SYCL offers no portable, reliable way to ask a pointer where it came from — on
some platforms one context's USM is visible, as a different kind, from a sibling
context — so the recorded answer is authoritative and introspection remains only
a best-effort fallback for pointers the runtime did not allocate.

The registry is a map behind one mutex, and that is deliberate. Allocation
happens when the caller constructs a `Buffer` or `State`, which is a setup-time
act: a measured run allocates nothing per step, and streaming regions in and out
allocates nothing at all, because a region describes work over buffers the caller
already owns.

**There is no allocation pool.** An earlier design layered a bitmask page pool,
per-thread magazines and a remote-free stack under a `USMAllocator` facade. It
was removed: nothing on the public path reached it, its 4 KB size ceiling was
below the buffers the runtime actually allocates, and the measurement above says
there is no allocation frequency for a pool to amortise. See
`docs/archive/superpowers/specs/2026-08-18-usm-allocation-seam-design.md`.

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
- non-core concerns are grouped into cohesive sub-structs so each subsystem owns
  its own slice rather than editing one flat node: `RegionLink region` (the dynamic
  graph's cross-region boundary layer — `intra_in_degree`, `boundary_in_degree`,
  `boundary_successors`, leaf/sentinel flags) and `NodeProfiling profiling`
  (`invocations` / `device_ns` / `host_ns`, written only when profiling is on), both
  inline;
- `std::unique_ptr<NodeExtensions> ext_` — one **unconditional** pointer to lazily
  allocated optional state, so `Node`'s layout never depends on a build flag. The
  distributed `OffloadState` (`offloadable`, the `attempts` count, the `live_reads` and
  `live_writes` lists of a double-buffered node, the coordinator's `payload`) lives in
  `NodeExtensions` and is reached through
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
- when an offload coordinator is installed, calls its `prepare()`, which marks the
  nodes a worker may run and resets their `attempts`.

---

## 9. Scheduler (`scheduler/`)

Runs the ready tasks. A work-stealing scheduler with a persistent worker pool.

### 9.1 `TaskScheduler`

- One **work-stealing `Deque`** per worker (`worker_queues[MAX_WORKER_THREADS]`,
  defined in `work_stealing_deque.hpp`, a private header of the scheduler module). A worker pushes/pops its own
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
- **An idle pool costs nothing.** `disarm()` clears the armed flag once a run has
  quiesced (`ExecutionEngine::complete()` calls it after `wait_quiescent()`), and
  a disarmed worker parks on `pending_cv` with no timeout. Both halves are
  required: without the disarm the flag stayed set from the first run until the
  context died, so workers never reached the park and instead sat in the steal
  backoff — yielding, sleeping in 100 µs increments and sweeping every peer deque
  thousands of times a second on a machine the runtime is only a guest on. The
  park is untimed because every term of its predicate (`running`,
  `shutdown_requested`, and the two injector queues) is mutated under
  `pending_mutex`, so a state change cannot be missed. `disarm()` is only safe at
  quiescence: the predicate does not look at the per-worker deques, so disarming
  with work still queued would park a worker's peers and leave it to drain the
  graph alone.
- The injector drain uses a **per-worker batch buffer** (`drain_scratch_`) that
  keeps its capacity, so pulling work out of the injector does not allocate once
  the high-water mark is reached. Owned by one worker and touched only inside its
  own drain, so it needs no synchronisation.
- Completed-task counters are per-worker and `alignas(CACHE_LINE_SIZE)` to avoid
  false sharing. `get_local_steal_count()` / `get_remote_steal_count()` expose the
  steal split for telemetry and benchmarks.
- **The pool is a co-tenancy contract, not a claim on the machine.**
  `RuntimeConfig::worker_count` sizes it (0 = one per logical core, the default),
  `first_core` places it (worker *i* binds to core `first_core + i`), and
  `pinning` chooses whether to bind at all. A host that keeps its main, render and
  audio threads on cores 0–3 gives the runtime `first_core = 4` and the rest. A
  requested range that does not fit the machine is **clamped and logged**, never
  silently obeyed — the OS affinity calls accept an out-of-range mask and simply
  do not bind, so an unclamped request would report a reservation it never made.
  `Runtime::advanced().worker_pool()` reports the pool as built, which is what a
  caller verifying its reservation should read.

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

The poller reports every node the pool ran to the installed offload coordinator
through `on_local_completion()`, before the node's successors are released. The
distributed master raises the versions of the shared buffers the node wrote there.
A node a worker ran comes back through `OffloadSink::complete_remote()`, which
releases its successors on the same path.

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

It is **off by default and opt-in**: set `RuntimeConfig::rebalancer = true` at
creation, or call `Runtime::rebalancer(true)` (via
`ExecutionEngine::set_rebalancer_enabled`) later. Mid-run task migration is itself
a source of run-to-run variation, and the heartbeat is a periodic wakeup under
every frame — both are costs a simulation pays for nothing, and simulation is the
primary workload. Batch throughput is what the trade is worth making for, so
batch turns it on.

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

`RuntimeContext` knows the distributed layer only as a `Graph::OffloadCoordinator`:
`install_offload()` installs one and `offload()` returns it, or `nullptr`. The
type is the same in every build. With `SR_ENABLE_DISTRIBUTED`,
`Distributed::Cluster::attach()` builds the buffer registry and the coordinator,
binds the coordinator to `offload_sink()`, starts it, and only then installs it in
place of any coordinator installed before. It returns the handle that carries the
knobs, the statistics and the buffer version calls.

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
  successor lists, EBR, and refcounts are all lock-free. The
  only mutex on a critical path guards the global injector queue.
- **Cache-line alignment to kill false sharing.** `ResourceState`, EBR `ThreadEpochSlot`,
  and per-worker counters are all `alignas(64)`.
- **Acquire/release discipline.** Successor release uses `fetch_sub(acq_rel)`;
  epoch publication uses release stores and acquire loads; the "drove in_degree to
  zero" check is the single synchronization point that makes a successor visible
  to exactly one worker.
- **Sharding to spread contention.** 1024 dependency shards, per-worker deques,
  per-thread magazines and retire lists — contention is partitioned, not locked.

---

## 12. Distributed (`distributed/`, optional)

Built only with `SR_ENABLE_DISTRIBUTED` (CMake), which also defines `SR_DISTRIBUTED`
for consumers. When off, none of it compiles and no header of another module names
it. How to run it is in [`DISTRIBUTED_GUIDE.md`](../guides/DISTRIBUTED_GUIDE.md); how
each class works is in `modules/extension/distributed/README.md`. What the rest of
the runtime needs to know:

- **Only references cross the wire.** A task ships its `OpID`, which names a kernel
  compiled into the worker, and `BufferHandle`s (`GlobalBufferID`, `version`,
  `size_bytes`). Closures and pointers do not travel.

- **The seam is in the engine.** The scheduler, the graph and the context know one
  interface, `Graph::OffloadCoordinator` (`task/offload_coordinator.hpp`):
  `prepare()` at compile, `try_offload()` at dispatch for a node whose
  `OffloadState::offloadable` is set, `on_local_completion()` after a local run,
  `share()` for a buffer, `stop()` at shutdown. With no coordinator installed the
  dispatch loop pays one null test per node. A coordinator hands nodes back through `Graph::OffloadSink`
  (`complete_remote()`, `resubmit()`), which the scheduler implements.
  `Distributed::Cluster::attach()` installs the one implementer,
  `DistributedCoordinator`, a private class of the module.

- **Protocol version 2** is a private codec (`source/wire_frame.hpp`,
  `source/wire_messages.hpp`): a 12-byte little-endian header with a magic, the
  version, the type, flags and a payload length of at most 1 MiB, then one of the
  sixteen messages `MessageType` names. Fields have fixed widths, so the frame
  layout does not depend on the machine. Buffer bytes travel in `BUFFER_CHUNK`
  frames of 256 KiB by default, so a buffer is not bounded by a frame.

- **The handshake authenticates both ends.** `HELLO`, `CHALLENGE`, `PROOF` and
  `WELCOME` carry two nonces and two HMAC-SHA256 proofs over a `SharedSecret`.
  SHA-256 and HMAC are written in the module. Frames after the handshake are
  neither encrypted nor signed; the layer is for a trusted LAN.

- **`ITransport`** moves frames and reports what it sees through `TransportEvents`:
  `on_peer_connected()`, `on_frame()`, `on_peer_lost()`, `on_connect_abandoned()`.
  `send()` queues a control frame and `send_stream()` hands over a `FrameSource`
  that yields the frames of one bulk transfer; neither waits for a socket. Two
  implementations:
  - `InProcessTransport` over an `InProcessFabric`, which joins a master and any
    number of workers inside one process and takes a `FaultPolicy`. It is the
    network analogue of `RemappingTopologyProvider`.
  - `TcpTransport` over the platform's sockets, with a reader thread and a writer
    thread per connection: `TcpTransport::make_master(TcpMasterOptions)` and
    `TcpTransport::make_worker(Endpoint, TcpWorkerOptions)`.

- **`KernelRegistry`** maps an `OpID` to a `KernelRegistry::Kernel`, a function
  that receives the task's buffers as `span<const BufferView>` and returns a SYCL
  event. A worker registers its kernels before it starts; the master sends only
  the `OpID`.

- **The master is composed of bricks**, each a private class with at most one
  mutex: `BufferRegistry` (ids and versions of the shared buffers),
  `WorkerDirectory` (sessions and the handshake state), `PendingTasks` (the tasks
  out on a worker and their nodes), `BufferExchange` (chunks in and out),
  `OffloadDispatcher` (`prepare` and `try_offload`) and `ClusterMonitor` (pings,
  liveness, deadlines). `DistributedCoordinator` builds them and routes frames to
  them.

- **The master owns every shared buffer.** A write raises the buffer's `version`. A
  worker caches what it pulled and pulls again when the version has moved. A
  worker's output chunks are written into the master's buffer in place, and only
  while their task is still out and their version is the one dispatched.

- **A task that cannot be trusted to finish comes back.** The monitor closes a
  worker that is silent for the liveness timeout (5 s by default) and takes back a
  task past its deadline (30 s), one no worker confirmed, or one a worker stopped
  holding. A lost worker's tasks are taken back at once. Each goes to
  `OffloadSink::resubmit()`, and after `K_MAXIMUM_OFFLOAD_ATTEMPTS` (2) dispatches
  in one run the node runs locally. `ClusterStatistics` counts each of these.

- **`WorkerAgent`** runs on a `RuntimeContext` of its own: it registers with the
  master, pulls the inputs of each dispatched task into a cache, runs the kernel on
  the queue of device 0, one task at a time, and streams the outputs back.
  `run_worker()` wraps it in a whole process: flags, the secret file, signals,
  reconnection and exit codes.

- **`IOffloadPolicy`** decides where a task goes, or that it stays. The default
  `LeastLoadedOffloadPolicy` keeps a task whose inputs total under
  `min_offload_bytes` (64 KiB) local, skips a worker without the free device memory
  for the inputs, and picks the lowest estimate of round trip, transfer time and
  backlog from `WorkerStats`. Declining is always safe: the task runs locally.

A declined or failed offload falls back to the local path, which is the same path a
standalone build runs.

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
| `SR_CUDA_ARCH` | *none* | CUDA compute capability for the nvptx64 backend; resolved from `nvidia-smi`, or stated (`86`, or `61;86` for a multi-arch binary) |
| `SR_AMD_ARCH` | *none* | AMD GPU ISA for the amdgcn backend; resolved from `rocm_agent_enumerator`, or stated (`gfx1030`, or `gfx1030;gfx1100`) |
| `SR_ENABLE_CUDA` | OFF | **Legacy alias** for `SR_GPU_BACKEND=cuda`; kept for back-compat |
| `SR_ENABLE_DISTRIBUTED` | OFF | compile the master-worker layer |
| `SR_BUILD_TESTS` | OFF | build the test suite (`sr build` turns this ON; pass `--no-test` to skip) |
| `SR_BUILD_BENCHMARKS` | ON | build the heavy `sr_benchmark_tests` binary |
| `SR_DETERMINISTIC_FP` | ON | forbid fast-math and FMA contraction in every TU, including the consumer's |
| `SR_NATIVE_ARCH` | OFF | build for this machine's exact ISA (`-march=native` / `/arch:AVX2`) |

### Compile policy and what crosses the library boundary

Four INTERFACE targets carry the compile settings, composed by
`cmake/BuildPolicy.cmake` and applied through `sushiruntime_apply_build_policy(<target>)`:

| Target | Carries | Linkage |
|--------|---------|---------|
| `sushiruntime_warnings` | `-Wall -Wextra -Werror -pedantic` / `/W4 /permissive-` | PRIVATE |
| `sushiruntime_perf` | `-ftls-model=initial-exec`, and `-march=native` only under `SR_NATIVE_ARCH` | PRIVATE |
| `sushiruntime_sanitizers` | TSan on Debug/RelWithDebInfo (non-Windows) | PRIVATE |
| `sushiruntime_deterministic_fp` | `-ffp-contract=off -fno-fast-math` / `/fp:precise` (+ a probed contraction switch, below) | **PUBLIC, exported** |

The split is the mechanism behind the determinism guarantee in §15, and behind
the promise that an out-of-tree consumer inherits nothing it did not ask for.
Three of the four are a *development* standard — a downstream build should never
fail because this project's dependency turned on `-Werror`, and a binary should
never carry the ISA of the machine that happened to compile it. The fourth is
different in kind: the kernels are header templates, so a consumer's own
translation units instantiate and compile the arithmetic. Byte-equal replay is
only true if the consumer compiles under the same floating-point semantics, so
that one target is PUBLIC on `sushiruntime` and travels through the installed
package.

The optimization level itself is not in any of them — CMake's own
`CMAKE_CXX_FLAGS_<CONFIG>` already supplies `-O3` / `/O2` in Release.

Model and contraction are separate axes, and the MSVC-style command line only
covers the first. `icx-cl` documents its contraction default as `fast` for every
floating-point model except `strict`, so `/fp:precise` alone would still let a
Windows oneAPI build fuse a multiply and an add and round once where the source
says twice — a different result from the same source on a Linux lane. On an
MSVC-frontend compiler that is not `cl.exe`, the build therefore probes for a
contraction switch (`-ffp-contract=off`, then `/Qfp-contract=off`) with
`check_cxx_compiler_flag` and adds whichever the driver accepts, warning if
neither does. Probed rather than assumed because the accepted spelling is a
property of the driver, not of the frontend variant, and a wrong guess would be
silently dropped in a consumer's build. `cl.exe` proper needs nothing: it does
not contract unless asked with `/fp:contract`.

The suite that holds all of this honest is `Integration_Determinism`
(`tests/integration/test_determinism.cpp`): the same graph run at
worker counts {1, 2, whole machine} with the rebalancer off, compared on the raw
bytes of its output buffer, plus a check that no fast-math macro is defined in a
translation unit outside the library — evidence the FP contract really did cross
the boundary rather than stopping at the library edge.

### GPU backend selection

`SR_GPU_BACKEND` is the single knob that picks the accelerator target; it
resolves in `cmake/gpu/` to exactly one vendor strategy:

- **`auto`** (default) — probe the host *compiler* toolchain (not just a driver):
  `nvcc` → `cuda`, else `hipcc`/`rocminfo` → `rocm`, else the portable
  **`cpu`** (OpenCL / `spir64` JIT) path. The apt CUDA and ROCm toolkits do not
  add themselves to `PATH`, so auto-detect also searches their well-known
  prefixes (`/usr/local/cuda*`, `/opt/rocm*`).
- **`cuda`** — SYCL CUDA, one `nvidia_gpu_sm_*` target per `SR_CUDA_ARCH` entry.
- **`rocm`** — AMD ROCm/HIP, one `amd_gpu_gfx*` target per `SR_AMD_ARCH` entry.
- **`intel`** — Intel GPU (Level Zero).
- **`cpu`** / **`none`** — CPU/OpenCL fallback (`none` also skips any offload triple).

The CLI wraps this: `sr build` passes `SR_GPU_BACKEND=auto` and reads the
resolved backend back, so the vendor logic lives in one place (`cmake/gpu/`)
rather than being duplicated in Python.

Neither `SR_CUDA_ARCH` nor `SR_AMD_ARCH` has a default. Each is resolved by its
own backend file (`cmake/gpu/Cuda.cmake`, `cmake/gpu/Rocm.cmake`) from the
vendor's own probe — `nvidia-smi` and `rocm_agent_enumerator` — and what the
probe reports is taken whole:

- **One architecture** — build for it.
- **Several different ones** — build for all of them, and say so in a `STATUS`
  line naming what was found and the flag that narrows it. Electing one would be
  the guess; targeting the set invents nothing and discards nothing. Compile time
  and binary size scale with the count, which is why it is announced rather than
  done quietly.
- **None** — there is nothing to resolve, and what happens next depends on who
  asked. See below.

A default is refused because it would stand in for hardware nobody looked at,
and on NVIDIA the substitution is invisible: forward-compatible PTX means a wrong
`sm_` still runs, just without the codegen you asked for. On AMD it is worse —
amdgcn compiles straight to final ISA, so a wrong arch yields a binary the GPU
cannot run at all.

The "none" case splits on intent. `auto` picks a backend by finding its
*toolkit* — `nvcc`, `hipcc` — which proves a compiler is installed, not that a
device exists; a CI or artifact box typically has the first without the second.
Failing there would make `auto` a mode that dies on a machine it was meant to
figure out, so the vendor file *declines*: it warns, sets
`SR_GPU_BACKEND_DECLINED`, and `DetectGpu.cmake` demotes the build to the CPU
strategy and corrects `SR_GPU_BACKEND_RESOLVED`. A backend the caller named
(`-DSR_GPU_BACKEND=cuda`, or the legacy `-DSR_ENABLE_CUDA=ON`) never declines —
it errors, because naming a backend without naming its architecture is the
omission this design exists to refuse.

The resolution lives in the backend files rather than in `GpuBackend.cmake`
because that file is included unconditionally, so a failure there would break
every unrelated build.

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
6. **Same-binary, same-device replay is byte-equal.** The schedule never decides
   which values a kernel combines, and the floating-point contract (§14)
   forbids the reassociation and FMA contraction that would otherwise let two
   builds of the same source disagree. Anything that weakens this silently — a
   build flag, a default-on rebalancer, schedule-dependent arithmetic — is a
   defect even when it is faster. Cross-architecture equality is explicitly not
   claimed. `Integration_Determinism` is where this stops being a promise: it
   varies the worker count and compares the output bytes, so a change that makes
   the arithmetic schedule-dependent fails there rather than in a consumer's
   replay a thousand steps later.

---

## 16. Directory map

```
modules/<tier>/<module>/   README.md, CMakeLists.txt, include/SushiRuntime/<module>/,
                           source/, tests/. Tiers, lowest first:
  foundation/  core        RefCounted, sushi_ptr, span, bit, Error/Result, export macro
               logging     the logger and the SR_LOG macros
               sycl        sycl_compat (toolchain differences)
  platform/    topology    HardwareDiscovery, NUMAMapper, ITopologyProvider,
                           INumaDistanceModel, TopologyView, affinity
               memory      UsmAllocator, UsmRegistry, resource regions, EpochManager (EBR)
               device      DeviceQueueSet, DeviceInfo, QueueFlavour
               observability  RunReport, ProfilingLedger, TransferCounters
  engine/      task        Task, Node, task_types (OpID/TaskMetadata), late bindings,
                           OffloadState/OffloadSink/OffloadCoordinator
               scheduler   TaskScheduler, RangeTask, EventPolling, WorkloadCoordinator,
                           rebalancer_interfaces, ThermalProxyStrategy
               graph       TaskGraph, CompiledGraph, DependencyTracker
               execution   RuntimeContext, IExecutionEngine/ExecutionEngine,
                           ExecutionMetrics
  surface/     interop     DeviceUuid, ExternalMemoryDescriptor, InteropCapabilities
               api         Runtime, Buffer/State, API::Graph, API::DynamicGraph / Region,
                           API::Interop, the umbrella header SushiRuntime.h
  extension/   distributed (optional) Cluster, ITransport and its two transports,
                           KernelRegistry, IOffloadPolicy, WorkerAgent, run_worker;
                           privately the wire codec, the handshake and the bricks
cmake/                     build functions; SushiRuntimeTiers.cmake states the tier order
                           and the edges allowed inside a tier
tests/                     tests that cross modules: unit/, integration/, regression/,
                           allocation/, benchmark/, package/
applications/examples/     fluent tutorials: quickstart + *_demo (always built);
                           distributed_demo, distributed_worker (distributed builds only)
docs/                      the manual, indexed by docs/README.md
```

For how to run any of this, see the [CLI guide](../guides/CLI_GUIDE.md); for how to write
against it, the [introduction](../getting_started/INTRODUCTION.md).
