# Design map

One document per topic. Look the topic up here before writing; extend the document that exists.

| Topic | Document | Status |
| --- | --- | --- |
| Running a graph's device work as one submission: the allocation removal, the in-order chain, record and replay | [`GRAPH_REPLAY.md`](GRAPH_REPLAY.md) | Open: P0, P1 and P3 built, P2 not started |
| What SushiEngine needs from the runtime at its edges: interop, co-tenancy, determinism, packaging, the ship path (BB-0 to BB-8) | [`ENGINE_BACKBONE_REFACTOR.md`](ENGINE_BACKBONE_REFACTOR.md) | Open: BB-1b unbuilt, BB-5 to BB-8 not re-checked |
| What a physics engine asks of the runtime (R1 to R9) | [`PHYSICS_SUBSTRATE_REQUIREMENTS.md`](PHYSICS_SUBSTRATE_REQUIREMENTS.md) | Open: R9 |
| Where work runs across the stack: placement class and scope on a node, the topology of machines and devices, placement rules, the distributed layer's needs (R1 to R14) | [`LAYERED_EXECUTION.md`](LAYERED_EXECUTION.md) | Open: designed and ruled, nothing built |
| The runtime as a simulation and game-engine substrate (WP-0 to WP-7) | [`SIMULATION_ENGINE_SUBSTRATE_PLAN.md`](SIMULATION_ENGINE_SUBSTRATE_PLAN.md) | Open: WP-5 and WP-7 not built |

The backlog is [`REMAINING_WORK.md`](REMAINING_WORK.md).

## Names that left the runtime

The four design documents name the reductions, the prefix scan and the counting sort
(`add_reduce`, `add_segmented_reduce`, `add_prefix_scan`, `add_counting_sort`). They leave the
runtime for SushiBLAS in v0.4.0, and the runtime keeps the `Recorder` interface they are built
on. Read every mention of them in a design document as the state before that release.

Designs that shipped are in the archive: the USM allocation seam, the G1 node event records and
the prefix scan and counting sort under `docs/archive/superpowers/specs/`, and the physics
substrate implementation plan under `docs/archive/slop/`.
