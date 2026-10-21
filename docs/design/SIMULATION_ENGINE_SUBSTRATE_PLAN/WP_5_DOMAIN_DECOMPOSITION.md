# WP-5: Peer-to-peer domain decomposition

Large enough that the domain decomposition page treats it on its own. WP-5 builds the new distributed model:
**peer-to-peer** region ownership with halo exchange (hundreds of machines, L5),
designed so **entity migration is a clean later addition** (L6) and so **region
load/unload (streaming, L11)** is the *same* lifecycle machinery. It depends on
WP-1/WP-2/WP-3 and the WP-4 reduction primitives.

## Why the current distributed layer is the wrong axis for a large world

The existing layer is a **task-offload / work-pull** system, and a good one for its
purpose, but its model is structurally not domain decomposition:

- **It ships individual tasks**, not regions. `compile()` stamps a node offloadable
  and the scheduler ships it (`task_graph.cpp:180`, `scheduler.cpp:483-489`). A large
  world is not "send these tasks elsewhere"; it is "this machine *owns* this region
  of space forever and only exchanges boundaries."
- **Buffer transfer is whole-buffer.** `BufferPushMsg` carries the entire buffer's
  bytes (`protocol.hpp:316-338`); coherence is whole-buffer versioning: a write
  bumps the version and the worker re-pulls the **whole** buffer
  (whole-buffer version coherence). Halo exchange needs to send a
  *thin boundary* every step, not the whole region.
- **Single authoritative master.** All buffers live on and are served by one master
  (`distributed_coordinator.hpp`); workers are stateless shells. A large world wants
  **peer-to-peer** neighbor exchange, not a star topology through one master: the
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

## Spatial domain decomposition with halo exchange

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
**fixed grid partition with field/cell data only** (L6) (no cross-machine entity
hand-off) but the **ownership-transfer primitive is designed for migration from day
one** so entity migration is an additive use of it, not a redesign. The hard residual
sub-problem deferred is consistent global identity for a migrating entity (stable id
+ component hand-off); the region/ownership abstraction is what makes it tractable.

**Acceptance (MVP):** (a) a 2-D heat/stencil field partitioned across 2 processes
with a 1-cell halo produces results bit-identical (under WP-4) to the single-process
field; (b) a region can be unloaded on one machine and re-attached on another via the
ownership primitive, with field state preserved: proving the migration seam without
entity migration.
