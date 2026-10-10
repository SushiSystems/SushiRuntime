# SushiRuntime as a Simulation/Game-Engine Substrate — Engineering Plan

**Status:** Open — WP-4 is delivered, WP-5 and WP-7 are not built; the other work packages were not re-checked against the tree on 2026-10-05.

Research and engineering specification. No code was changed by this
document. Every claim below is anchored to a location (`file:line`) in the tree as it stood
when the claim was written; the anchors have not been refreshed since.

**Goal of the host project (stated by the owner):** make SushiRuntime the *brain*
of a large simulation engine whose task graph behaves like a game engine. The
prioritised target shape is:

1. an **ECS** (entity / component / system) world, plus
2. **field / grid / node** operations (stencils, PDE-like updates, cellular work),
3. ultimately scaling to a **single, very large multi-machine world**.

This document answers three questions precisely:

- What does SushiRuntime actually do today, at the level that matters for that goal?
- Where does the current design hold, and where does it break, for each of the
  three target shapes?
- What is the exact engineering required to close the gap — broken into ordered
  work packages with designs, touch-points, risks, and acceptance criteria?

---

## 0. How to read this document

The runtime is, correctly, an **orchestrator**, not a compute library. This is a
foundational design principle of the project: the runtime decides
*when / where / in what order* work runs; domains (physics, AI, BLAS, and an ECS)
decide *what is computed* and live on top as pure compute layers.

That decision is sound and this plan does **not** propose changing it. The plan
keeps engine concepts (entities, transforms, collisions, scenes) **out** of the
runtime core and builds them as a layer above it. What the plan *does* propose is
changing the runtime's execution and dependency model where it is structurally
incompatible with a per-frame, dynamically-shaped, multi-machine world.

Throughout, three labels are used:

- **BLOCKER** — will cause incorrect results, deadlock, or a dead runtime at the
  scale the goal implies. Must be fixed before building anything on top.
- **STRUCTURAL** — the current design works but is built on an assumption the
  target violates; needs a real design change, not a patch.
- **PERF** — correctness is fine, but the constant factor or scaling is wrong for
  a real-time frame budget or a massive entity/cell count.

---

## 0.1 Locked context (owner decisions for this engagement)

The plan below is written against the following decisions, taken explicitly by the
project owner. They are not assumptions; they reshape the priorities in §7–§10.

| # | Decision | Engineering consequence |
|---|---|---|
| L1 | **Real-time interactive**, variable / scene-dependent frame budget (sometimes ~144 Hz interactive, sometimes a heavy sim step). | Both a low-latency and a high-throughput mode are needed. Cancellation/frame-abort (WP-0) and per-step overhead removal (§5.1) are first-class, not optional. |
| L2 | **Scale 10⁶–10⁹** entities/cells per machine. | Region identity (WP-2) and a **device-resident** state path are mandatory, not optional. Per-frame recompile is untenable → WP-1 mandatory. |
| L3 | **Determinism required in v1**, for **server-authoritative** replay/debug, **same binary/architecture only** (no cross-arch, no lockstep sync). | No fixed-point math needed. Determinism reduces to **fixed reduction order** (see refined WP-4); a separate single-threaded engine may not be required. |
| L4 | **Node hardware is a heterogeneous mix** (single-GPU, multi-GPU, CPU-only nodes coexist). | Cross-context queue routing (§5.4) and per-allocator magazines (§3.6) become load-bearing, not edge cases. |
| L5 | **Cluster size unspecified → design for hundreds of machines.** | The distributed model must be **peer-to-peer** with hierarchical partitioning, not a single star master (§8–§9). |
| L6 | **Massive-world MVP is field/cell-only**, BUT the seams must be SOLID enough that **entity migration across machines is the easiest possible later addition**. | Region **ownership** must be a first-class abstraction from day one (WP-2/WP-5), even though the MVP never migrates an entity. |
| L7 | **Full physics is in scope:** particle/N-body, grid/field PDE, soft-body/constraint, **and rigid body**, with **collision detection as a central feature**. | Frame-varying graph topology is guaranteed (collision pairs change every frame) → WP-1 critical. Solvers need ordered islands. |
| L8 | **Constraint/rigid solver style is Gauss-Seidel / PGS (sequential).** | Needs **graph coloring** to parallelize within a colour and ordered dependency chains across colours. The dependency tracker provides the ordering; the ECS/physics layer provides the colouring. |
| L9 | **Simulation state is GPU-resident and persistent** across frames; host reads only on demand. | The current shared-USM `Buffer/State` (`buffer.hpp`) is insufficient; a **device-USM state** with selective readback is required (new WP, see §7). |
| L10 | **Headless** — no rendering in this project. | No render-thread sync problem; the runtime stays a pure server-side sim brain. A clean read-back seam still needed for telemetry/IO. |
| L11 | **Streaming is mandatory** — the world does not fit in memory; regions load/unload dynamically with LOD and sleeping islands. | The persistent compiled graph must **evolve** (add/remove region sub-graphs at sync points). This is the same machinery as region ownership (L6) — design once. |
| L12 | **Claude builds the domain layers** (ECS + physics) as **separate, zero-coupling libraries** (no runtime header in them); the fluent API **may be freely extended**. | The plan must reach implementation-level API contracts; new fluent overloads (late binding, regions, device state) are permitted. |
| L13 | **Native CPU execution path is wanted, but fully abstracted** behind the existing SYCL + scheduler model — invisible to the user side, single completion contract, low maintenance. | Add a native-thread path for pure-CPU HOST tasks behind `Task::ExecutionMode`, reusing the existing "empty-event = ready inline" seam (`event_polling.hpp:63`). One branch in `execute_task`, **not** a second scheduler. |
| L14 | **The engine is a separate repository, `SushiEngine` (not yet created).** Strict role split: **SushiEngine = the engine** (entities, physics, scenes — the *what*); **SushiRuntime = computes and schedules** (the *when / where / how*). | The ECS and physics libraries live in the **SushiEngine** repo and depend only on SushiRuntime's **public API** (the fluent layer + any new public seams). SushiRuntime gains **no** engine concept. This document plans the SushiRuntime-side seams (WP-0…WP-2, WP-4…WP-7) and the contracts SushiEngine builds against (WP-3 is the SushiEngine side). Keep the two repos' roles distinct at all times. |
| L15 | **Pure C++ for v1** — no scripting/authoring frontend. **Python frontend is explicitly ABANDONED** (decided 2026-06-25): the audience is **industry, not researchers**, so a binding layer would only cost time. The C++ **Fluent API is the one and only frontend**, and it gets first-class developer experience: proper docs + a **Unity3D-scripting-API-style documentation website** (a browsable API-reference site, not just README prose). | No Python/binding work in scope; worlds and systems are defined in C++. The fluent API surface is the product's public face, so it ships with a real docs site (see **WP-DOCS**, §7). The architecture should not preclude a later scripting layer, but none is built now. |
| L16 | **First end-to-end milestone = a tiny ECS vertical slice.** | A few components + 2–3 systems proving parallel system scheduling and deferred spawn/destroy — the "graph behaves like a game engine" thesis. It may use dense arrays at tiny scale (region identity not yet required), then graduate to chunks. See §10. |
| L17 | **WP-0 order: the two deadlock BLOCKERs first** (§3.1 event-cap, §3.4 deque), then error isolation, cancellation, magazine. | Sequences the first weeks; deadlocks are the items that make any larger graph unsafe, so they lead. |

