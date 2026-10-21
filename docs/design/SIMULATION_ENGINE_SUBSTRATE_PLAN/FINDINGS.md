# Findings: what the runtime is and where it breaks

## 1. What the runtime actually is, mechanically

The fluent API (`modules/surface/api/include/SushiRuntime/api/runtime/runtime.hpp`) is the intended entry
point. The mechanical pipeline underneath it is:

1. **Allocate data as handles.** `Buffer<T>` and `State<T>`
   (`modules/surface/api/include/SushiRuntime/api/data/buffer.hpp`) own USM allocations.
   `State<T>` is a double buffer (`current()` / `next()`, swapped by `advance()`,
   `buffer.hpp:281`). The dependency-tracking identity of a buffer is **its base
   pointer** (`buffer.hpp:164`); a `State` exposes its current-buffer pointer as
   the read key and its next-buffer pointer as the write key (`buffer.hpp:269-272`).

2. **Describe work as `add()` calls.** Each `add()` overload records a *spec*
   closure into `specs_` (`runtime.hpp:1147`) and sets `dirty_ = true`. The spec,
   when replayed, calls `TaskGraph::add_node` / `add_task` with read/write pointer
   lists.

3. **Compile once.** On first run (or after any mutation), `build_plan()`
   (`runtime.hpp:1124`) constructs a fresh `TaskGraph`, replays every spec into it,
   and calls `compile()`. `compile()` (`modules/engine/graph/source/task_graph.cpp:471`) drains the
   builder's lock-free node stacks into a `CompiledGraph`, resolves roots and
   leaves, links a sentinel, and **resets the dependency tracker**
   (`task_graph.cpp:528`). The compiled plan then snapshots its in-degrees
   (`capture_base_state`, `execution_engine.cpp:292`).

4. **Replay N times.** `run(n)` loops: re-point any dynamic IO, `rearm()` the plan
   (restore in-degrees from the snapshot, `execution_engine.cpp:308`),
   `engine.execute(plan_)`, then `advance()` every `State` (`runtime.hpp:965-982`).
   The graph is **not** recompiled between steps as long as no `add()` happened.

5. **Execute.** `ExecutionEngine::execute` (`modules/engine/execution/source/execution_engine.cpp:174`)
   wires the sentinel's completion promise, arms the persistent
   `TaskScheduler`, submits root nodes, and **blocks the caller on a
   `std::future`** until the sentinel fires. The scheduler is a NUMA-aware
   work-stealing pool (`modules/engine/scheduler/source/task_scheduler.cpp`) with per-worker Chase-Lev
   deques and a global injector. Completion is detected by `EventPolling`
   (`modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp`) polling SYCL events and
   decrementing successors' in-degrees.

The single most important property of this pipeline for everything below:

> **The compiled plan is static in topology and static in per-node iteration size.
> Replay is cheap precisely because nothing about the graph's shape can change
> between steps.** Changing the shape means recompiling, which rebuilds the graph
> and the dependency history from scratch.

A game/ECS/large world violates that assumption every frame. Sections 4–9 are
about resolving that violation without throwing away what the runtime does well.

## 2. What it does well (keep these)

These are genuine strengths and the reason the goal is realistic at all:

- **Automatic data-driven ordering is the hard part of an ECS scheduler, and it
  exists.** `DependencyTracker::resolve` (`modules/engine/dependency_tracking/source/dependency_tracker.cpp:66`)
  derives RAW/WAR/WAW edges from declared read/write sets. An ECS "system schedule"
(run systems in parallel unless their component access conflicts) is exactly
  this mechanism with components as the tracked resources.
- **`State<T>` is a correct, ready primitive for stepped fields.** Read-current /
  write-next with an O(1) swap is the Jacobi update every stencil/PDE/cellular
  step wants, and it is already wired through the fluent layer including the
  multi-field coupled form (`runtime.hpp:597-609`).
- **Heterogeneous CPU+GPU in one graph**, with data-ownership-correct dispatch:
  the scheduler routes a node to the queue whose context owns its data
  (`scheduler.cpp:551-558`, `RuntimeContext::get_queue_for_ptr`).
- **NUMA-aware work stealing** with a precomputed distance-ordered victim table
  (`scheduler.cpp:210-257`, `find_task_to_steal` at `:609`): most engines never
  get this.
- **Persistent worker pool with cheap re-arm** (`arm_for_execution`,
  `scheduler.cpp:277`): no thread churn between steps.
