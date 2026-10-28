# Design map

One document per topic. Look the topic up here before writing; extend the document that exists.

## Topics

<!-- topics:begin -->
| Topic | State | About |
| --- | --- | --- |
| [ENGINE_BACKBONE_REFACTOR](ENGINE_BACKBONE_REFACTOR/README.md) | Open | The runtime's simulation core is already the right machine: dependency-inferred DAG, compile-once replay, late-bound sizes and bases, fixed-order deterministic reductions, interval-exact... |
| [GRAPH_REPLAY](GRAPH_REPLAY/README.md) | Open | SushiEngine's matter device solver is launch-bound on the device. |
| [LAYERED_EXECUTION](LAYERED_EXECUTION/README.md) | Open | What SushiRuntime adds so that SushiBLAS, SushiAI and SushiEngine can say where their work runs without writing it into their code: a placement vocabulary on every node, a description of... |
| [PROFILING](PROFILING/README.md) | Open | How SushiRuntime reports its own time to SushiProfiler and to any observer, and which timing leaves its public report. |
| [SIMULATION_ENGINE_SUBSTRATE_PLAN](SIMULATION_ENGINE_SUBSTRATE_PLAN/README.md) | Open | SushiRuntime as a simulation and game-engine substrate. |
<!-- topics:end -->

The backlog is [`REMAINING_WORK.md`](../backlog/README.md).

## Names that left the runtime

The four design documents name the reductions, the prefix scan and the counting sort
(`add_reduce`, `add_segmented_reduce`, `add_prefix_scan`, `add_counting_sort`). They leave the
runtime for SushiBLAS in v0.4.0, and the runtime keeps the `Recorder` interface they are built
on. Read every mention of them in a design document as the state before that release.

Designs that shipped are in the version history: the USM allocation seam, the G1 node event records,
the prefix scan and counting sort, and the physics substrate implementation plan.
