# What a AAA Physics Engine Needs From SushiRuntime — Engineering Request

**Status:** Open — R1 to R8 are addressed; R9, the multi-colour dispatch node, is open.

The reductions, the prefix scan and the counting sort this document names (`add_reduce`,
`add_segmented_reduce`, `add_prefix_scan`, `add_counting_sort`) leave the runtime for SushiBLAS
in v0.4.0; the runtime keeps the `Recorder` interface they are built on. Read every mention
below as the state before that release.

**The original seven are addressed**, plus R8 (raised and closed after this document's
first pass) — see [`PHYSICS_SUBSTRATE_IMPLEMENTATION.md`](../archive/slop/PHYSICS_SUBSTRATE_IMPLEMENTATION.md) for
the design and [the section below](#delivery) for what each one became. **R9 is open** — raised
2026-08-02, a dispatch-primitive gap the physics side's P8 phase measured directly; see its row
below and `sushiengine/docs/slop/physics_system.md` §16.37/§16.42/§18 for the evidence.

The rest of this document is the original request, kept as written so the reasoning behind each ask
stays auditable. Every claim is anchored to a location in the tree (`file:line`) so it can be
verified independently, in the style of `SIMULATION_ENGINE_SUBSTRATE_PLAN.md`.

## Delivery

| # | Gap | Outcome |
|---|---|---|
| **R1** | Device-driven iteration count | **Built.** `API::sized_from_device(counter, index)`. The count is resolved at *dispatch* rather than at the step boundary, which the request document did not realise was possible: a node's command group is built on a worker only after `EventPolling` waited on every predecessor's event, so a value a predecessor kernel wrote is already visible. That makes both proposed designs unnecessary — no second `run()`, no readback stall, no host node in a device chain, and no launch at worst-case width. The counter is registered as a read of the consuming task, so the producer edge is structural, not a convention. |
| **R2** | Fixed-order deterministic reductions | **Built.** `Graph::add_reduce` and `Graph::add_segmented_reduce`, plus `Sum`/`Minimum`/`Maximum`. A fixed 256-element tile fold repeated over the partials; the combination order is a function of the element layout alone. |
| **R3** | Sub-range ordering at the region boundary | **Built.** The boundary pins carry their byte intervals and every hazard test is an interval overlap, so two regions writing disjoint element ranges of one allocation gain no edge. One region per simulation island is now viable. |
| **R5** | Dependency tracking on every overload | **Built, as Option A.** Every launch shape has a tracked overload (three were missing), and the five dependency-blind ones are spelled `add_untracked`. Inference was never possible for a callable that captures its USM pointers, so "always offer a tracked form, and make the untracked one name itself" is the honest reading of Option A. |
| **R4** | **Asynchronous step** | **Built** — `Graph::run_async()` / `DynamicGraph::run_async()` returning a move-only `RunHandle`. No driver thread was needed: the work was always on the worker pool and `execute()` only added the blocking, so it split into `submit()` + `complete()` and the asynchronous path is those two halves with the caller's own work in between. |
| **R6** | **Late-bound base offset** | **Built** — `API::based_at(provider)` and `API::based_at_device(slot)`, mirroring `sized()` / `sized_from_device()`. Base and count are resolved and validated together at dispatch, since only their combination says whether the node stays inside its buffers. |
| **R7** | **`ThreadLocalMagazine` teardown** | **Built, then superseded.** The fix landed (the lock protecting teardown was itself a namespace-scope mutex destroyed before the `thread_local` destructor that takes it, so it is now allocated once and never freed). As of 2026-08-18 the magazine and the whole pooled allocation path are deleted, so the hazard is gone by construction — see `ENGINE_BACKBONE_REFACTOR.md` §8 item 4. |
| **R8** | **A node label on the ordinary `add()` overloads**, so per-node device timing can be attributed by name instead of arriving as `unnamed_task`. | **Built** (raised 2026-07-30 from the physics side's P8-A; first attempt 2026-08-02, completed 2026-08-18). `Graph::name_last(const char*)` labels the task just added and reaches `Node::metadata` when the plan is built. The first attempt was a trailing defaulted argument, which reached only the `Dynamic` overloads and `add_host()` — two launch shapes end in a parameter pack whose last element is the callable, so a trailing name cannot be expressed there, and the most common shape of all, `add(Reads, Writes, n, fn)`, was left unnamed. One spelling for every shape replaced it. |
| **R9** | **A multi-colour (or persistent-kernel) dispatch node** — one `add()` call covering several ordered sub-ranges of the same node shape, so a graph-colouring consumer pays one scheduler round trip (dependency check, event submission, event poll) per *kind* per substep rather than one per *(kind, colour)* pair per substep. | **Open** (raised 2026-08-02 from the physics side's P8, `sushiengine/docs/slop/physics_system.md` §16.37/§16.42/§18). The physics engine's `RuntimeGraphBuilder` measured this round trip, not the kernel it guards, as 96–98% of a representative tick on the CPU backend (§16.37) — colouring exists specifically to let same-colour work run without ordering between members, but `Graph`/`Region`'s `add_parallel` is one node in, one node out, so a colour sweep with a dozen colours pays a dozen full round trips even though nine of those colours have no data dependency on each other's *order*, only on not overlapping the *previous* colour's writes. An engine-side workaround (a kernel that loops over colours internally) was deliberately not built to approximate this — see the physics doc's own reasoning: it would either serialize what colouring exists to parallelize, or duplicate the scheduler's own ordering guarantee inside one node, i.e. a second scheduler hiding inside the adapter. |

**Origin.** SushiEngine is planning a full rigid + soft body physics system
(`sushiengine/docs/slop/physics_system.md`): unified substepping XPBD, joints and articulated
assemblies, tetrahedral finite-element soft bodies with plasticity and fracture, mesh-accurate
collision with signed-distance fields, and a deformable-vehicle path. That plan reads this repo's
public API closely and depends on it heavily. This document is the resulting request: **what the
substrate is missing for that workload, and how badly.**

**Framing.** The substrate plan's locked decisions already name this workload as the target — **L7**
(*"Full physics is in scope… with collision detection as a central feature. Frame-varying graph
topology is guaranteed (collision pairs change every frame)"*), **L8** (Gauss-Seidel / PGS +
graph colouring), **L11** (streaming and sleeping islands). Most of what the physics needs was built:
late binding (WP-1), region identity (WP-2), device residency (WP-6), the native CPU path (WP-7), and
cancellation all shipped. **Four gaps remain**, and one of them is structural.

Labels follow the substrate plan: **BLOCKER** (incorrect results or an unusable shape at the target
scale), **STRUCTURAL** (works, but is built on an assumption the target violates), **PERF**.

---

## Summary

| # | Gap | Class | Effect on the physics |
|---|---|---|---|
| **R1** | No device-driven iteration count (indirect dispatch) | **STRUCTURAL** | The produce-then-consume-a-data-dependent-count chain — which *is* collision detection — cannot be expressed in one composition without either a mid-tick readback stall or dispatching at worst-case capacity. |
| **R2** | No fixed-order deterministic reduction primitives | **BLOCKER for determinism** | WP-4's own item 1, unbuilt. Every state-affecting accumulation must be fixed-order; today each consumer must build that itself, in the wrong layer. |
| **R3** | `DynamicGraph` cross-region ordering keys on whole allocations, not sub-ranges | **STRUCTURAL** | Physics islands are disjoint *slices* of one body column. Mapping one island to one region — the intended use — serializes islands that never touch each other. |
| **R4** | `run()` is synchronous; there is no asynchronous step | **PERF** | A simulation tick cannot overlap with the render or audio extract. |
| **R5** | Raw-kernel `add()` overloads silently skip dependency tracking | **BLOCKER (latent)** | A physics node added through the wrong overload mis-orders silently. Already an open item in `TODO.md`; this is a vote for Option A. |

R6 and R7 at the end are minor.

---

## R1 — Device-driven iteration count (indirect dispatch)

### The evidence

`Dynamic` carries two host-side providers (`api/vocabulary/dynamic.hpp:56-58`):

```cpp
using WhenProvider  = Core::Delegate<bool(), PROVIDER_CAPACITY>;
using CountProvider = Core::Delegate<std::size_t(), PROVIDER_CAPACITY>;
```

They are polled on the **driver thread**, once per step, before the plan replays
(`api/detail/step_machinery.hpp:114-125`):

```cpp
binding_updaters_.push_back([lb = std::move(lb), dyn = std::move(dyn)]()
{
    if (dyn.when_pred) lb->enabled.store(dyn.when_pred(), std::memory_order_relaxed);
    if (dyn.count_fn)  lb->count.store(dyn.count_fn(), std::memory_order_relaxed);
});
```

`LateBindings` itself is explicit about the ordering contract
(`graph/late_bindings.hpp:41-60`): *"the writes happen on the driver thread at a step boundary
(after the previous step's completion barrier) and the reads happen on a worker after the task is
handed off through the scheduler's acquire/release submit path."*

There is no indirect-dispatch facility anywhere in `include/` (a grep for `indirect` returns only
`device_count` false positives).

### Why the physics cares

A physics tick is a chain of producers whose output counts are **only known on the device, mid-tick**:

```
  broadphase  →  N_pairs      (data-dependent, changes every tick)
  narrowphase →  N_manifolds  (data-dependent)
  solver      →  consumes N_manifolds × substeps
```

L7 states this as a given: *"frame-varying graph topology is guaranteed (collision pairs change every
frame)."* Late binding solves the *topology* half of that — the graph compiles once. It does not solve
the *count* half, because the count is produced inside the same run that needs to consume it, and
`sized()` was already latched before that run began.

That leaves three shapes:

1. **Two `run()`s per tick with a readback between them.** Reintroduces exactly the host↔device round
   trip the engine restructured its whole solver to remove.
2. **Dispatch at capacity, early-out per lane** on a device-side counter. One run, no sync, but the
   launch width is the worst case forever: a contact pass sized for a 50 000-contact pile-up dispatches
   50 000 lanes on the ordinary tick that has 800.
3. **Indirect dispatch** — the count is read from a device buffer at dispatch time.

The engine plan takes (2) so it is not blocked on this repo, and its `RuntimeGraphBuilder` seam is
written so switching to (3) is one file.

> **Outcome.** None of the three was needed. This analysis missed *when* a node's command group is
> built: on a worker at dispatch, after `EventPolling::poll_events` has waited on every predecessor's
> event. The count can therefore simply be read there. See the Delivery table above and
> `docs/archive/slop/PHYSICS_SUBSTRATE_IMPLEMENTATION.md` §0; the two designs below were not built.

### Two possible designs

**Design A — a device-resident count slot.** `sized()` gains a sibling that names a device location
rather than a host callable:

```cpp
API::sized_from_device(const Buffer<std::uint32_t>& counter, std::size_t element_index = 0)
```

The scheduler reads that slot when it dispatches the node. This mirrors `vkCmdDispatchIndirect` and is
the shape every GPU physics engine already assumes. Cost: the dispatch path must read one device
value, which on most backends means either a small host-visible allocation or a launch-time read; on
SYCL this may need the counter allocated as `Residency::Shared` even when the rest of the state is
device-resident, which is an acceptable and explicit constraint.

**Design B — sanctioned in-graph binding writes.** A `add_host()` node may write a *later* node's
`LateBindings::count`, **provided a dependency edge orders the two**. The mechanism already exists —
`LateBindings` is a `shared_ptr` with atomic fields — and the missing piece is only the sanctioned API
plus the ordering guarantee. This is cheaper to build than A and stays inside the existing model, but
it costs a host node in the middle of a device chain, so it trades a launch-width saving for a
host-side hop.

**Recommendation:** A if the SYCL backends can carry it, B as the fallback. Either turns the physics
tick into one composition with no wasted width.

### Acceptance

A graph where node 1 writes a count to a device buffer and node 2 iterates exactly that many elements,
in one `run()`, with `compile_count()` staying at 1 across a multi-step run whose count varies every
step.

---

## R2 — Fixed-order deterministic reduction primitives

### The evidence

WP-4 (`SIMULATION_ENGINE_SUBSTRATE_PLAN.md`) states the requirement and its own caveat:

> *"Fixed-order reduction primitives. Every sum/min/max/dot/accumulation that feeds simulation state
> uses a deterministic combination order (a fixed-layout tree reduction), independent of how many
> workers ran it or in what order."*
>
> *"GPU reductions are the hard case — a device `reduce` is not guaranteed fixed-order. Deterministic
> state-affecting reductions on GPU must use an explicit fixed-order kernel (segmented tree reduce
> over a fixed layout)."*

It is unbuilt. There is no reduction header in `modules/surface/api/include/SushiRuntime/api/`, and the tree contains no
`sycl::reduction` and no floating-point `atomic_ref` (the only `atomic_ref` mention is a comment in
`scheduler/work_stealing_deque.hpp:61` about a C++17 replacement).

### Why the physics cares

WP-4's own analysis is that on one architecture, determinism reduces to fixed-order accumulation —
which makes this primitive the *entire* determinism story, not a detail of it. The physics has exactly
two paths that need it:

- per-body contact impulse accumulation in a Jacobi-style contact pass,
- soft-body vertex accumulation across the tetrahedra sharing a vertex.

Both are the classic scatter-accumulate shape, and both feed simulation state that a rollback must
reproduce byte-for-byte.

### Why it belongs here and not in the engine

Per **L14**, the runtime owns *when / where / how*; determinism of a reduction is a property of how
work is combined, not of what is being computed. If every domain layer writes its own fixed-order
reduce, the guarantee is re-derived (and re-broken) once per domain. The engine will build one to
avoid blocking, and would retire it the day this lands.

### Suggested shape

```cpp
// api/reduce.hpp
template <typename T, typename Combine>
Graph& add_reduce(const Reads& input, const Writes& output, std::size_t n, Combine op);

template <typename T, typename Combine>
Graph& add_segmented_reduce(const Reads& values, const Reads& segment_offsets,
                            const Writes& output, std::size_t segment_count, Combine op);
```

The contract that matters is not the signature but the **guarantee**: the combination order is a
function of the element layout alone — not of the worker count, the device, the steal pattern, or the
work-group size.

### Acceptance

The same reduction over the same input produces bit-identical output with the worker count varied from
1 to `MAX_WORKER_THREADS`, on both the CPU and the GPU backend.

---

## R3 — Sub-range ordering at the region boundary

### The evidence

This repo already records the gap, in `TODO.md` (now `docs/archive/slop/TODO.md`) under *Future work*:

> *"Cross-region ordering keys on the whole allocation, not sub-ranges — two regions writing disjoint
> halves of one buffer are serialized though they need not be (the intra tracker already handles
> sub-ranges; lifting it to the boundary means carrying the entry/exit pins' byte intervals)."*

The intra-region tracker does handle it (`docs/architecture/ARCHITECTURE.md`, "Plan": disjoint sub-regions stay
parallel, via `Core::ResourceRegion` and `Buffer::region(ElementRange)` at
`api/data/buffer.hpp:349`). It is only the boundary layer that coarsens.

### Why the physics cares

The intended mapping is **one region per simulation island**, so a sleeping island is `drop()`ped and a
waking one composed — exactly the L11 shape. But islands are *slices of one body column*, not separate
allocations: a scene has one bodies buffer and islands are disjoint index ranges within it. With
whole-allocation boundary keys, every island region is ordered against every other island region
through that shared buffer, and the parallelism the region model exists to provide is lost.

The alternatives are all worse: per-island allocations mean a copy on every island merge and split (and
islands merge and split constantly — that is what contact does); one static graph with `when()`-gated
islands keeps sleeping cheap but not free and gives up the streaming path later.

### Acceptance

Two regions writing disjoint element ranges of one buffer run concurrently, verified by a test that
would deadlock or serialize measurably if they did not; overlapping ranges still serialize correctly.

---

## R4 — Asynchronous step

`Graph::run()` blocks until the sentinel fires (`modules/engine/execution/README.md`, "execute is submit plus complete": it injects the roots into the
scheduler, arms it, and blocks until the sentinel fires). `run_for` / `run_until` are multi-step
drivers, not asynchronous ones, and `run_for` takes a **wall-clock** budget, which a deterministic
simulation cannot use at all.

For a fixed-step simulation thread, blocking is acceptable — the thread has nothing else to do. What it
costs is **overlap**: the physics tick cannot proceed while the previous tick's snapshot is extracted
for the renderer and the audio system, which is a real fraction of a 16 ms frame at scale.

A `try_run_async()` returning a handle that can be waited on, or a completion callback fired from the
sentinel, would let the consumer overlap. **PERF, not a blocker** — recorded so it is on the list, not
because it gates anything.

---

## R5 — Dependency tracking on every overload

`TODO.md` (now `docs/archive/slop/TODO.md`) already carried this as an open API-surface decision:

> *"`API::Graph::add()` calls `add_node()` without read/write pointer lists. Tasks added through the
> fluent API do not participate in automatic RAW/WAR/WAW dependency detection unless a handle-based
> overload is used. The current state is undocumented, which means a user who follows the README's
> 'intended entry point' and expects ordering will get silent mis-ordering on raw kernels."*

**This request is a vote for Option A** (add tracking), or failing that, for making the untracked
overloads *loudly* named (`add_untracked(...)`) rather than merely documented. The reason is asymmetric
consequence: in a physics solve, a silently mis-ordered node does not crash and does not produce an
obviously wrong picture — it produces a simulation that is subtly wrong and non-reproducible, which is
the most expensive class of bug this stack can have, and the one hardest to attribute back to a
scheduling decision made in another repo.

Option B (document it) leaves a correctness guarantee resting on a reviewer noticing an overload.

---

## R6, R7 — Minor

- **R6 (low): a late-bound base offset** beside `sized()`. Graph-coloured constraints are contiguous
  slices of one buffer whose offsets shift as the constraint set changes; `sized()` varies the count
  but the base index is baked into the kernel's capture. Workable today with an index-indirection
  buffer at one extra load per element, so this is convenience, not capability.
- **R7 (low for the engine, already on the runtime's own list): `ThreadLocalMagazine` teardown
  ordering** (`TODO.md` #29). A long-running simulation process is exactly the shape that hits a
  thread-exit-ordering use-after-free, so the engine has an interest in it being closed — but it is
  already correctly tracked as a v1.0.0 correctness blocker here.

---

## What is *not* requested

Worth stating, because it bounds the ask:

- **No new engine concepts in the runtime.** L14 holds. Nothing above asks the runtime to know what a
  body, a contact, or a constraint is. R1 is a count, R2 is a combination order, R3 is a byte interval.
- **No cross-device execution.** The physics targets one device per scene and accepts the co-location
  guard (`modules/engine/scheduler/README.md`). Splitting a simulation across devices is WP-5's problem, not a
  prerequisite.
- **No cross-architecture determinism.** L3 already ruled it out and the engine's plan agrees.
- **No change to the blocking execution model** beyond the optional R4.

## Priority order, if only some of this is built

1. **R2** — it is a stated WP-4 deliverable, it is the whole determinism story, and every day it is
   absent another domain layer writes its own copy.
2. **R1** — the largest single performance factor for collision-driven workloads, and the one with a
   correct-but-wasteful workaround the engine is already committed to shipping.
3. **R5** — cheap, and it converts a class of silent correctness bug into a compile error.
4. **R3** — needed before islands scale, and needed anyway for streaming.
5. **R4**, then R6.
