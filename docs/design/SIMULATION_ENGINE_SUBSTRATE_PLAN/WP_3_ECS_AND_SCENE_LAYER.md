# WP-3: The ECS and scene layer, a new domain above the runtime

**Problem:** there is no entity/component/system model. This is where "the graph
behaves like a game engine" actually gets built, and it must be built *on* the
runtime, not *in* it (per the project's downward-only layering principle).

**Storage recommendation (answering the owner's "you decide", L-context).** Given
10⁶–10⁹ entities (L2), central collision producing per-frame neighbour sets (L7),
and mandatory streaming/LOD (L11), the recommendation is **archetype chunks**, not
dense per-component arrays: fixed-size chunks (e.g. 16 K entities) are the natural
unit for (a) cache-friendly SoA iteration, (b) **streaming**: load/unload a region
as a set of chunks, (c) **domain decomposition**: a chunk is the migration unit
(L6), and (d) **PGS graph colouring** (L8): colour at chunk granularity. The cost
is that chunks make WP-2 region identity **mandatory** (chunks are sub-regions of a
component's backing store), which is already on the critical path. Dense arrays would
be simpler but cannot express streaming or migration cleanly, so they are rejected
for this target.

**Design: a thin ECS that compiles to the task graph:**

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
  a **command buffer** applied at an explicit sync barrier between phases: modeled
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
  needs no solver knowledge: colouring is a domain concern that expresses itself as
  ordinary read/write sets. Collision broadphase (L7) rebuilds the constraint/colour
  sets per frame, which is exactly the WP-1 dynamic-topology case.

**Repo (L14):** WP-3 lives in the **SushiEngine** repository, not SushiRuntime. It
depends only on `<sycl/sycl.hpp>` + SushiRuntime's public fluent API. No
runtime-core changes beyond the public seams WP-1/WP-2/WP-6 expose. If WP-3 finds it
*needs* a runtime change, that change is a new public seam in SushiRuntime (a
SushiRuntime task), keeping the role split (L14) intact.

**Risks:** archetype/chunk layout interacts with WP-2 region identity: design them
together. Command-buffer application is a serialization point; keep it O(changes),
not O(entities).

**Acceptance:** a worked example: N entities with `Position/Velocity/Mass`
components, `Integrate` and `ApplyForces` systems running in parallel where
non-conflicting, spawning/destroying entities per frame with no per-frame recompile,
producing correct results vs a reference single-threaded loop.