**Two cross-cutting principles the owner stated explicitly, applied throughout:**

- **Total abstraction.** Every capability added (native CPU path, device-resident
  state, distribution, region ownership) must be invisible to the user side — the
  same `add()` / handle API, the runtime decides the mechanism. Portability and
  "don't make the user think" are why SYCL was chosen and must not regress.
- **Maintainability over cleverness.** Prefer one seam with one contract over a
  parallel mechanism. A second execution path must share the existing
  event/successor/ref-counting contract, not duplicate it.

**The single most important refinement these decisions produce** is in WP-4 (§7):
because determinism is same-architecture and the DAG already guarantees
dependency-correct results regardless of schedule, **determinism reduces to making
every order-sensitive reduction use a fixed combination order.** Work stealing and
the scheduler's nondeterministic dispatch *order* do not change results as long as
no accumulation depends on that order. So WP-4 is primarily "deterministic reduction
primitives + an audit that nothing order-sensitive leaks schedule order," and only
secondarily a separate engine. This is far cheaper than a single-threaded
deterministic executor and preserves throughput.

---

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

---

## 2. What it does well (keep these)

These are genuine strengths and the reason the goal is realistic at all:

- **Automatic data-driven ordering is the hard part of an ECS scheduler, and it
  exists.** `DependencyTracker::resolve` (`modules/engine/graph/source/dependency_tracker.cpp:66`)
  derives RAW/WAR/WAW edges from declared read/write sets. An ECS "system schedule"
  — run systems in parallel unless their component access conflicts — is exactly
  this mechanism with components as the tracked resources.
- **`State<T>` is a correct, ready primitive for stepped fields.** Read-current /
  write-next with an O(1) swap is the Jacobi update every stencil/PDE/cellular
  step wants, and it is already wired through the fluent layer including the
  multi-field coupled form (`runtime.hpp:597-609`).
- **Heterogeneous CPU+GPU in one graph**, with data-ownership-correct dispatch:
  the scheduler routes a node to the queue whose context owns its data
  (`scheduler.cpp:551-558`, `RuntimeContext::get_queue_for_ptr`).
- **NUMA-aware work stealing** with a precomputed distance-ordered victim table
  (`scheduler.cpp:210-257`, `find_task_to_steal` at `:609`) — most engines never
  get this.
- **Persistent worker pool with cheap re-arm** (`arm_for_execution`,
  `scheduler.cpp:277`) — no thread churn between steps.
- **Lock-free hot paths** for scheduling, dependency edges, memory pool, and EBR.
- **The execution strategy is already an interface** (`IExecutionEngine`,
  `execution_engine.hpp:121`). A deterministic/replay engine can be added without
  touching `TaskGraph` or `RuntimeContext`. This is the seam determinism will use.
- **An opt-in, zero-overhead-when-off distributed seam** already threads through
  `compile()` stamping and a single scheduler branch (`scheduler.cpp:483-489`).

---

## 3. Findings: correctness and robustness (BLOCKER class)

These will manifest as a dead or wrong runtime at the scale the goal implies. They
are independent of which target shape is chosen and must be fixed first.

### 3.1 BLOCKER — `EventPolling` silently drops completion events past 256 in-flight

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

Important nuance: `ActiveEventList` is **per-worker** — it is a local in
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

### 3.2 BLOCKER — a single task exception permanently kills the runtime

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
`shutdown_requested`** — `arm_for_execution` only sets `running` (`scheduler.cpp:277`).
So after *one* kernel/host exception anywhere, the worker pool is gone and the
`RuntimeContext` is unusable for the rest of the process. The error surfaces to the
caller via the sentinel promise (`execution_engine.cpp:193-203, 250-254`), but the
engine cannot be restarted.

For a one-shot HPC job this is acceptable. For a long-running engine — where frame
1,000,000 must survive a transient bad kernel, a NaN guard, or a recoverable device
error — it is fatal: one bad frame ends the simulation.

**Fix:** separate "this run failed" from "the pool is dead." A task exception should
fail the *current* `execute()` (it already does) without latching
`shutdown_requested`; the pool should drain the current round and remain re-armable.
This pairs with cancellation (§3.3). Acceptance: a graph whose kernel throws on step
K still allows a fresh graph to run on the same `Runtime` afterward.

### 3.3 BLOCKER (for real-time) — no cancellation / frame-abort mechanism

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

### 3.4 BLOCKER (latent) — Chase-Lev deque overflow drops tasks