- **Lock-free hot paths** for scheduling, dependency edges, memory pool, and EBR.
- **The execution strategy is already an interface** (`IExecutionEngine`,
  `execution_engine.hpp:121`). A deterministic/replay engine can be added without
  touching `TaskGraph` or `RuntimeContext`. This is the seam determinism will use.
- **An opt-in, zero-overhead-when-off distributed seam** already threads through
  `compile()` stamping and a single scheduler branch (`scheduler.cpp:483-489`).

## 3. Findings: correctness and robustness (BLOCKER class)

These will manifest as a dead or wrong runtime at the scale the goal implies. They
are independent of which target shape is chosen and must be fixed first.

### 3.1 BLOCKER: `EventPolling` silently drops completion events past 256 in-flight

`ActiveEventList` is a fixed 256-slot array (`event_polling.hpp:68`). `push`
returns `false` when full (`event_polling.hpp:74-83`). The scheduler's reaction to
a full list is to **log a warning and discard the event**
(`scheduler.cpp:580-581`):

```cpp
const bool ready = (e == sycl::event{});
if (!events.push({e, task, ready}))
    SR_LOG_WARN("ActiveEventList capacity is full, events cannot be polled!");
```

A discarded event is never polled, so `trigger_successors` never runs for that
task, so its successors' `in_degree` never reaches zero, so the **sentinel never
fires and `execute()` blocks forever** (`execution_engine.cpp:247`). The task also
leaks its scheduling ref (`task->release()` only happens on completion,
`event_polling.hpp:175-176`).

Important nuance: `ActiveEventList` is **per-worker**; it is a local in
`dispatch_loop` (`scheduler.cpp:416`). So the true ceiling is 256 concurrently
in-flight events *on a single worker*, i.e. up to `256 × num_workers` globally, but
unevenly. A wide DAG level (thousands of independent systems or grid chunks
becoming ready together) can pile >256 onto one worker and deadlock
non-deterministically. A "very large world" produces exactly these wide levels.

**Fix:** make the active list grow (spill to a `std::vector` beyond the inline 256,
mirroring `SmallEventVector` at `node.hpp:56`) **or** apply back-pressure: when the
list is full, the worker must process the completion path before executing more
tasks rather than dropping. Dropping must never be an option. Acceptance: a
synthetic graph with one root fanning out to 10⁵ independent leaves on a 1-worker
configuration completes.

### 3.2 BLOCKER: a single task exception permanently kills the runtime

`execute_task`'s catch block (`scheduler.cpp:583-593`) does:

```cpp
catch (...) {
    if (error_callback) error_callback(std::current_exception());
    shutdown_requested.store(true, std::memory_order_release);
    running.store(false, std::memory_order_release);
}
```

Setting `shutdown_requested = true` makes every worker break out of `dispatch_loop`
(`scheduler.cpp:500-501`) and exit. There is **no path that clears
`shutdown_requested`**: `arm_for_execution` only sets `running` (`scheduler.cpp:277`).
So after *one* kernel/host exception anywhere, the worker pool is gone and the
`RuntimeContext` is unusable for the rest of the process. The error surfaces to the
caller via the sentinel promise (`execution_engine.cpp:193-203, 250-254`), but the
engine cannot be restarted.

For a one-shot HPC job this is acceptable. For a long-running engine (where frame
1,000,000 must survive a transient bad kernel, a NaN guard, or a recoverable device
error) it is fatal: one bad frame ends the simulation.

**Fix:** separate "this run failed" from "the pool is dead." A task exception should
fail the *current* `execute()` (it already does) without latching
`shutdown_requested`; the pool should drain the current round and remain re-armable.
This pairs with cancellation (findings 3.3). Acceptance: a graph whose kernel throws on step
K still allows a fresh graph to run on the same `Runtime` afterward.

### 3.3 BLOCKER (for real-time): no cancellation / frame-abort mechanism

There is no cancellation token anywhere. Once `execute()` submits roots it blocks
until the sentinel fires (`execution_engine.cpp:245-256`); a worker only stops at
shutdown. A real-time engine needs to (a) abort an over-budget frame and (b) stop a
run from another thread on user input or a failed invariant.

**Fix:** add an `std::atomic<bool>` cancellation flag on the run, checked by
`dispatch_loop` and `EventPolling`; on cancel, stop dispatching new nodes, let
in-flight events drain, and fire the sentinel with a "cancelled" status. This is the
cancellation mechanism a long-running engine needs. Acceptance: a separate
thread can cancel a `run_until` mid-flight and `execute()` returns within one task
quantum.

### 3.4 BLOCKER (latent): Chase-Lev deque overflow drops tasks

