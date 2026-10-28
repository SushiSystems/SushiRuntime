# RW-6 Rule on the public-surface reshapes of the API review

The row of this entry is in `docs/backlog/GENERAL.md`. This file holds what did not fit the row.

- State: decision
- Done when: the owner has ruled on each reshape, each a breaking change of its own

QC-21, 22, 23 (`IExecutionEngine`, `Node`, `TaskScheduler` responsibilities); QC-56 (types in
`buffer.hpp`); QC-57, 67, 68 (`Recorder` listener, `untracked_kernel` order, `scratch()` span);
QC-59 (no `Dynamic` overload); QC-63 (`dynamic_graph.hpp` includes `runtime.hpp`); QC-85
(`Node::profiling` and `Core::Backend` unread); QC-109 (`EpochManager` singleton); QC-113, 114
(topology distances); QC-122 (`WorkerStats`); QC-126 (interop handle switch); QC-93, 103, 116,
136 (naming, `enqueue_host_task`).