`Deque::push` discards the task with an error log when `b - t >= CAPACITY`
(`scheduler.cpp:102-106`); `CAPACITY = 262144` per worker (`scheduler.cpp:64`).
`drain_pending_submissions` guards the injector→deque path
(`scheduler.cpp:363-364`), but `split()` re-pushes (`scheduler.cpp:471-472`) and
`trigger_successors` pushes directly (`event_polling.hpp:246, 260`) with no guard.
262K per worker is large, but a massive world with deep fan-out is exactly the
workload that can reach it, and the failure mode is a silently dropped task →
unsatisfied dependency → deadlock, same shape as §3.1.

**Fix:** the deque must not drop. Either spill on overflow or assert-and-grow.
Acceptance: a graph that intentionally drives one worker's ready set past 262K
completes.

### 3.5 STRUCTURAL — `ThreadLocalMagazine` teardown depends on thread-exit ordering

`ThreadLocalMagazine` (`memory_pool.hpp:95`) is `thread_local`; on thread exit it
flushes live USM slots back to the pool. The teardown order in
`RuntimeContext::Impl::~Impl` (`runtime_context.cpp:114-140`) shuts the engine down
first (joining all worker threads while the allocator still lives), so **worker
threads are safe by ordering**. The hazard is **application-owned threads** that
allocate via the context and exit *after* `RuntimeContext` is destroyed — their
magazine destructor then touches a freed pool (use-after-free). An engine will have
such threads (a render thread, an asset thread, a job-system thread).

**Fix:** gate the magazine destructor on a liveness flag published by the pool/
allocator, so a flush after pool destruction becomes a no-op. Acceptance: an
application thread that allocates, then outlives `RuntimeContext`, exits cleanly
under ASan.

### 3.6 PERF/STRUCTURAL — multi-allocator "flush cliff" hurts multi-GPU

The magazine caches one allocator id; allocating from a different `USMAllocator`
flushes the **entire** magazine first (`memory_pool.hpp:82-94`). A multi-GPU world
that interleaves allocations across device contexts pays a full flush per switch.
Relevant the moment the "large world" spans more than one device.

**Fix:** per-allocator magazines, or a small per-allocator bucket set, so switching
device context does not nuke the cache. Acceptance: an alloc/free microbench
alternating two allocators shows no flush spike.

---

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
  (`buffer.hpp:49`, re-read each submit), but **only the pointers move — not `n`**.

So a `State` whose element count changes between steps (entities spawned/destroyed,
a changing number of active cells, a varying collision-pair count) requires a
recompile.

### 4.2 Recompile cost is real and scales with the world

`compile()` is not free at scale:

- Every read/write declaration does a heap `new AccessNode`
  (`dependency_tracker.cpp:73`) and a backward traversal of the per-shard history
  (`dependency_tracker.cpp:86-120`).
- For a write to a heavily-shared resource, the traversal walks **all prior reads**
  back to the previous write (`dependency_tracker.cpp:91-101`) — O(readers). A
  component read by R systems then written by one costs O(R); across a frame of such
  components this trends O(systems²) on the hot components.
- `compile()` then drains and `reset()`s the tracker, retiring every `AccessNode`
  through EBR (`dependency_tracker.cpp:40-54`).

Doing this **every frame** for a 10⁴–10⁶ entity/cell world is the dominant cost and
defeats the "compile once, replay" advantage.

### 4.3 The mechanism to fix this already exists in embryonic form

The distributed path solved the "pointers rotate every step but I don't want to
recompile" problem with `live_reads_` / `live_writes_` — `shared_ptr<vector<void*>>`
on the node, re-pointed before each step, resolved at dispatch
(`node.hpp:189-201`, `add_offload_dynamic` at `task_graph.cpp:425`,
`dist_io_updaters_` at `runtime.hpp:1156`). This is the seed of the general
solution: a node whose *bindings* (pointers, and crucially **size**, and an
**enabled** flag) are late-bound and updated per step, on a graph whose *topology*
stays compiled.

### 4.4 Resource identity is a raw base pointer — wrong granularity for chunks/halos

Dependencies are keyed on the base pointer (`buffer.hpp:164`), hashed into 1024
shards (`dependency_tracker.hpp:79-103`). Two consequences the target hits hard
(both independently verifiable from the code cited above):

- **No sub-region identity.** A grid's interior and its halo border share one base
  pointer, so they are seen as the same resource (false serialization). Conversely,
  if you give archetype chunks or sub-grids *distinct* base pointers, two tasks
  touching overlapping regions through different bases are **not ordered at all** —
  a silent data race. ECS chunked storage and grid domain decomposition both live
  squarely in this trap.
- **Shard collisions create spurious edges.** Distinct pointers can hash to the same
  shard (`dependency_tracker.hpp:96-103`); a collision never drops a real dependency
  but can serialize two unrelated systems, costing parallelism. At 10⁵ component
  arrays / chunks, collisions in 1024 shards are frequent.

This is the single highest-leverage *correctness* redesign for ECS-at-scale and the
prerequisite for halo-based domain decomposition (§9).

---

## 5. Findings: performance and scaling (PERF)

Correct today, but the constant factors are wrong for a real-time frame or a huge
world.

### 5.1 Per-step `std::promise`/`std::future` + `shared_ptr` churn

Every `execute()` — i.e. **every step** of `run(n)` — allocates two
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
(`event_polling.hpp:229-234`) — two different strategies for the same need. At
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
two different device contexts cannot be expressed correctly — it will run on one
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
structured frame report. This is also what makes §5.1/§5.3 tunable rather than
guessed.

---

## 6. Target-by-target gap summary

| Capability the target needs | Runtime today | Verdict |
|---|---|---|
| **Field/grid stepped update** (stencil, PDE, CA) | `State` + `Extent`/halo + `run_until`, compile-once-replay | **~80% — usable now**, after §3 hardening. Halo identity (§4.4) needed for correctness when chunking. |
| **ECS system scheduling** (parallel systems by component access) | `DependencyTracker` is exactly this engine | **~65%** — the scheduler exists; missing the ECS layer, dynamic sizing (§4.1), and handle identity (§4.4). |
| **ECS with fast spawn/destroy** | recompile per frame + tracker heap churn | **~40%** — needs persistent graph + late-bound size/enable (§4.1/§4.3) and a deferred command buffer. |
| **Deterministic stepping** (replay/multiplayer) | scheduler is nondeterministic by design (work stealing + adaptive polling); rebalancer always on | **~20%** — needs a replay engine on the existing `IExecutionEngine` seam + deterministic reductions. |
| **Single very large multi-machine world** | task-offload distributed layer (ship individual tasks) | **~15%** — wrong model: offload ≠ spatial domain decomposition with halo exchange (§9). |

