# SushiRuntime v1.0.0 — Stable API Freeze

> Decided 2026-06-20. This document records the design decisions that fix the
> v1.0.0 public API, the rationale behind each, and the implementation plan that
> follows. Once v1.0.0 is tagged, the public surface is **frozen**: it grows only
> additively (new overloads, new optional parameters, filling reserved fields),
> never by changing a signature or a documented guarantee.
>
> This supersedes the API-shape decisions in `docs/slop/v1_api_plan.md` and
> consolidates the open items tracked in `docs/slop/ROAD_TO_v1.0.0.md`.

---

## 1. The vision (macro)

SushiRuntime should let a user do HPC by writing **plain C++**, adding it to a
graph in two lines, and letting the runtime handle everything underneath:
lock-free scheduling, NUMA placement, GPU offload, and optional distributed
execution — all invisible. No SYCL or kernel boilerplate. Maximum abstraction
with maximum optimisation, in the spirit of a "batteries-included" HPC
experience.

The production driver is **digital-twin simulation** (a model stepped over time
for hours), but the API is general: the same surface serves one-shot batch DAGs,
iterative simulation/training loops, and streaming/real-time workloads.

A future **JIT** path ("write code, it is added and compiled at runtime") is in
scope for the design but **not implemented in v1**. The v1 API must be shaped so
that adding JIT — and incremental graph update, and real-time pacing — slots in
behind existing seams without changing the public surface.

---

## 2. Locked decisions

| # | Topic | Decision | Rationale |
|---|---|---|---|
| D1 | API style | **Single, human-language fluent API.** One graph object; `run` drives it. No separate "compile then loop" entry point. | One mental model. `run()` and `run(1)` are the same thing; the batch user never meets the word "step". |
| D2 | Lifecycle | **Lazy compile + cache + invalidate-on-mutation.** First `run` compiles and caches the plan; later `run`s replay it; mutating the graph invalidates the plan and the next `run` recompiles. | Unifies one-shot, replay, and rebuild-recompile under one call. Mirrors XLA/JAX `jit`: compile on first call, cache, recompile on change. |
| D3 | Time-stepping | **First-class default.** `run(n)` runs `n` steps; `run_for(duration)` and `run_until(predicate)` bound by wall-clock / condition. | Digital twins step over time; this is the natural verb. Not a dialect — `run()` == `run(1)`. |
| D4 | Evolving state | **`state<T>` vs `buffer<T>`.** `buffer<T>` is ordinary data; `state<T>` is a time-evolving field the runtime double-buffers (`cur`/`next`, O(1) swap each step). | Stencil/PDE steps read-old/write-new and cannot update in place. The runtime owns the ping-pong so the user never sees it. Accepted as a small, deliberate break in abstraction purity for pragmatism. |
| D5 | Dependency tracking | **Handle-based, automatic.** Ops take `state`/`buffer` handles; reads/writes register automatically (RAW/WAR/WAW). An explicit `reads/writes` overload stays as an advanced escape hatch. | The library's headline feature must work through the primary path. Handle access is the canonical "no boilerplate" model (cf. StarPU data handles + access modes). Replaces the explicit-pointer Option A as the *default*. |
| D6 | Dynamic topology (v1) | **Rebuild + recompile only.** Mutating the graph triggers a full recompile on next `run` (D2). Incremental update is deferred. | Simplest correct behaviour. Digital-twin topology changes (AMR, runtime coupling) are infrequent relative to steps. |
| D7 | Stable surface | **All public headers under `include/SushiRuntime/` freeze at v1.0.0.** | Maximum stability guarantee. Makes the hardening work below mandatory pre-freeze (see §5). Freeze = declarations + documented semantics; implementations remain fixable. |
| D8 | JIT / extensibility | **Not in v1; seams left open.** `IKernelSource`, the existing `OpID`/`KernelRegistry` indirection, `IExecutionEngine`, and the injector queue are the seams future work lands behind. | "Future updates must not be painful." Each future capability lands behind an existing seam, not on the public surface. |

---

## 3. The v1 public surface (racon)

