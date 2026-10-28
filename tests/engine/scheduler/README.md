# scheduler tests

The tests of the `scheduler` module, mirroring `modules/engine/scheduler/`.
`tests/engine/scheduler/CMakeLists.txt` builds them into `sr_scheduler_test_objects`, linked into `sr_engine_tests`. `sr test` runs them
with the rest; `sr run sr_engine_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_dispatch_record.cpp

The file tests the pairing-claim policy and the polled events of a dispatch, without a device.

| Test | What it proves |
| --- | --- |
| `NothingClaimsWithoutProfiling` | Nothing claims a pairing with profiling off. |
| `AHostBodyClaimsOnlyForARealNode` | A kernel claims; a host body claims only for a node that is not the sentinel. |
| `AnEmptyHostBodySubmittedNothing` | A host body with an empty event and no handed-over event submitted nothing. |
| `AnInlineHostNodeIsReady` | A host node that ran inline is pushed ready and holds no event. |
| `AnEmptyEventIsReadyAndUnprofiled` | An empty event is pushed ready, with no profiling fields when profiling is off. |

## test_event_polling.cpp

`ActiveEventList` must never drop an in-flight event. A dropped event leaves the successors of a
task untriggered and deadlocks the run. The checks pin the inline-then-spill contract on the data
structure, without a live scheduler: they push far past the inline capacity, remove in every order,
and assert that nothing is lost and that swap-with-last identity holds across the boundary between
inline and spilled storage.

### `RemoveAtDrainsEveryEventExactlyOnce`

Always removing index 0 relocates the tail to the front on every step, which crosses the boundary
between inline and spilled storage repeatedly.

### `RemoveAcrossInlineSpillBoundary`

The only spilled entry is the last one, with tag N. Removing an inline slot must move that tail into
the freed slot and shrink the spill to empty.

## test_range_task.cpp

The file covers `RangeTask`, the primitive for divisible workloads: the grain threshold, that
`split()` yields a distinct sub-task while work remains, that repeated splitting shrinks the owner
and terminates, that `split()` returns `nullopt` below the grain, and that the constructor refuses a
reversed range. All tests are deterministic. `SplitPartsCoverTheRangeExactlyOnce` submits to the
default SYCL device; the others construct no queue.

### `IsSplittableAgreesWithSplitAroundTheThreshold`

A range splits when the upper half, which the owner keeps, holds at least 1024 elements, so the
first size that splits is 2047. The test asks `is_splittable()` and `split()` about sizes 1024,
1025, 2046, 2047 and 2048, once from index 0 and once from index 4096, and the two must agree on
each.

### `SplitPartsCoverTheRangeExactlyOnce`

The range [37, 10044) is split as far as `split()` allows, chunks included. `execute()` takes a
`sycl::handler`, so each part runs inside a `queue.submit()`; the kernel records the range it is
given and submits an empty `single_task`. The recorded ranges, sorted, must run from 37 to 10044
with each one starting where the last ended.

### `AReversedRangeStopsAtConstruction`, `AnEmptyRangeIsAcceptedAndDoesNotSplit`

`RangeTask(8, 4, ...)` stops the process. An empty range, `start == end`, is accepted and does not
split.

### `SplitHandsOffDistinctChunksUntilExhausted`

The test keeps every chunk alive. An address that was freed and reused could otherwise look like a
duplicate, and the test could not assert that the chunks are distinct.

## test_task_scheduler.cpp

The file pins which device queue the scheduler hands a graph node. A kernel that captured USM the
runtime cannot see names no region, so nothing but the placement rule keeps it on the device that
owns its memory. Each node is a host node that submits device work, so the scheduler picks a queue
for it, and its work records the queue it receives and submits nothing.

### `Medium_NodePlacement`

The fixture builds the scheduler from a topology view, a device set and an allocator, as
`Medium_OffloadSeam` does. The device set holds two entries over the one device SYCL selects by
default, both on NUMA node 0. Each entry has its own `sycl::queue`, and two queues compare equal
only when they are copies of one another, so the queue a node receives names its entry.

### `dispatched_device`

`dispatched_device` runs one node and compares the queue its work received with the plain queue of
each entry. `wait_quiescent()` returns after the work ran, so the read needs no lock. The tests also
read `Node::cached_queue_index_`, the index the scheduler records on the node at its first dispatch,
which would still tell the entries apart if their queues could not.

### `RegionlessNodesRunOnDeviceZeroAndAPlacedNodeOnItsDevice`

Two nodes with no region and no placement both receive entry 0, and a third placed with
`place_on(1)` receives entry 1. A rotation over the NUMA node's devices cannot hand entry 0 to two
dispatches in a row. The test first asserts that the two entries' queues differ.

### `ANodeKeepsItsDeviceOnEveryStep`

Each node is dispatched four times, so every dispatch after the first takes the cached index.

### `AStatedPlacementInTheOwnersContextOutranksTheOwner`

`USMAllocator::adopt()` records a host address as owned by entry 1 without allocating device
memory. A node that writes it and states no device runs on entry 1; one that states entry 0, in the
owner's SYCL context, runs on entry 0.

### `AnUnownedRegionLeavesTheStatedPlacementInForce`

A host address with no record has no owning device, so the node runs on the placement it states,
and on entry 0 when it states none.

### `APlacementPastTheDeviceSetFailsTheRun`

`place_on(2)` on a two-entry set fails the run with `ErrorCode::InvalidGraph`. The message names the
node, the index and the device count, and the node's work does not run.

The Chase-Lev deque must never drop a task on overflow. A dropped task leaves the `in_degree` of its
successors above zero for good and deadlocks the run. The checks drive the ring past its fixed
capacity, single-threaded for exactness and with concurrent stealers for the lock-free path, and
assert that every pushed task comes back out exactly once.

The file pins what the scheduler tells an offload coordinator. A coordinator bumps the version of
every buffer a reported node writes and may send an offered node to another machine, so a node of
an aborted run must reach neither call. Each test first runs one node on a healthy run and asserts
the count it expects to stay fixed, so a coordinator that was never called cannot pass.

### `OnlyAcceptedOffersAreCounted`

`offloaded_task_count()` is what a RunReport's `tasks_offloaded_to_network` is read from. A node
the coordinator declines leaves it at zero and runs on the pool. A node the coordinator takes
raises it to one and does not run; the test then returns the node through `complete_remote()`,
which drops the reference the coordinator held.

### `Medium_OffloadSeam`

The fixture builds the scheduler from a topology view, a device set and an allocator, the parts a
`RuntimeContext` would supply, because a module test names no execution header. The pool has two
floating workers. The nodes are native host nodes, so no test submits to a device queue.

### `SingleNodeTopology`

`SingleNodeTopology` and `SingleNodeDistance` describe one NUMA node with no core map, which is all
an unpinned pool reads.

### `CountingCoordinator`

`CountingCoordinator` declines every offer, so an offered node still runs locally and
`wait_quiescent()` covers it.

### `AbortedRunOffersNoNode`

The run is aborted twice: once by `request_cancellation()`, once by a node whose work throws. The
offer count must stay at the one offer of the healthy run both times.

### `SkippedNodeNotReportedAsCompleted`

Three kinds of node did not run to their end: a node of a cancelled run, a late-bound node
disabled for the step and a node whose work threw. None may reach `on_local_completion()`. The poller makes that call before
it drops the in-flight count, so the counter is final when `wait_quiescent()` returns.

### `Medium_SchedulerIntegration` and `Medium_TaskScheduler`

These checks are fast and deterministic; heavy load is tested by
`tests/engine/execution/benchmark_scheduler_execution_engine.cpp`. The scheduler must run exactly
the tasks submitted, honor priority bypass, survive a clean round of arm, submit and stop, and drain
without a deadlock when it is stopped under load.

### `Medium_NumaStealTest`

NUMA-aware hierarchical work stealing is checked on single-node hardware through a synthetic
topology. A `RemappingTopologyProvider` spreads the cores across N nodes (core i goes to node
i % N) with a matching `MatrixDistanceModel`. The tests then assert the locality invariant: victims
on the local node come before remote ones.

## test_thermal_proxy_strategy.cpp

The file drives the real `ThermalProxyStrategy` with a fake telemetry and a fake migrator. The rule
of the strategy is to migrate work away from a throttled worker, whose queue backs up while its
completion crawls, toward the least-loaded one, and to leave alone a long queue that still drains
fast. `completed_task_count()` is cumulative, so on the first `evaluate()` the delta equals the
reported total.

## test_workload_coordinator.cpp

The file covers the heartbeat loop of `WorkloadCoordinator`. Fakes for all three collaborators
(telemetry, migrator and strategy) keep the test deterministic and on the host. The loop must drive
the strategy, start and stop cleanly, and swap strategies at runtime.

### `wait_until_at_least`

`wait_until_at_least` spins until the value reaches the target or the budget elapses. The tests use
it to synchronize their assertions with the background threads of the coordinator.

## test_failure_naming.cpp

The file tests describe_task() and name_failure(): a task by name, a node by name and index.

| Test | What it proves |
| --- | --- |
| `ATaskIsDescribedByItsName` | A plain task is described by its name. |
| `ANodeIsDescribedByNameAndIndex` | A node is described by its name and its registration index. |
| `AFailureKeepsItsCodeAndNamesTheTask` | A named failure keeps the code and puts the task before the cause. |

## test_offload_gate.cpp

The file tests OffloadGate: what it offers a coordinator, what it never offers, and what it counts.

| Test | What it proves |
| --- | --- |
| `AnOffloadableNodeIsOfferedAndCounted` | An offloadable node is offered, and counted once accepted. |
| `TheGateOffersNothingItMustNot` | Checks the four cases the gate never offers: no coordinator, no node, not offloadable, aborting. |
| `PrepareReachesTheCoordinator` | Prepare() reaches the coordinator, and does nothing without one. |

## test_quiescence_latch.cpp

The file tests QuiescenceLatch: a wait returns once every added unit retired.

| Test | What it proves |
| --- | --- |
| `AnEmptyLatchDoesNotWait` | An empty latch does not wait. |
| `TheWaiterWakesAfterTheLastRetire` | The waiter wakes only after the last of three units retires. |

## test_rebalancer_interfaces.cpp

The file tests the rebalancer contracts: a strategy reads telemetry and moves work through a
migrator.

| Test | What it proves |
| --- | --- |
| `AStrategyMovesWorkThroughTheMigrator` | Reports fixed queue sizes for two workers. */ class FixedTelemetry final : public TelemetryProvider { public: std::size_t worker_count() const override { return 2; } std::size_t queue_size(int worker_id) const override { return worker_id == 0 ? 10 : 2; } std::size_t completed_task_count(int) const override { return 0; } }; /** @brief Records each migration it is asked for. */ class RecordingMigrator final : public TaskMigrator { public: void migrate_tasks(int from_worker_id, std::size_t count) override { moves.push_back({from_worker_id, count}); } /** @brief Holds one migration. */ struct Move { int from; /**< The worker the tasks leave. */ std::size_t count; /**< How many leave. */ }; /** @brief Holds the migrations in order. */ std::vector<Move> moves; }; /** @brief Moves half the difference from the fullest worker to even the two queues. */ class EvenOutStrategy final : public RebalanceStrategy { public: void evaluate_and_rebalance(const TelemetryProvider& telemetry, TaskMigrator& migrator) override { const std::size_t first = telemetry.queue_size(0); const std::size_t second = telemetry.queue_size(1); if (first > second) migrator.migrate_tasks(0, (first - second) / 2); } }; } /** @brief Tests that a strategy reads the telemetry it is given and asks the migrator once. |

## test_run_outcome.cpp

The file tests RunOutcome: the first failure wins, cancellation, clearing and aborting.

| Test | What it proves |
| --- | --- |
| `TheFirstFailureWins` | The first recorded failure is kept and a later one is not. |
| `CancellationAbortsWithoutAFailure` | Cancellation aborts on its own and clears on its own. |

## test_worker_pool_settings.cpp

The file tests WorkerPoolSettings: one worker per core, pinned from core 0, by default.

| Test | What it proves |
| --- | --- |
| `DefaultsAskForOneWorkerPerCore` | Checks the defaults of the settings. |

## test_event_polling.cpp: the first failure and the stall drives

| Test | What it proves |
| --- | --- |
| `Small_EventPolling.KeepsTheFirstFailureAndLogsTheSecond` | The first failure of a poll is kept and a later one is logged, not returned. |
| `Small_EventPolling.EveryLaterFailureIsLoggedAndTheFirstStays` | A third failure is logged too and the first stays. |
| `Medium_EventPolling.CountsAStallDrive` | A poll that stalls waits on its oldest event once and counts it. |
| `Medium_EventPolling.AnEventThatCompletesIsNotAStallDrive` | A poll that finds progress counts no stall drive. |

## test_task_scheduler.cpp: the lines about the pool and a cancelled run

| Test | What it proves |
| --- | --- |
| `Medium_TaskScheduler.LogsAClampedWorkerCount` | A request past the largest pool is cut to it, with a line saying so. |
| `Medium_TaskScheduler.AWorkerCountInsideTheLimitsLogsNoWarning` | A request inside the limits logs nothing about the count. |
| `Medium_TaskScheduler.LogsAWorkerWhoseCoreHasNoNumaNode` | A pinned worker whose core the topology does not list is mapped to NUMA node 0, and says so. |
| `Medium_TaskScheduler.AFloatingPoolLogsNoCoreMapLine` | A floating pool has no core to map and logs no such line. |
| `Medium_TaskSchedulerCancel.LogsTheTasksASkippedRunLeftUnrun` | A cancelled run logs one INFO with the number of tasks it skipped. |
| `Medium_TaskSchedulerCancel.ARunNobodyCancelledLogsNoSkipLine` | A run nobody cancelled logs no skip line. |