---

## 7. The engineering program

Ordered. Each work package lists the problem, the design, the files it touches, the
risks, and a concrete acceptance test. Later packages depend on earlier ones; §10
gives the dependency graph.

### WP-0 — Core hardening (BLOCKERs from §3). Prerequisite for everything.

**Scope:** §3.1 event-list overflow, §3.2 error isolation, §3.3 cancellation,
§3.4 deque overflow, §3.5 magazine teardown. Optionally §3.6.

**Design:**
- `ActiveEventList`: keep the inline 256 fast path, spill to a heap vector beyond it
  (same pattern as `SmallEventVector`, `node.hpp:56`). `push` never fails.
- Cancellation + error isolation share one run-state object owned by the engine
  per run: `{ atomic<bool> cancelled; atomic<bool> failed; exception_ptr err; }`.
  Workers check `cancelled` in the dispatch loop; a task exception sets `failed`/
  `err` for *this run* and stops new dispatch **without** latching the pool-level
  `shutdown_requested`. `arm_for_execution` resets the run-state.
- Deque: spill-on-overflow or grow; remove the silent-drop branch.
- Magazine: liveness flag checked in the destructor.

**Files:** `event_polling.hpp`, `scheduler.cpp`, `task_scheduler.hpp`,
`execution_engine.cpp`, `memory_pool.hpp`, `usm_allocator.*`.

**Risks:** the spill paths must stay lock-free/contention-light on the hot path; the
run-state must be cache-friendly (one cache line) to avoid false sharing across
workers.

**Acceptance:** (a) 1-worker, 10⁵-leaf fan-out completes; (b) a throwing kernel on
step K leaves the `Runtime` usable for a new graph; (c) a second thread cancels a
`run_until` and `execute()` returns within a task quantum; (d) ASan clean for an
app thread that outlives `RuntimeContext`.

### WP-1 — Persistent graph with late-bound bindings (the §4.1 fix)

**Problem:** topology and per-node size are frozen at compile; the target changes
both per frame.

**Design — split "shape" from "binding."** Generalise the distributed
`live_reads_/live_writes_` idea (`node.hpp:189-201`) into a first-class concept for
*every* node, not just offloaded ones:

- A compiled node gains an optional **binding block**: late-bound
  `{ reads*, writes*, element_count, enabled }`, held behind a stable indirection
  (a `shared_ptr` or an index into a per-plan binding table) and **re-read at
  submit time**, exactly as `StateView` re-reads `cur/next` today (`buffer.hpp:49`).
- The fluent layer exposes this as e.g. `State::resize()`-aware steps and a
  `g.add(..., when([]{ return active; }), ...)` predicate that toggles `enabled`.
- A node with `enabled == false` at dispatch is treated as already-complete:
  it releases successors immediately and submits no kernel. This gives **conditional
  systems** (LOD, disabled subsystems, sleeping islands) with zero recompile.
- `element_count` late-bound means a `parallel_for` range can shrink/grow per step
  within a **pre-sized capacity** (allocate for max N; iterate over live N). This is
  the ECS-friendly path: keep the system DAG compiled, vary the row count per frame.

**What stays compiled:** the set of systems and their data-flow edges. That changes
rarely (only when the *kinds* of systems/components change), so recompiles become
infrequent instead of per-frame.

**Files:** `node.hpp` (promote `live_*` + add `enabled`, `live_count`),
`task_graph.cpp` (`add_node`/`add_task` accept binding blocks; `compile` preserves
them), `event_polling.hpp`/`scheduler.cpp` (honor `enabled` as instant-complete),
`runtime.hpp` (fluent `when()`/late-size overloads; extend `dist_io_updaters_`
into a general per-step `bindings_updaters_`).

**Risks:** dependency edges are computed at compile from a snapshot of the bindings;
if a *write set* (not just pointer identity) can change per frame, the compiled
edges can become wrong. Mitigation: bindings may change pointers/size/enabled but
**not** which resources a node touches; structural change to the access set requires
recompile (and is rare by construction). This must be enforced and documented.

**Acceptance:** a stencil whose active-cell count varies each step, and an ECS-style
graph that enables/disables a system per step, both run for N steps with **zero**
recompiles (assert compile count == 1).

### WP-2 — Resource identity redesign (the §4.4 fix)

**Problem:** base-pointer identity cannot express chunks, sub-grids, or halos
correctly; it both over-serializes (interior vs border) and under-serializes
(distinct bases over overlapping memory).

**Design — region-based resource keys.** Replace the bare `void*` key with a
`ResourceRegion { base, offset, length }` (or `{ resource_id, interval }`):

- The dependency tracker orders two accesses iff their regions **overlap**, not iff
  their base pointers are equal. This makes interior-vs-halo correctly *parallel*
  when they don't overlap and correctly *ordered* when they do.
- Identity becomes a registered `resource_id` (stable, not a raw address), decoupling
  dependency identity from allocation address — which is also what the distributed
  layer already wants (`GlobalBufferID`, `protocol.hpp:71`). Unifying these is a
  deliberate goal: one identity model for local ordering and remote resolution.
- Shard the tracker by `resource_id`, not hashed pointer, removing the spurious-edge
  collision class (§4.4) and the 1024-shard ceiling for free.

**Interval overlap** can be kept cheap: per resource, maintain the access history as
today but compare intervals instead of pointer equality; for the common
whole-buffer case the interval is `[0, size)` and the check degenerates to the
current behavior.

**Files:** `dependency_tracker.{hpp,cpp}` (region keys, overlap test, id-sharding),
`buffer.hpp` (handles expose a `resource_id` + sub-region views, e.g.
`buf.region(offset, len)`), `task_graph.cpp` (read/write sets carry regions),
`node.hpp` (`io_reads_/io_writes_` become regions; queue anchoring uses the region's
base).