`Deque::push` discards the task with an error log when `b - t >= CAPACITY`
(`scheduler.cpp:102-106`); `CAPACITY = 262144` per worker (`scheduler.cpp:64`).
`drain_pending_submissions` guards the injector→deque path
(`scheduler.cpp:363-364`), but `split()` re-pushes (`scheduler.cpp:471-472`) and
`trigger_successors` pushes directly (`event_polling.hpp:246, 260`) with no guard.
262K per worker is large, but a massive world with deep fan-out is exactly the
workload that can reach it, and the failure mode is a silently dropped task →
unsatisfied dependency → deadlock, same shape as findings 3.1.

**Fix:** the deque must not drop. Either spill on overflow or assert-and-grow.
Acceptance: a graph that intentionally drives one worker's ready set past 262K
completes.

### 3.5 STRUCTURAL: `ThreadLocalMagazine` teardown depends on thread-exit ordering

`ThreadLocalMagazine` (`memory_pool.hpp:95`) is `thread_local`; on thread exit it
flushes live USM slots back to the pool. The teardown order in
`RuntimeContext::Impl::~Impl` (`runtime_context.cpp:114-140`) shuts the engine down
first (joining all worker threads while the allocator still lives), so **worker
threads are safe by ordering**. The hazard is **application-owned threads** that
allocate via the context and exit *after* `RuntimeContext` is destroyed: their
magazine destructor then touches a freed pool (use-after-free). An engine will have
such threads (a render thread, an asset thread, a job-system thread).

**Fix:** gate the magazine destructor on a liveness flag published by the pool/
allocator, so a flush after pool destruction becomes a no-op. Acceptance: an
application thread that allocates, then outlives `RuntimeContext`, exits cleanly
under ASan.

### 3.6 PERF/STRUCTURAL: multi-allocator "flush cliff" hurts multi-GPU

The magazine caches one allocator id; allocating from a different `USMAllocator`
flushes the **entire** magazine first (`memory_pool.hpp:82-94`). A multi-GPU world
that interleaves allocations across device contexts pays a full flush per switch.
Relevant the moment the "large world" spans more than one device.

**Fix:** per-allocator magazines, or a small per-allocator bucket set, so switching
device context does not nuke the cache. Acceptance: an alloc/free microbench
alternating two allocators shows no flush spike.

## 4. Findings: the central architectural tension (STRUCTURAL)

Everything in the target shape that is hard reduces to one mismatch.

### 4.1 The plan is static in topology and in per-node size

- **Topology is fixed at compile.** Any `add()` sets `dirty_` (`runtime.hpp:295`
  etc.); the next run calls `build_plan()` which constructs a brand-new `TaskGraph`,
  replays all specs, and `compile()`s (`runtime.hpp:1124`). There is no incremental
  edit of a compiled plan.
- **Per-node iteration size is baked at compile.** In `add(State<T>&, fn)` the
  element count is read inside the spec at build time:
  `const std::size_t n = sp->size();` (`runtime.hpp:516`), captured into the kernel.
  The double-buffer swap is followed at dispatch via `StateView::cur/next`
  (`buffer.hpp:49`, re-read each submit), but **only the pointers move, not `n`**.

So a `State` whose element count changes between steps (entities spawned/destroyed,
a changing number of active cells, a varying collision-pair count) requires a
recompile.

### 4.2 Recompile cost is real and scales with the world

`compile()` is not free at scale:

- Every read/write declaration does a heap `new AccessNode`
  (`dependency_tracker.cpp:73`) and a backward traversal of the per-shard history
  (`dependency_tracker.cpp:86-120`).
- For a write to a heavily-shared resource, the traversal walks **all prior reads**
  back to the previous write (`dependency_tracker.cpp:91-101`): O(readers). A
  component read by R systems then written by one costs O(R); across a frame of such
  components this trends O(systems²) on the hot components.
- `compile()` then drains and `reset()`s the tracker, retiring every `AccessNode`
  through EBR (`dependency_tracker.cpp:40-54`).

Doing this **every frame** for a 10⁴–10⁶ entity/cell world is the dominant cost and
defeats the "compile once, replay" advantage.

### 4.3 The mechanism to fix this already exists in embryonic form

The distributed path solved the "pointers rotate every step but I don't want to
recompile" problem with `live_reads_` / `live_writes_`: `shared_ptr<vector<void*>>`
on the node, re-pointed before each step, resolved at dispatch
(`node.hpp:189-201`, `add_offload_dynamic` at `task_graph.cpp:425`,
`dist_io_updaters_` at `runtime.hpp:1156`). This is the seed of the general
solution: a node whose *bindings* (pointers, and crucially **size**, and an
**enabled** flag) are late-bound and updated per step, on a graph whose *topology*
stays compiled.