```cpp
auto rt    = Runtime::create();
auto field = rt.state<float>(N);          // evolving state; runtime double-buffers
auto data  = rt.buffer<float>(N);         // ordinary data

auto sim = rt.graph();
sim.add(field, stencil_step);             // describe one step; deps auto-tracked

sim.run();                                // compile + run once
sim.run(1'000'000);                       // a million steps (compiled once)
sim.run_for(8h);                          // wall-clock bounded
sim.run_until([&]{ return steady(); });   // condition bounded

sim.add(new_subsystem);                   // mutate -> plan invalidated
sim.run(1000);                            // recompiles once, then replays
```

Same surface, three user shapes:

- **Batch HPC** (one big DAG): `sim.run()` — once, done.
- **Iterative** (simulation / training): `sim.run(n)` / `run_until` — compile once, replay many.
- **Streaming / real-time**: `sim.run_until(...)` fed by the injector queue.

`run` variants (the only run verbs; all return a `RunReport`, with the
non-throwing `try_run*` mirror):

| Verb | Meaning |
|---|---|
| `run()` | one step (== `run(1)`) |
| `run(n)` | `n` steps |
| `run_for(duration)` | steps until wall-clock budget elapses |
| `run_until(predicate)` | steps until the predicate returns true |

---

## 4. Lifecycle mechanics (how `run` works underneath)

1. **First `run` (or first after a mutation):** `TaskGraph::compile()` runs the
   dependency analysis and produces a `CompiledGraph`; the graph caches it.
2. **Each step:** reset every node's atomic incoming-dependency counter to its
   compiled value (the **graph-state reset**, D-locked), `arm_for_execution()`
   the persistent scheduler (exists today), inject roots, block on the sentinel.
   No thread churn, no recompile.
3. **Between steps:** swap every `state<T>` handle's `cur`/`next` backing (O(1)).
4. **On mutation** (`add(...)` after a run): drop the cached `CompiledGraph`; the
   next `run` recompiles (D6).

The scheduler is already built for replay — `TaskScheduler::start()` runs the
worker pool once and keeps it alive across rounds; `arm_for_execution()` re-arms
without recreating threads (`task_scheduler.hpp`). The two genuinely new pieces
are (a) a **re-runnable, cached `CompiledGraph`** that `execute()` no longer
consumes, and (b) the **per-node counter reset** between steps.

---

## 5. Pre-freeze work (mandatory because of D7)

Because every public header freezes, the following must be settled **before** the
v1.0.0 tag — afterwards they can only be fixed if the fix is internal or additive.

### WP-A — Surface scoping (decide what actually freezes)
- **Decided 2026-06-20: keep `node.hpp` where it is — do not move to `detail/`.**
  The replay reset is done from the fluent layer (it reads/writes the public
  `Node::in_degree`), so moving `Node` is not required for the lifecycle.
  Relocating a header included by 18 files is broad and unverifiable without a
  build, so the safe choice is to leave the layout in place; the same applies to
  `dependency_tracker.hpp` and `event_polling.hpp`.
- Final classification per header: **stable-public** / **stable-advanced** /
  **detail (not frozen)** — still to be written down per header.

### WP-B — Correctness hardening (frozen guarantees must be real)
- **#30 EventPolling 256-event cap:** verify the overflow path; fix the silent
  deadlock risk if present. Keep the fix internal — do **not** document a "256
  in-flight max" guarantee, so the implementation stays free to change.
- **#29 ThreadLocalMagazine teardown:** prove the shutdown ordering is safe or add
  a liveness guard. No use-after-free ships in v1.
- **#33 EventPolling `Task*`→`Node*` cast:** confirm against
  `src/scheduler/scheduler.cpp`; clean up via a virtual `on_complete()` if still
  present.

### WP-C — The fluent lifecycle API (the actual surface, D1–D6) — **implemented**
- `Runtime::create()` / `rt.graph()` + `rt.buffer<T>()` / `rt.state<T>()`.
- Handle-based `add(...)` overloads with automatic dependency registration (D5),
  plus coupled multi-field rules `add(out, in0[, in1], fn)` (Jacobi semantics).
- Explicit `reads/writes` overload kept as the advanced escape hatch.
- `run()` / `run(n)` / `run_for` / `run_until` + `try_run()` / `try_run(n)`.
- Cached `CompiledGraph` + per-node `in_degree` reset + `state` double-buffer.
- `state<T>` / `buffer<T>` handle types over `RuntimeContext` allocation.

As-built divergences from the original sketch, found during implementation:
- **`compile()` is destructive** (it drains its builder), so the fluent `Graph`
  retains *task specs* and rebuilds a fresh backing graph on mutation, rather
  than re-compiling a persistent one.
