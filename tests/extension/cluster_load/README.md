# cluster_load tests

The tests of the `cluster_load` module, mirroring `modules/extension/cluster_load/`.
`tests/extension/cluster_load/CMakeLists.txt` builds them into `sr_cluster_load_tests`. `sr test`
runs them with the rest; `sr run sr_cluster_load_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_offload_policy.cpp

The tests need no hardware and no cluster.

| Test | What it proves |
| --- | --- |
| Offload policy tests | The policy tests call `pick_worker()` of `LeastLoadedOffloadPolicy` with synthetic `WorkerLoad` and no cluster. They cover the byte gate, the round-trip and backlog terms of the estimate, the free-memory gate, and the worker that is not alive. |
| `live_worker`, `pick` | `live_worker` builds the `WorkerLoad` of one live worker. `pick` asks a default policy to place a task of a given size on a list of workers. |

## test_cluster_statistics.cpp

The file tests ClusterStatistics and WorkerSnapshot: everything starts at zero and empty.

| Test | What it proves |
| --- | --- |
| `StartAtZero` | The statistics of a cluster with no worker are zero. |

## test_placement_decision.cpp

The file tests PlacementDecision, the variant the dispatcher visits.

| Test | What it proves |
| --- | --- |
| `Small_PlacementDecision.DefaultIsAWorkerOnDeviceZeroWithNoId` | A default decision is `RunOnWorker` with id -1 and device 0. |
| `Small_PlacementDecision.WaitAndFailCarryTheirFacts` | `WaitForTarget` carries its end and `FailRun` its error. |