### 4.4 Resource identity is a raw base pointer: wrong granularity for chunks/halos

Dependencies are keyed on the base pointer (`buffer.hpp:164`), hashed into 1024
shards (`dependency_tracker.hpp:79-103`). Two consequences the target hits hard
(both independently verifiable from the code cited above):

- **No sub-region identity.** A grid's interior and its halo border share one base
  pointer, so they are seen as the same resource (false serialization). Conversely,
  if you give archetype chunks or sub-grids *distinct* base pointers, two tasks
  touching overlapping regions through different bases are **not ordered at all**:
a silent data race. ECS chunked storage and grid domain decomposition both live
  squarely in this trap.
- **Shard collisions create spurious edges.** Distinct pointers can hash to the same
  shard (`dependency_tracker.hpp:96-103`); a collision never drops a real dependency
  but can serialize two unrelated systems, costing parallelism. At 10⁵ component
  arrays / chunks, collisions in 1024 shards are frequent.

This is the single highest-leverage *correctness* redesign for ECS-at-scale and the
prerequisite for halo-based domain decomposition (the domain decomposition page).

## 5. Findings: performance and scaling (PERF)

Correct today, but the constant factors are wrong for a real-time frame or a huge
world.

### 5.1 Per-step `std::promise`/`std::future` + `shared_ptr` churn

Every `execute()` (i.e. **every step** of `run(n)`) allocates two
`std::promise`/`std::future` pairs and several `shared_ptr`s and installs a
capturing error callback (`execution_engine.cpp:186-203`), then blocks the caller
thread on a future (`:247`). For a million-step run that is millions of promise/
future/shared_ptr allocations plus a kernel-thread handoff per step. At 60–240 Hz
this is measurable frame overhead and avoidable jitter.

**Fix:** hoist the completion-signalling primitive out of per-step. A replayed plan
can own one reusable completion latch (`std::atomic` + futex/`atomic_wait`, or a
reset-able binary semaphore) instead of constructing a promise per step.

### 5.2 RTTI on the hot path

`execute_task` does `dynamic_cast<Graph::Node*>(task)` for **every task**
(`scheduler.cpp:535`) to read `numa_affinity` and deps. `EventPolling` uses a
virtual `is_graph_node()` + `static_cast` for the same downcast
(`event_polling.hpp:229-234`): two different strategies for the same need. At
millions of node-dispatches/frame, `dynamic_cast` is a real tax and the inconsistent
approach is also an unresolved Liskov-substitution concern (the core scheduling
path is coupled to the derived `Node` type).

**Fix:** one type-blind virtual (`on_dispatch`/`on_complete`) on `Task`, no
`dynamic_cast`. Removes both the RTTI cost and the LSP violation.

### 5.3 Always-on thermal rebalancer adds a background thread and nondeterminism

`ExecutionEngine::Impl` unconditionally constructs and starts a `WorkloadCoordinator`
with a `ThermalProxyStrategy` (`execution_engine.cpp:69-75`). It migrates tasks
between workers based on queue-length/throughput, via the global priority queue
(`scheduler.cpp:721-749`). For throughput this is good; for a real-time frame and
especially for **determinism** (multiplayer/replay) it injects run-to-run variation
and a background thread doing work mid-frame.

**Fix:** make the rebalancer opt-in (and off in deterministic mode). Acceptance:
deterministic mode runs with the rebalancer absent and no background migration.

### 5.4 Single-pointer queue anchoring limits multi-device systems

The scheduler anchors a node to the queue owning its **first** write (else first
read) pointer (`scheduler.cpp:551-558`). A node that legitimately touches USM from
two different device contexts cannot be expressed correctly: it will run on one
context and fault on the other. ECS systems that join components living on different
devices, or grid kernels straddling a device boundary, hit this.

**Fix (design constraint, not necessarily code):** the ECS/field layer must keep a
system's inputs co-located, or the runtime must support split-by-locality
sub-dispatch. Document and enforce co-location at the layer above; revisit only if
cross-device systems become common.

### 5.5 `RunReport` reserved fields and observability gaps

Several `RunReport` fields are always zero: scheduling
overhead, H2D/D2H bytes, network counters. An engine needs per-system timing, per
phase frame breakdown, and stall attribution to tune a frame budget. Today the only
real signals are steal counts and total durations.

**Fix:** add per-node (per-system) timing capture in the engine and surface a
structured frame report. This is also what makes findings 5.1 and 5.3 tunable rather than
guessed.