- **Non-consuming execute** is an overload pair: `execute(CompiledGraph&)` (the
  replay path) plus an rvalue convenience that delegates — existing rvalue call
  sites are unaffected, avoiding a breaking interface change.
- **Double-buffering** uses a per-`State` shared `StateView` indirection the
  kernel reads at submit time; the once-per-step advancer updates it. This is
  also what makes coupled reads see the previous step (Jacobi).
- **Device co-location:** handles allocate via `RuntimeContext::malloc_shared_on`
  on one device, because the NUMA-local allocator round-robins and would
  otherwise split a graph's buffers across contexts.
- `add_node(work, reads, writes)` was fixed to retain its access lists (queue
  routing), matching `add_task`.
- `OpID` auto-generation for fluent tasks (for JIT/offload reach) is **not yet
  wired**; it remains an additive follow-up behind the existing `OpID` seam.

### WP-D — Forever-type decisions
- **`Core::Result<T,E>`:** declare it the *official forever type* (not a temporary
  `std::expected` shim). Even when the C++ baseline rises, `Result` stays our type
  so the return shape never breaks. *(Recommendation; open: confirm.)*
- Audit `Runtime::context()` / `create(HardwareFilterConfig)`: these pull
  `RuntimeContext` and `HardwareFilterConfig` into the frozen surface. Keep, but
  label `context()` "advanced".
- Lock the macro/shim surfaces that freeze forever: `SR_LOG_*`,
  `SushiRuntime::span`, `bit.hpp`, `sycl_compat.hpp`.

### WP-E — Release hygiene (low risk, additive)
- Single-include entry point: keep `SushiRuntime/SushiRuntime.h` as the one public
  umbrella. No separate `sushi.hpp` (decided 2026-06-20).
- Remove the orphaned comment at `graph/task_graph.hpp:161`.
- Add `@note Reserved: always 0 in v1` to each reserved `RunReport` field.
- CMake `VERSION 0.1.0` → `1.0.0`; resolve `SR_BUILD_TESTS` default (flip or note).
- Move `docs/slop/cpp_standard_migration.md` → `docs/guides/CPP_STANDARD_MIGRATION.md`.

### WP-F — Doxygen conformance sweep + tag
- Every public function in `include/` conforms to CONTRIBUTING §4 (why + how, every
  `@param`/`@return`, no separator comments). One behaviour-free pass.
- Tag `v1.0.0`. Surface is additive-only from here.

---

## 6. Seams that keep future updates painless (D8)

| Future capability | Lands behind (no surface change) |
|---|---|
| Runtime JIT (`add` from source string) | `IKernelSource` abstraction; existing `OpID` + `KernelRegistry` dispatch indirection |
| Incremental graph update (vs full recompile) | `IExecutionEngine` strategy + the lazy-compile cache (D2) |
| Real-time / event-driven stepping | Existing MPSC injector queue (`pending_submissions`) |
| Per-node / per-byte instrumentation | Reserved `RunReport` fields (filled, never reshaped) |
| Distributed offload from the fluent path | Auto-generated `OpID` on every fluent task (WP-C) |
| DAG cancellation (#37) | New additive method / `run` parameter |

---

## 7. Suggested order

1. **WP-A** (surface scoping) and **WP-B** (hardening) — must precede the freeze;
   they decide and stabilise what gets locked.
2. **WP-C** (the fluent lifecycle API) — the new surface, D1–D6.
3. **WP-D** (forever-type decisions).
4. **WP-E** + **WP-F** — hygiene, then the freeze tag.

## 8. Status and open items

- **WP-C (fluent lifecycle API): implemented and tested** — handles, stepping,
  coupling, replay, mutation-recompile (`tests/functional/integration/
  test_fluent_lifecycle.cpp`).
- **WP-A: resolved** — `node.hpp` stays public (not moved to `detail/`).
- **WP-D: `Core::Result` as the official forever type — recommended, awaiting
  confirmation.**
- Remaining before the freeze tag: **WP-B** (hardening #30/#29/#33), **WP-E**
  (version bump to 1.0.0, reserved-field `@note`s, remove the `task_graph.hpp`
  orphan comment), **WP-F** (Doxygen sweep), and wiring `OpID` auto-generation.
