# Distributed State — design note

## Problem

`State<T>` double-buffers a field: it owns two USM allocations (`cur_`, `next_`)
and swaps their roles every step with an O(1) pointer exchange (`advance()`). The
fluent layer makes this work locally because each kernel reads `cur`/`next` fresh
from a `StateView` at *submit* time, so one compiled node follows the swap with no
recompilation.

The distributed coherence model keys on the **USM base pointer**. `BufferRegistry`
maps a pointer to a `BufferHandle{id, version}`; the master bumps the version on a
local write and workers cache by `id + version`. Crucially, `TaskGraph::compile()`
resolves each offloadable node's handles **once**, from the static
`dist_reads_`/`dist_writes_` pointers, and bakes them into `node->in_handles_` /
`node->out_handles_` (see `src/graph/task_graph.cpp`). The fluent `Graph` then
*replays* that compiled plan across every step without re-resolving handles.

These two facts collide. A `State` step is "read allocation P, write allocation Q"
on even steps and "read Q, write P" on odd steps. The locally-replayed plan handles
this via the live `StateView`; but the **baked distributed handles are static**, so
on alternating steps an offloaded `State` step would ship the wrong allocation's
handle to the worker. That is the precise reason `State::distributed()` does not
exist today — silently enabling it would corrupt coherence, not merely run slow.

## Options

### Option A — Per-step handle re-resolution (transparent, performant)
Stop baking distributed handles at compile time for nodes whose buffers rotate.
Give the offloadable node an indirection (analogous to `StateView`) that the fluent
layer re-points each step, and have the coordinator resolve `in_handles_` /
`out_handles_` from the **live** pointers at *dispatch* time instead of reading the
pre-baked vectors.

- Pros: `State::distributed()` "just works"; keeps the single-compile replay fast.
- Cons: touches the distributed dispatch contract (`distributed_coordinator`,
  `node`, `compile()`); must keep the version fence correct as handles move; this is
  the layer with prior segfault history, so it needs ASan + the full `*Distributed*`
  suite.
- Risk: **high**. Scope: distributed core, not just the fluent header.

### Option B — `DistributedState` helper, recompile-per-step (fluent-only, correct)
A new fluent type owning two `.distributed()` Buffers with `current()/next()/
advance()`. Because the offload plan bakes handles, a distributed run drives one
*single-step* graph per iteration (recompile each step) so handles re-resolve from
the current roles. Correct and entirely in the fluent layer.

- Pros: no distributed-core change; coherence model untouched; ships this session.
- Cons: loses the compiled-plan replay optimisation for distributed stepped runs
  (recompile per step). Acceptable when the per-step kernel dominates compile cost,
  which is the case the cluster targets.
- Risk: **low**. Scope: fluent header + a thin driver.

### Option C — Document the manual two-Buffer pattern only
Keep the status quo; document staging two distributed `Buffer`s and managing the
ping-pong by hand (what the current `buffer.hpp` note already suggests).

- Pros: zero new code.
- Cons: leaves gap #3 effectively unaddressed for fluent users.
- Risk: none. Scope: docs.

## Decision — Option A (implemented)

Option A was chosen: transparent `State::distributed()` with dispatch-time handle
resolution. The implementation keeps the coherence model intact and adds no
recompilation cost:

- `Node` carries optional `live_reads_` / `live_writes_` shared pointer lists. They
  are null for every ordinary node, so the hot path is unchanged.
- `TaskGraph::add_offload_dynamic()` builds an offload node that holds those live
  lists. `compile()` (`stamp_offloadable`) marks such a node offloadable but does
  **not** bake handles.
- `DistributedCoordinator::try_offload()` resolves the in/out handles from the live
  lists via the registry at dispatch (versions still refreshed and fenced exactly as
  before); an unregistered pointer falls the task back to local.
- The fluent `Graph::add(op, State, fn)` overload registers a per-step updater that
  re-points the live lists at the step's current/next allocations before each step,
  alongside the existing `StateView` and advance machinery. `State::distributed()`
  registers both allocations.

### Validation owed

This touches the distributed dispatch path, which has prior segfault history. Before
merge it must pass `sr project build --distributed` with the `*Distributed*` tests
and an ASan run, plus a multi-step offloaded-State case that asserts the result
matches the local single-machine run across an even and an odd number of steps (to
exercise both swap parities).
