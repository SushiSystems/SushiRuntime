# Simulation substrate plan: evidence index

**Status:** Shipped — 2026-10-21

The anchors behind [the simulation substrate plan](../../design/SIMULATION_ENGINE_SUBSTRATE_PLAN/README.md),
which moved them out when it took the design format. The line numbers are those of the tree when
the plan was written.

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