**Risks:** this touches the correctness core. It must be landed behind exhaustive
tests (overlap/no-overlap, adjacent, nested, whole-buffer regression) and benchmarked
to confirm the whole-buffer path doesn't regress. Biggest single risk in the plan;
also highest leverage.

**Acceptance:** two kernels writing disjoint halves of one buffer run in parallel;
two kernels writing overlapping ranges through different base offsets are ordered;
the existing functional suite passes unchanged.

### WP-3 — The ECS / scene layer (a new domain, above the runtime)

**Problem:** there is no entity/component/system model. This is where "the graph
behaves like a game engine" actually gets built — and it must be built *on* the
runtime, not *in* it (per the project's downward-only layering principle).

**Storage recommendation (answering the owner's "you decide", L-context).** Given
10⁶–10⁹ entities (L2), central collision producing per-frame neighbour sets (L7),
and mandatory streaming/LOD (L11), the recommendation is **archetype chunks**, not
dense per-component arrays: fixed-size chunks (e.g. 16 K entities) are the natural
unit for (a) cache-friendly SoA iteration, (b) **streaming** — load/unload a region
as a set of chunks, (c) **domain decomposition** — a chunk is the migration unit
(L6), and (d) **PGS graph colouring** (L8) — colour at chunk granularity. The cost
is that chunks make WP-2 region identity **mandatory** (chunks are sub-regions of a
component's backing store), which is already on the critical path. Dense arrays would
be simpler but cannot express streaming or migration cleanly, so they are rejected
for this target.

**Design — a thin ECS that compiles to the task graph:**

- **Component storage = archetype chunks over `Buffer<T>` (SoA).** Each component in
  an archetype is a contiguous per-chunk array; chunks are the runtime's tracked
  resources via WP-2 region keys, so systems touching disjoint chunks run in
  parallel automatically.
- **System = a task** that declares `Reads{...}/Writes{...}` over component arrays.
  The runtime's dependency tracker *is* the system scheduler: systems with disjoint
  access run in parallel automatically; conflicting ones are ordered. No bespoke
  scheduler is written.
- **Entity = an index** into the SoA arrays. Spawn = append within pre-sized
  capacity (WP-1 late size); destroy = tombstone + compaction at a sync point.
- **Structural changes are deferred.** Spawns/destroys/add-remove-component go into
  a **command buffer** applied at an explicit sync barrier between phases — modeled
  naturally on `State`'s read-current/write-next discipline (`buffer.hpp:281`) so a
  system never observes a structural change mid-frame. This is the ECS-correct
  pattern (Bevy/DOTS) and `State` already provides the double-buffer substrate.
- **The frame = a compiled graph replayed**, with WP-1 bindings carrying per-frame
  entity counts and enable flags so the graph is compiled when the *system set*
  changes (rare), not when entities change (every frame).
- **Gauss-Seidel / PGS solvers (L8) via graph colouring.** A sequential solver
  cannot run all constraints at once without races. The physics layer partitions
  constraints into **colours** (no two constraints in a colour share a body), and
  emits one task per colour; the dependency tracker orders colour *k+1* after colour
  *k* (they share body buffers) while parallelising **within** a colour. The runtime
  needs no solver knowledge — colouring is a domain concern that expresses itself as
  ordinary read/write sets. Collision broadphase (L7) rebuilds the constraint/colour
  sets per frame, which is exactly the WP-1 dynamic-topology case.

**Repo (L14):** WP-3 lives in the **SushiEngine** repository, not SushiRuntime. It
depends only on `<sycl/sycl.hpp>` + SushiRuntime's public fluent API. No
runtime-core changes beyond the public seams WP-1/WP-2/WP-6 expose. If WP-3 finds it
*needs* a runtime change, that change is a new public seam in SushiRuntime (a
SushiRuntime task), keeping the role split (L14) intact.

**Risks:** archetype/chunk layout interacts with WP-2 region identity — design them
together. Command-buffer application is a serialization point; keep it O(changes),
not O(entities).

**Acceptance:** a worked example — N entities with `Position/Velocity/Mass`
components, `Integrate` and `ApplyForces` systems running in parallel where
non-conflicting, spawning/destroying entities per frame with no per-frame recompile,
producing correct results vs a reference single-threaded loop.

### WP-4 — Determinism via fixed reduction order (refined per L3)

**Problem:** the default scheduler is nondeterministic — work stealing, adaptive
polling, and the always-on rebalancer (§5.3) vary the dispatch order run-to-run.
The owner needs **server-authoritative replay/debug determinism on the same
architecture** (L3): re-running recorded inputs reproduces the same state bit-for-bit.

**Key insight (this is the cheap path).** The compiled DAG already guarantees a
**dependency-correct** result regardless of dispatch order (§1). The *only* source
of run-to-run variation in the **result** is **order-sensitive floating-point
accumulation** (a parallel reduction whose combination order follows the schedule).
Therefore, on a single architecture, determinism does **not** require a
single-threaded engine or disabling work stealing. It requires:

1. **Fixed-order reduction primitives.** Every sum/min/max/dot/accumulation that
   feeds simulation state uses a deterministic combination order (a fixed-layout
   tree reduction), independent of how many workers ran it or in what order.
2. **No schedule-order leakage.** An audit/lint that no system writes state in a
   way that depends on the order sibling systems ran (e.g. "first writer wins"
   races, non-commutative in-place accumulation). The region-overlap tracker (WP-2)
   makes genuine write-write conflicts explicit, which helps this audit.
3. **Deterministic RNG.** Per-entity / per-cell streams seeded by stable id, not by
   a global counter touched in schedule order.

With those, **work stealing and the rebalancer can stay on** in the deterministic
mode, because they no longer affect the result — only the timing. A separate
fixed-order `IExecutionEngine` (the seam at `execution_engine.hpp:121`) becomes a
**fallback/validation tool** (run the same frame single-threaded and diff) rather
than the production path. This preserves throughput (L1) while meeting L3.

**Caveat to honor:** GPU reductions are the hard case — a device `reduce` is not
guaranteed fixed-order. Deterministic state-affecting reductions on GPU must use an
explicit fixed-order kernel (segmented tree reduce over a fixed layout), accepting a
modest performance cost on exactly those reductions. Non-state-affecting reductions
(telemetry, diagnostics) need not be deterministic.

**Files:** a deterministic reduction header in the fluent API; rebalancer made
opt-in (§5.3) so a *strict* mode can disable even timing nondeterminism if ever
needed; optional `execution/deterministic_engine.{hpp,cpp}` as the validation
fallback. Cross-architecture determinism is **explicitly out of scope** (L3), so no
fixed-point math is introduced.

**Acceptance:** a physics+ECS frame, run twice under the normal work-stealing
engine, produces bit-identical state; a recorded input stream replays to the same
state; the single-threaded validation engine agrees with the parallel one.

### WP-5 — Peer-to-peer domain-decomposition (the §9 massive-world fix)

Large enough that §9 treats it on its own. WP-5 builds the new distributed model:
**peer-to-peer** region ownership with halo exchange (hundreds of machines, L5),
designed so **entity migration is a clean later addition** (L6) and so **region
load/unload (streaming, L11)** is the *same* lifecycle machinery. It depends on
WP-1/WP-2/WP-3 and the WP-4 reduction primitives.

### WP-6 — Device-resident persistent state (per L9)

**Problem:** `Buffer<T>`/`State<T>` allocate **shared** USM (`buffer.hpp:95-103`,
via `malloc_shared_on`). At 10⁶–10⁹ elements kept resident across frames (L2/L9),
shared USM risks host/device migration thrash, and the world must stay on the device
between frames with the host reading only selected sub-ranges on demand.

**Design:**
- A **device-USM** storage option for `State`/`Buffer` (the runtime already exposes
  `malloc_device`, `runtime_context.hpp:160`), selected at allocation behind the same
  handle API (L-principle: invisible to the user — `rt.state<T>(n)` unchanged; a
  policy/tag picks residency).
- **Selective readback**: a typed `read_range(offset, len)` that copies just the
  requested sub-region to a host staging buffer (uses WP-2 regions), for telemetry/IO
  (headless, L10) without host-mapping the whole field.
- Co-location stays correct via the existing owner map (`runtime_context.cpp:84`,
  `get_queue_for_ptr`); device residency does not change the dependency model.

**Files:** `buffer.hpp` (residency tag + `read_range`), `runtime_context.*`
(device-allocation path already present), interaction with WP-2 region views.

**Risks:** device USM is not host-addressable, so any host-side `operator[]`
(`buffer.hpp:154,275`) must be gated/removed for device-resident handles — a
compile-time or debug-time guard, since silently returning a device pointer to host
code is a fault. Multi-allocator magazine cliff (§3.6) is hit here under multi-GPU
(L4) and should be fixed alongside.

**Acceptance:** a 10⁸-element field stays device-resident across 10⁴ steps with no
host migration (verified by profiler), while `read_range` pulls a window correctly.

### WP-7 — Native CPU execution path (per L13), fully abstracted

**Problem:** pure-CPU work (gameplay logic, AI, navigation, the CPU half of
broadphase) currently routes through `q.submit(host_task)` (`scheduler.cpp:561-563`),
adding SYCL submission overhead and requiring a live queue for plain C++.

**Design — one branch, not a second scheduler (L13 + maintainability principle):**
- A HOST task that does **not** submit device work runs directly on the worker
  thread instead of through `host_task`. The decision is internal to `execute_task`.
- Completion reuses the **existing** "empty event = ready inline" seam
  (`event_polling.hpp:63`, `scheduler.cpp:579`): the native task returns an empty
  event, the poller flags it ready, and successors fire through the *same*
  `trigger_successors` + ref-counting path. No new completion contract.
- **Portability preserved:** where a host task submits device work, or on a build
  without the native path, it transparently falls back to the SYCL host_task route.
- **User side is unchanged:** `g.add(...)` with a plain C++ lambda is identical;
  the runtime picks native vs SYCL.

**Files:** `scheduler.cpp` (`execute_task` branch on `ExecutionMode::HOST` +
"pure-CPU" flag on the task), `task.hpp`/`node.hpp` (a `submits_device_work` hint set
by the fluent layer or detected conservatively), no API change.

**Risks:** misclassifying a host task that *does* submit device work as "pure CPU"
would run device submission off the SYCL path — the flag must default to the safe
(SYCL) route and only opt into native when the caller asserts purity.

**Acceptance:** a CPU-only system runs without any SYCL submission (verified), its
successors release correctly, and a host task that submits a kernel still works via
the fallback.

---

### WP-DOCS — Fluent API documentation site (per L15, Unity-style)

**Problem:** the C++ Fluent API is now the *only* frontend (Python abandoned, L15) and
the product's public face for an **industry** audience. A README is not enough; the
owner wants a Unity3D-scripting-API-style experience: a browsable, searchable
**API-reference website** with per-type / per-method pages, signatures, parameters,
return values, and runnable usage snippets.

**Design:**
- **Source of truth = the Doxygen comments already mandated** by `CONTRIBUTING.md`
  (every public function documents *why* + *how* + every `@param`/`@return`). The site
  is generated from those, so docs cannot drift from the headers.
- **Pipeline:** Doxygen (XML) → a static-site generator (e.g. Doxygen+doxysphinx,
  or Doxygen XML → mkdocs-material / Sphinx-breathe) producing a Unity-manual-style
  layout: a hand-written **manual / guides** section (getting started, the
  build→compile→execute model, State/Buffer, run/run_for/run_until, regions once WP-2
  lands) alongside the **auto-generated scripting reference**.
- **Surface scoped to the public fluent layer** (`api/`): `Runtime`, `API::Graph`,
  `Buffer`/`State`, `RunReport`, `Error`/`Errc`. Lower layers stay in `ARCHITECTURE.md`.
- **Kept honest by CI:** the docs build runs in CI; a missing/!-documented public
  symbol fails the build, so the site stays complete as the API grows (WP-1 late-binding
  overloads, WP-6 residency tags, etc. each add their pages when they land).

**Files:** a `docs/` site config (generator of choice), a Doxyfile scoped to
`modules/surface/api/include/SushiRuntime/api/`, CI job to build+publish (e.g. GitHub Pages).

**Risks:** low. Main cost is choosing the generator and writing the hand-authored
manual pages. It must track the API, so it is *not* a one-shot: every public-seam WP
(WP-1, WP-2, WP-6, WP-7) adds its doc pages as part of that WP's "done".

**Acceptance:** a published site where a user can browse from `Runtime::create()` to
each `add()` overload with signatures and a working snippet, plus a getting-started
manual; CI fails if a public fluent symbol is undocumented.

---

## 8. Why the current distributed layer is the wrong axis for a large world

The existing layer is a **task-offload / work-pull** system, and a good one for its
purpose, but its model is structurally not domain decomposition:

- **It ships individual tasks**, not regions. `compile()` stamps a node offloadable
  and the scheduler ships it (`task_graph.cpp:180`, `scheduler.cpp:483-489`). A large
  world is not "send these tasks elsewhere"; it is "this machine *owns* this region
  of space forever and only exchanges boundaries."
- **Buffer transfer is whole-buffer.** `BufferPushMsg` carries the entire buffer's
  bytes (`protocol.hpp:316-338`); coherence is whole-buffer versioning — a write
  bumps the version and the worker re-pulls the **whole** buffer
  (whole-buffer version coherence). Halo exchange needs to send a
  *thin boundary* every step, not the whole region.
- **Single authoritative master.** All buffers live on and are served by one master
  (`distributed_coordinator.hpp`); workers are stateless shells. A large world wants
  **peer-to-peer** neighbor exchange, not a star topology through one master — the
  master is an immediate bandwidth and latency bottleneck at scale.
- **Eager full-output writeback.** Results stream the whole output back
  (`TaskResultMsg`, `protocol.hpp:269-309`). Domain decomposition keeps region state
  *resident* on its owner and never ships it wholesale.
- **Homogeneous, native-endian, trusted-LAN, single-master, no reconnect** are all
  limitations of the current transport (`protocol.hpp:121-123`).

The reusable parts: `ITransport` (`i_transport.hpp`) as the wire abstraction, the
`ByteWriter/ByteReader` framing (`protocol.hpp:130-217`), and the
version-by-id coherence *idea* (re-applied at sub-region granularity). The
coordinator/worker layer itself must be redesigned.

---

## 9. WP-5 in detail — spatial domain decomposition with halo exchange

**Target:** one logical world partitioned across machines; each machine simulates
its sub-domain continuously and exchanges only boundary (ghost/halo) cells with its
spatial neighbors each step.

**Design sketch:**

1. **Partition the world into regions** with a stable owner per region. The region
   identity reuses WP-2's `resource_id`/region model so local ordering and remote
   identity are one concept.
2. **Ghost layers.** Each region carries a halo of cells mirrored from neighboring
   owners. After each step, owners push their updated boundary (a thin slice) to the
   neighbors that ghost it. This is where WP-2 region identity is load-bearing: the
   boundary is a sub-region, not the whole buffer.
3. **Per-step neighbor exchange, not master pull.** Replace the star topology with
   neighbor links: a region sends `O(boundary)` bytes to `O(neighbors)` peers, not
   `O(region)` bytes to a master. Reuse `ITransport`; add a neighbor table.
4. **Overlap exchange with compute** (compute the interior while halos are in
   flight, then update the boundary once they land. The `State` double-buffer maps
   onto this: interior into `next`, boundary completed after exchange.
5. **Determinism (with WP-4):** fixed exchange order + deterministic reductions give
   reproducible distributed steps for multiplayer/replay.
6. **Load balancing = repartitioning**, not task stealing: migrate region ownership
   when a machine is overloaded (rare, expensive, at a sync point), distinct from
   intra-machine work stealing which stays as-is.
7. **Streaming and migration are one mechanism (L6 + L11).** Region *load/unload*
   (streaming a region into a machine, evicting it when far/asleep) and region
   *ownership transfer* (entity/region migrating to a neighbour) are the same
   operation: attach/detach a region's chunk set + its sub-graph at a sync point.
   Build this **ownership-transfer primitive once**; the MVP exercises it only for
   load/unload (no cross-machine entity hand-off yet), but the seam is identical, so
   adding entity migration later is "call the existing primitive with a remote
   destination." This is the SOLID seam the owner asked for (L6).

**Files:** new `distributed/decomposition/` (region table, neighbor links, halo
exchange, the attach/detach ownership primitive) reusing `i_transport.hpp` and
`protocol.hpp` framing; integration with the ECS layer (WP-3) and WP-1 graph
evolution (attaching a region attaches its sub-graph).

**Risks:** the largest body of work in the plan. The MVP is deliberately scoped to a
**fixed grid partition with field/cell data only** (L6) — no cross-machine entity
hand-off — but the **ownership-transfer primitive is designed for migration from day
one** so entity migration is an additive use of it, not a redesign. The hard residual
sub-problem deferred is consistent global identity for a migrating entity (stable id
+ component hand-off); the region/ownership abstraction is what makes it tractable.

**Acceptance (MVP):** (a) a 2-D heat/stencil field partitioned across 2 processes
with a 1-cell halo produces results bit-identical (under WP-4) to the single-process
field; (b) a region can be unloaded on one machine and re-attached on another via the
ownership primitive, with field state preserved — proving the migration seam without
entity migration.

---

## 10. Sequencing and dependencies

Full work-package set after folding in the owner's decisions (§0.1):
WP-0 core hardening · WP-1 persistent graph / late bindings · WP-2 region identity ·
WP-3 ECS + physics layer (archetype chunks, PGS colouring) · WP-4 determinism via
fixed reduction order · WP-5 peer-to-peer domain decomposition (+ streaming/migration
primitive) · WP-6 device-resident state · WP-7 native CPU path · WP-DOCS Unity-style
fluent-API docs site (ongoing — each public-seam WP adds its pages as part of "done").

```
WP-0 (core hardening: event cap, error isolation, cancellation, deque, magazine)
  │
  ├──> WP-2 (region identity)  ── highest correctness leverage; everything regions-aware needs it
  │        │
  ├──> WP-1 (persistent graph / late bindings: size + enabled + topology evolution)
  │        │
  ├──> WP-6 (device-resident state)        ── independent of WP-1/2; needed for L2/L9 scale
  │
  └──> WP-7 (native CPU path)              ── independent; small, high value for L7 broadphase/AI

WP-1 + WP-2 + WP-6 ──> WP-3 (ECS + physics: chunks, command buffer, PGS colouring)
                             │
WP-4 (fixed-order reductions) runs alongside WP-3 (it is mostly reduction
      primitives + an audit, not a separate engine — see refined WP-4)
                             │
WP-1 + WP-2 + WP-3 + WP-4 ──> WP-5 (peer-to-peer decomposition + streaming/migration)
```

**Recommended order: WP-0 → WP-2 → WP-1 → (WP-6, WP-7 in parallel) → WP-3 (with WP-4
folded in) → WP-5.**

Rationale, reweighted by §0.1:
- **WP-0 is non-negotiable** and larger now: real-time (L1) makes cancellation and
  per-step overhead first-class, not nice-to-have.
- **WP-2 is placed first after WP-0** because the archetype-chunk storage (L2/L7/L11)
  makes region identity *mandatory*, and WP-1, WP-3, WP-5, WP-6 all build on it.
  Retrofitting identity later is the most expensive possible mistake.
- **WP-6 and WP-7 are independent and cheap relative to their value** (device
  residency is forced by 10⁹ scale; the native path is small and unblocks CPU-heavy
  broadphase/AI), so they can run in parallel with WP-1.
- **WP-4 is no longer a heavyweight separate engine** (refined WP-4): it folds into
  WP-3 as deterministic reduction primitives + an audit, so determinism is achieved
  without a throughput-killing single-threaded path. It must be designed *with* WP-3,
  not bolted on after.
- **WP-5 is last and largest**, but its ownership/streaming primitive (L6/L11) is
  designed during WP-2/WP-3 so it is an assembly, not a redesign.

**First milestone (owner's choice, L16): a tiny ECS vertical slice.** After **WP-0
(deadlocks first, L17) + a minimal WP-1**, build a small ECS in the **SushiEngine**
repo: a few components and 2–3 systems proving (a) parallel system scheduling derived
from component read/write sets, and (b) deferred spawn/destroy via a command buffer.
At this tiny scale it may use **dense component arrays** (whole-buffer identity is
sufficient, WP-2 not yet required) — the point is to prove the "graph behaves like a
game engine" thesis end-to-end with the least surface. It then graduates to archetype
chunks (WP-2) and device residency (WP-6) as scale grows toward L2.

Note the repo split (L14): WP-0…WP-2 and WP-4…WP-7 are **SushiRuntime** work; the ECS
itself (WP-3) is the **SushiEngine** side, built against SushiRuntime's public API.
The tiny slice therefore also validates that the public seams WP-1 exposes are
sufficient for an external engine to drive — a real test of the role boundary.

---

## 11. Decisions — resolved and still open

**Resolved (§0.1 L1–L13).** Determinism (L3), ECS storage (archetype chunks,
recommended under WP-3), entity-migration scope (field-only MVP with a migration-ready
seam, L6), and host-work (native CPU path behind the abstraction, L13) are all
decided and folded into the plan above.

**Now resolved (batch 6):** first milestone = tiny ECS vertical slice (L16);
greenfield in a new **SushiEngine** repo with the strict role split (L14); pure C++
for v1 (L15); WP-0 lands the two deadlock BLOCKERs first (L17).

**Genuinely still open — surface as they become relevant, not blockers now:**

1. **Cluster scale (L5 was "unspecified").** Designed for hundreds, but the real
   target number will tune the partition hierarchy in WP-5. Revisit before WP-5.
2. **Chunk size and archetype policy.** The 16 K-entity chunk figure in WP-3 is a
   starting point; the real value depends on component sizes and cache targets,
   measured during WP-3.
3. **Cross-architecture determinism is explicitly out (L3)** — re-open only if the
   target ever changes to a heterogeneous lockstep cluster.

These do not block starting WP-0.

---

## 12. Evidence index (so claims can be verified)

- Static topology / per-node size baked at compile: `runtime.hpp:516`, `:1124`,
  `:295`; `task_graph.cpp:471-536`.
- Late-bound binding mechanism (seed of the fix): `node.hpp:189-201`,
  `task_graph.cpp:425-460`, `runtime.hpp:1156`, `buffer.hpp:49`.
- Dependency tracker (heap per access, O(readers) traversal, base-pointer identity,
  1024 shards): `dependency_tracker.cpp:66-121`, `:40-54`;
  `dependency_tracker.hpp:79-103`; `buffer.hpp:164,269-272`.
- Event-list overflow drop: `event_polling.hpp:68-83`, `scheduler.cpp:580-581,416`.
- Deque overflow drop: `scheduler.cpp:64,102-106`.
- Exception kills runtime permanently: `scheduler.cpp:583-593`, `:277,500-501`.
- Per-step promise/future churn: `execution_engine.cpp:174-260`.
- RTTI on hot path / LSP: `scheduler.cpp:535`, `event_polling.hpp:229-234`.
- Always-on rebalancer: `execution_engine.cpp:69-75`, `scheduler.cpp:721-749`.
- Queue anchoring by first pointer: `scheduler.cpp:551-558`.
- Magazine teardown / multi-allocator cliff: `memory_pool.hpp:82-94,95`;
  `runtime_context.cpp:114-140`.
- Distributed = whole-buffer, single-master, task-offload:
  `protocol.hpp:64,121-123,316-338,269-309,71`; `distributed_coordinator.hpp`;
  `task_graph.cpp:180`; `scheduler.cpp:483-489`.
- Reusable distributed seams: `i_transport.hpp`; `protocol.hpp:130-217`.
```
