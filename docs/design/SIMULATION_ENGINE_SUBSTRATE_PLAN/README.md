# SushiRuntime as a simulation and game-engine substrate

**Status:** Open — WP-4 is delivered, WP-5 and WP-7 are not built; the other packages were not re-checked
**About:** SushiRuntime as a simulation and game-engine substrate.
**Modules:** none

The reductions, the prefix scan and the counting sort this page names are SushiBLAS's;
[the design map](../README.md#names-that-left-the-runtime) says how to read them.

## Problem

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
- What is the exact engineering required to close the gap: broken into ordered
  work packages with designs, touch-points, risks, and acceptance criteria?

Every claim in the pages anchors to a location (`file:line`) in the tree as it stood when the
claim was written; the anchors have not been refreshed since. The anchors are indexed in
the evidence.

The findings are in [FINDINGS.md](FINDINGS.md): what the runtime does mechanically, what it does
well, and where it breaks. Three labels grade each:

Throughout, three labels are used:

- **BLOCKER**: will cause incorrect results, deadlock, or a dead runtime at the
  scale the goal implies. Must be fixed before building anything on top.
- **STRUCTURAL**: the current design works but is built on an assumption the
  target violates; needs a real design change, not a patch.
- **PERF**: correctness is fine, but the constant factor or scaling is wrong for
  a real-time frame budget or a massive entity/cell count.

### Target by target

| Capability the target needs | Runtime today | Verdict |
|---|---|---|
| **Field/grid stepped update** (stencil, PDE, CA) | `State` + `Extent`/halo + `run_until`, compile-once-replay | **~80%: usable now**, after findings 3 hardening. Halo identity (findings 4.4) needed for correctness when chunking. |
| **ECS system scheduling** (parallel systems by component access) | `DependencyTracker` is exactly this engine | **~65%**: the scheduler exists; missing the ECS layer, dynamic sizing (findings 4.1), and handle identity (findings 4.4). |
| **ECS with fast spawn/destroy** | recompile per frame + tracker heap churn | **~40%**: needs persistent graph + late-bound size/enable (findings 4.1 and 4.3) and a deferred command buffer. |
| **Deterministic stepping** (replay/multiplayer) | scheduler is nondeterministic by design (work stealing + adaptive polling); rebalancer always on | **~20%**: needs a replay engine on the existing `IExecutionEngine` seam + deterministic reductions. |
| **Single very large multi-machine world** | task-offload distributed layer (ship individual tasks) | **~15%**: wrong model; offload ≠ spatial domain decomposition with halo exchange (the domain decomposition page). |

## Decision

The runtime is, correctly, an **orchestrator**, not a compute library. This is a
foundational design principle of the project: the runtime decides
*when / where / in what order* work runs; domains (physics, AI, BLAS, and an ECS)
decide *what is computed* and live on top as pure compute layers.

That decision is sound and this plan does **not** propose changing it. The plan
keeps engine concepts (entities, transforms, collisions, scenes) **out** of the
runtime core and builds them as a layer above it. What the plan *does* propose is
changing the runtime's execution and dependency model where it is structurally
incompatible with a per-frame, dynamically-shaped, multi-machine world.

### Locked context

The plan below is written against the following decisions, taken explicitly by the
project owner. They are not assumptions; they reshape the priorities in the phases and the sequencing.

| # | Decision | Engineering consequence |
|---|---|---|
| L1 | **Real-time interactive**, variable / scene-dependent frame budget (sometimes ~144 Hz interactive, sometimes a heavy sim step). | Both a low-latency and a high-throughput mode are needed. Cancellation/frame-abort (WP-0) and per-step overhead removal (findings 5.1) are first-class, not optional. |
| L2 | **Scale 10⁶–10⁹** entities/cells per machine. | Region identity (WP-2) and a **device-resident** state path are mandatory, not optional. Per-frame recompile is untenable → WP-1 mandatory. |
| L3 | **Determinism required in v1**, for **server-authoritative** replay/debug, **same binary/architecture only** (no cross-arch, no lockstep sync). | No fixed-point math needed. Determinism reduces to **fixed reduction order** (see refined WP-4); a separate single-threaded engine may not be required. |
| L4 | **Node hardware is a heterogeneous mix** (single-GPU, multi-GPU, CPU-only nodes coexist). | Cross-context queue routing (findings 5.4) and per-allocator magazines (findings 3.6) become load-bearing, not edge cases. |
| L5 | **Cluster size unspecified → design for hundreds of machines.** | The distributed model must be **peer-to-peer** with hierarchical partitioning, not a single star master (see the domain decomposition page). |
| L6 | **Massive-world MVP is field/cell-only**, BUT the seams must be SOLID enough that **entity migration across machines is the easiest possible later addition**. | Region **ownership** must be a first-class abstraction from day one (WP-2/WP-5), even though the MVP never migrates an entity. |
| L7 | **Full physics is in scope:** particle/N-body, grid/field PDE, soft-body/constraint, **and rigid body**, with **collision detection as a central feature**. | Frame-varying graph topology is guaranteed (collision pairs change every frame) → WP-1 critical. Solvers need ordered islands. |
| L8 | **Constraint/rigid solver style is Gauss-Seidel / PGS (sequential).** | Needs **graph coloring** to parallelize within a colour and ordered dependency chains across colours. The dependency tracker provides the ordering; the ECS/physics layer provides the colouring. |
| L9 | **Simulation state is GPU-resident and persistent** across frames; host reads only on demand. | The current shared-USM `Buffer/State` (`buffer.hpp`) is insufficient; a **device-USM state** with selective readback is required (new WP, see the Phases). |
| L10 | **Headless**: no rendering in this project. | No render-thread sync problem; the runtime stays a pure server-side sim brain. A clean read-back seam still needed for telemetry/IO. |
| L11 | **Streaming is mandatory**: the world does not fit in memory; regions load/unload dynamically with LOD and sleeping islands. | The persistent compiled graph must **evolve** (add/remove region sub-graphs at sync points). This is the same machinery as region ownership (L6): design once. |
| L12 | **Claude builds the domain layers** (ECS + physics) as **separate, zero-coupling libraries** (no runtime header in them); the fluent API **may be freely extended**. | The plan must reach implementation-level API contracts; new fluent overloads (late binding, regions, device state) are permitted. |
| L13 | **Native CPU execution path is wanted, but fully abstracted** behind the existing SYCL + scheduler model: invisible to the user side, single completion contract, low maintenance. | Add a native-thread path for pure-CPU HOST tasks behind `Task::ExecutionMode`, reusing the existing "empty-event = ready inline" seam (`event_polling.hpp:63`). One branch in `execute_task`, **not** a second scheduler. |
| L14 | **The engine is a separate repository, `SushiEngine` (not yet created).** Strict role split: **SushiEngine = the engine** (entities, physics, scenes; the *what*); **SushiRuntime = computes and schedules** (the *when / where / how*). | The ECS and physics libraries live in the **SushiEngine** repo and depend only on SushiRuntime's **public API** (the fluent layer + any new public seams). SushiRuntime gains **no** engine concept. This document plans the SushiRuntime-side seams (WP-0…WP-2, WP-4…WP-7) and the contracts SushiEngine builds against (WP-3 is the SushiEngine side). Keep the two repos' roles distinct at all times. |
| L15 | **Pure C++ for v1**: no scripting/authoring frontend. **Python frontend is explicitly ABANDONED** (decided 2026-06-25): the audience is **industry, not researchers**, so a binding layer would only cost time. The C++ **Fluent API is the one and only frontend**, and it gets first-class developer experience: proper docs + a **Unity3D-scripting-API-style documentation website** (a browsable API-reference site, not just README prose). | No Python/binding work in scope; worlds and systems are defined in C++. The fluent API surface is the product's public face, so it ships with a real docs site (see **WP-DOCS**). The architecture should not preclude a later scripting layer, but none is built now. |
| L16 | **First end-to-end milestone = a tiny ECS vertical slice.** | A few components + 2–3 systems proving parallel system scheduling and deferred spawn/destroy: the "graph behaves like a game engine" thesis. It may use dense arrays at tiny scale (region identity not yet required), then graduate to chunks. See Sequencing. |
| L17 | **WP-0 order: the two deadlock BLOCKERs first** (findings 3.1 event-cap, findings 3.4 deque), then error isolation, cancellation, magazine. | Sequences the first weeks; deadlocks are the items that make any larger graph unsafe, so they lead. |

**Two cross-cutting principles the owner stated explicitly, applied throughout:**

- **Total abstraction.** Every capability added (native CPU path, device-resident
  state, distribution, region ownership) must be invisible to the user side: the
  same `add()` / handle API, the runtime decides the mechanism. Portability and
  "don't make the user think" are why SYCL was chosen and must not regress.
- **Maintainability over cleverness.** Prefer one seam with one contract over a
  parallel mechanism. A second execution path must share the existing
  event/successor/ref-counting contract, not duplicate it.

**The single most important refinement these decisions produce** is in WP-4:
because determinism is same-architecture and the DAG already guarantees
dependency-correct results regardless of schedule, **determinism reduces to making
every order-sensitive reduction use a fixed combination order.** Work stealing and
the scheduler's nondeterministic dispatch *order* do not change results as long as
no accumulation depends on that order. So WP-4 is primarily "deterministic reduction
primitives + an audit that nothing order-sensitive leaks schedule order," and only
secondarily a separate engine. This is far cheaper than a single-threaded
deterministic executor and preserves throughput.

## Design

The program is the packages below, one page each. The dependency order and the first milestone
follow.

### Sequencing

Full work-package set after folding in the owner's decisions (the locked context):
WP-0 core hardening · WP-1 persistent graph / late bindings · WP-2 region identity ·
WP-3 ECS + physics layer (archetype chunks, PGS colouring) · WP-4 determinism via
fixed reduction order · WP-5 peer-to-peer domain decomposition (+ streaming/migration
primitive) · WP-6 device-resident state · WP-7 native CPU path · WP-DOCS Unity-style
fluent-API docs site (ongoing; each public-seam WP adds its pages as part of "done").

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

Rationale, reweighted by the locked context:
- **WP-0 is non-negotiable** and larger now: real-time (L1) makes cancellation and
  per-step overhead first-class, not nice-to-have.
- **WP-2 is placed first after WP-0** because the archetype-chunk storage (L2/L7/L11)
  makes region identity *mandatory*, and WP-1, WP-3, WP-5, WP-6 all build on it.
  Retrofitting identity later is the most expensive possible mistake.
- **WP-6 and WP-7 are independent and cheap relative to their value** (device
  residency is forced by 10⁹ scale; the native path is small and unblocks CPU-heavy
  broadphase/AI), so they can run in parallel with WP-1.
- **WP-4 is not a heavyweight separate engine** (refined WP-4): it folds into
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
sufficient, WP-2 not yet required): the point is to prove the "graph behaves like a
game engine" thesis end-to-end with the least surface. It then graduates to archetype
chunks (WP-2) and device residency (WP-6) as scale grows toward L2.

