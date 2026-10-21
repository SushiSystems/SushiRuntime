# WP-1: Persistent graph with late-bound bindings

**Problem:** topology and per-node size are frozen at compile; the target changes
both per frame.

**Design: split "shape" from "binding."** Generalise the distributed
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
