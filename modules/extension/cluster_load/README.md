# cluster_load

The module holds the load of a cluster's workers as a master sees it: the policy that chooses the
worker of a task from their loads, and the statistics that report them. It is built with the
distributed layer, under `SR_ENABLE_DISTRIBUTED`, and every name it declares is in
`SushiRuntime::Distributed`. Its headers are installed: a program passes its own policy in
`ClusterOptions` and reads `Cluster::statistics()`.

It depends on `core`, `api` and `machine`.

## Public entry points

| Header | Holds |
| --- | --- |
| `offload_policy.hpp` | `OffloadPolicy`, `WorkerLoad`, `LeastLoadedOffloadPolicy` |
| `cluster_statistics.hpp` | `ClusterStatistics`, `WorkerSnapshot` |
| `placement_decision.hpp` | `PlacementDecision` and its outcomes `RunOnWorker`, `RunLocally`, `WaitForTarget`, `FailRun` |

## offload_policy.hpp

`WorkerDirectory::pick()` offers the policy the registered, alive workers that are below their
`max_jobs` and registered the kernel of the task's operation, under the directory's mutex. It
does not call the policy when no such worker has room, and an answer that names a worker it did
not offer counts as no pick. Workers of one cluster may therefore hold different kernels.

`WorkerLoad::facts` points at the `NodeFacts` the worker sent in `HELLO`: its name
(`facts->name`), operating system, labels and devices. The pointer is not null in a load the
directory builds and is valid only during the `pick_worker()` call, so a policy that keeps a fact
copies it. The facts belong to the worker's name, not to its `worker_id`: a worker that reconnects
under the same name gets a new `worker_id` and its newest facts. `machine` is a dependency of
this module for that type.

`LeastLoadedOffloadPolicy` is the default. It returns nothing, which runs the task locally, when
the task's inputs total fewer bytes than `min_offload_bytes` (64 KiB unless the constructor is
given another value). It skips a worker whose `free_mem` is not 0 and is below the input size.
Among the rest it picks the lowest cost, in microseconds: `rtt_micros`, plus the input bytes
divided by 1000, plus 1000 per inflight task. The two constants are in
`source/offload_policy.cpp`.

## placement_decision.hpp

`PlacementDecision` is a `std::variant` of the four places a task can go at dispatch, a closed
set that `OffloadDispatcher::try_offload()` visits in one place:

| Outcome | Carries | The dispatcher |
| --- | --- | --- |
| `RunOnWorker` | `worker_id`, `device_index`, an optional `deadline` | sends `TASK_DISPATCH` with that `device_index`; the deadline replaces the cluster's for the task |
| `RunLocally` | nothing | declines, and the pool runs the task |
| `WaitForTarget` | `until`, `time_point::max()` for a busy target | holds the node in `HeldNodes` until a worker registers, frees a slot or leaves, or `until` passes |
| `FailRun` | an `Error` naming the rule and its target | ends the run through `OffloadSink::fail()`; see `offload_coordination/README.md` |

`OffloadPolicy::pick_worker()` keeps its signature: the dispatcher turns its answer into
`RunOnWorker` on device 0, or `RunLocally` for none, so a policy written before rules works
unchanged. A rule's decision comes from `RulePlacement` of `offload_coordination`.

## cluster_statistics.hpp

`Cluster::statistics()` returns `ClusterStatistics`, which the coordinator assembles from the
brick that causes each count. What each field means to a user is in §9 of the guide.

| Fields | Kept by |
| --- | --- |
| `offloaded`, `declined`, `send_queue_full` | `OffloadDispatcher` |
| `tasks_reaped_deadline`, `tasks_reaped_unacknowledged`, `tasks_reaped_lost_result`, part of `redispatched` | `ClusterMonitor` |
| `chunks_sent`, `chunks_received`, `transfers_aborted`, part of `stale_writebacks` | `BufferExchange` |
| `completed_remote`, `handshake_rejected_auth`, `handshake_rejected_version`, `peers_lost`, `peers_closed_malformed`, the rest of `redispatched` and `stale_writebacks` | `DistributedCoordinator` |
| `bytes_sent`, `bytes_received` | the transport |
| `alive_workers`, `workers` | `WorkerDirectory` |
| `pending_tasks` | `PendingTasks` |
| `held_nodes` | `HeldNodes` |