Note the repo split (L14): WP-0…WP-2 and WP-4…WP-7 are **SushiRuntime** work; the ECS
itself (WP-3) is the **SushiEngine** side, built against SushiRuntime's public API.
The tiny slice therefore also validates that the public seams WP-1 exposes are
sufficient for an external engine to drive: a real test of the role boundary.

## Phases

| Id | Phase | State |
| --- | --- | --- |
| WP-0 | [Core hardening](WP_0_CORE_HARDENING.md): the two deadlock blockers first, then error isolation, cancellation and the magazine | building |
| WP-1 | [Persistent graph with late-bound bindings](WP_1_PERSISTENT_GRAPH.md) | built |
| WP-2 | [Resource identity](WP_2_RESOURCE_IDENTITY.md): region keys instead of base pointers | built |
| WP-3 | [The ECS and scene layer](WP_3_ECS_AND_SCENE_LAYER.md), built in SushiEngine | building |
| WP-4 | [Determinism via fixed reduction order](WP_4_DETERMINISM.md) | built |
| WP-5 | [Peer-to-peer domain decomposition](WP_5_DOMAIN_DECOMPOSITION.md) | planned |
| WP-6 | [Device-resident persistent state](WP_6_DEVICE_RESIDENT_STATE.md) | built |
| WP-7 | [Native CPU execution path](WP_7_NATIVE_CPU_PATH.md) | planned |
| WP-DOCS | [Fluent API documentation site](WP_DOCS_API_SITE.md) | building |

RW-14 re-checks every package against the tree. The states of WP-0, WP-1, WP-2, WP-3, WP-6 and
WP-DOCS above are the last recorded reading, not a verified one; the physics requirements
document reports WP-7 as shipped while the backlog lists it as unbuilt.

### Work

<!-- work:begin -->
No open work.
<!-- work:end -->

## Open questions

1. **Cluster scale.** The design assumes hundreds of machines, but the real target number will
   tune the partition hierarchy in WP-5. Revisit before WP-5.
2. **Chunk size and archetype policy.** The 16 K-entity chunk figure in WP-3 is a starting point;
   the real value depends on component sizes and cache targets, measured during WP-3.
3. **Cross-architecture determinism** is out (L3); re-open it only if the target ever becomes a
   heterogeneous lockstep cluster.
4. **WP-7.** Built or not, given the contradiction above.
