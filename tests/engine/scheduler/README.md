# scheduler tests

The tests of the `scheduler` module, mirroring `modules/engine/scheduler/`.
`tests/engine/scheduler/CMakeLists.txt` builds them into `sr_scheduler_test_objects`, linked into `sr_engine_tests`. `sr test` runs them
with the rest; `sr run sr_engine_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.
A test's prefix names what it touches: `Small_` touches no device, thread, file or clock, and
`Medium_` touches a device or a thread. The medium tests that need a device take the fixture base of
`tests/support/runtime_environment.hpp`, so the program holds one runtime and one queue.

## compile_rebalancer_interfaces.cpp, compile_scheduler_log_category.cpp and compile_worker_pool_settings.cpp

Each file includes one header alone, so the header carries every include it needs. The headers hold
interfaces, a constant and a plain struct, which have no behaviour of their own to test.

## test_dispatch_record.cpp

The file tests the pairing-claim policy and the polled events of a dispatch. The claim and the
submission rules run as rows over their inputs; only the two tests that read a real event need a queue.

| Test | What it proves |
| --- | --- |
| `NothingClaimsWithoutProfiling` | No input claims a pairing with profiling off. |
| `AHostBodyClaimsOnlyForARealNode` | Under profiling a kernel claims, and a host body claims only for a node that is not the sentinel. |
| `AnEmptyHostBodySubmittedNothing` | A submission counts as device work when it is a kernel or hands an event over, and not when a host body returns a default event and hands none over. |
| `AnInlineHostNodeIsReady` | A host node that ran inline is pushed ready and holds no event. |
| `ASubmissionThatReachedNoDeviceIsReady` | A submission that reached no device is pushed ready and holds no event. |
| `ASubmissionThatReachedNoDeviceCarriesItsStepAndDevice` | The same submission still carries its task, step, device index and handed-over events. |
| `AnUnprofiledSubmissionRecordsNoSubmittingThread` | With profiling off no submitting thread is recorded. |
| `ASubmissionThatReachedADeviceWaitsOnItsEvent` | A submission that reached a device is pushed with its event, not ready. |
| `Medium_DispatchRecord.ACompleteProfiledEventStillCountsAsSubmitted` | A host-task event from a profiling queue that completed before the read still counts as submitted. |
| `Medium_DispatchRecord.ACompleteEventIsNoWorkWithProfilingOff` | With profiling off a complete event is no device work. |

The fixture derives its profiling queue from the context and device of the program's queue, once per
process, because the program's queue does not profile.

## test_event_polling.cpp

`ActiveEventList` must never drop an in-flight event. A dropped event leaves the successors of a
task untriggered and deadlocks the run. The list checks pin the inline-then-spill contract on the
data structure, without a live scheduler: they push far past the inline capacity, remove in every
order, and assert that nothing is lost and that swap-with-last identity holds across the boundary
between inline and spilled storage. A task is a tag in the pointer slot; the list never reads it.

| Test | What it proves |
| --- | --- |
| `PushNeverDropsBeyondInlineCapacity` | The list keeps every pushed event, in push order, across inline and spill storage. |
| `RemoveAtDrainsEveryEventExactlyOnce` | Removing index 0 until empty yields each pushed event once. The tail moves to the front on every step, which crosses the inline and spill boundary repeatedly. |
| `RemoveAcrossInlineSpillBoundary` | Removing an inline slot moves the one spilled tail entry into the freed slot. |
| `RemoveAtOutOfRangeIsANoOp` | `remove_at` past the end changes nothing. |
| `RemovingTheLastEntryMovesNothing` | Removing the last entry leaves the others where they were. |
| `TheFirstFailureStays` | `keep_first_failure` keeps the first failure whatever follows. |
| `TheFirstFailureIsLoggedNowhere` | The first failure is kept without a line. |
| `ALaterFailureIsLoggedAtWarnUnderTheSchedulerModule` | A later failure is logged once, at Warn, under the `scheduler` module. |
| `ALaterFailureLineNamesBothFailures` | The line names the dropped failure and the kept one. |
| `EveryLaterFailureIsLogged` | Three failures give two lines. |
| `AnEventThatCompletesIsNotAStallDrive` | A poll that finds progress counts no stall drive. |
| `AReapedTaskReleasesItsSuccessors` | A reaped task puts the successors it freed on the queue it was polled with. |
| `AReapedTaskRetiresItsOwnUnitOfTheLatch` | A reaped task counts its successor before it retires its own unit, so the latch drains only after the successor retires. |
| `Medium_EventPolling.CountsAStallDrive` | A poll that stalls waits on its oldest event once and counts the stall. |

The polls take a real `Deque`, the queue type the scheduler passes. The log lines go through
`LogCapture`, which installs a process-wide sink; an injected sink would let them run with no shared state.

## test_failure_naming.cpp

| Test | What it proves |
| --- | --- |
| `ATaskIsDescribedByItsName` | `describe_task` names a task by its name; the task is a `RangeTask`. |
| `ANodeIsDescribedByNameAndIndex` | A node is described by its name and its registration index. |
| `AFailureKeepsItsCodeAndNamesTheTask` | A named failure keeps the code and puts the task before the cause. |

## test_in_memory_telemetry.cpp

| Test | What it proves |
| --- | --- |
| `CountsOneWorkerPerQueueDepth` | In-memory telemetry counts one worker per held queue depth. |
| `AnswersEachWorkerWithItsHeldValues` | In-memory telemetry answers each worker with the depth and total held for it. |

## test_offload_gate.cpp

The file tests `OffloadGate` against `InMemoryOffloadCoordinator`, the adapter beside the port, and a real
`PlacementRules`.

| Test | What it proves |
| --- | --- |
| `AnOffloadableNodeIsOfferedToTheCoordinator` | An offloadable node is offered to the installed coordinator. |
| `AnAcceptedOfferIsCounted` | An offer the coordinator accepts is taken and counted. |
| `ARefusedOfferIsNotCounted` | An offer the coordinator refuses is not counted. |
| `TheGateOffersNothingWithNoCoordinator` | With no coordinator installed nothing is offered. |
| `TheGateOffersNothingThatIsNoNode` | A task that is no graph node is never offered. |
| `TheGateOffersNothingThatIsNotOffloadable` | A node that is not offloadable is never offered. |
| `TheGateOffersNothingToAnAbortingRun` | Nothing is offered while the run is aborting. |
| `PrepareReachesTheCoordinatorWithTheRulesItWasGiven` | `prepare` reaches an installed coordinator with the rules it was given. |

## test_quiescence_latch.cpp

| Test | What it proves |
| --- | --- |
| `AnEmptyLatchDoesNotWait` | An empty latch's wait returns at once. |
| `Medium_QuiescenceLatch.TheWaiterWakesAfterTheLastRetire` | A waiter wakes only after the last of three units retires. |

## test_range_task.cpp

The file covers `RangeTask`, the primitive for divisible workloads: the grain threshold, that
`split()` yields a distinct sub-task while work remains, that repeated splitting shrinks the owner
and terminates, that `split()` returns `nullopt` below the grain, and that the constructor refuses a
reversed range. All tests are deterministic.

| Test | What it proves |
| --- | --- |
| `Small_RangeTaskThreshold.IsSplittableAgreesWithSplitAroundTheThreshold` | Rows of (offset, size): a range splits only when the half it keeps reaches the grain of 1024, so the first size that splits is 2047, from offset 0 and from 4096; `is_splittable()` and `split()` agree on every row. |
| `SplitHandsOffDistinctChunksUntilExhausted` | Repeated splitting hands off a distinct chunk each time until the owner is below the grain. The helper keeps every chunk alive, so a freed and reused address cannot pass for a duplicate. |
| `AnExhaustedOwnerHandsOffNothing` | Once below the grain, `split()` reports nothing to hand off. |
| `AnEmptyRangeIsAcceptedAndDoesNotSplit` | An empty range is accepted and does not split. |
| `Medium_RangeTaskTest.SplitPartsCoverTheRangeExactlyOnce` | The parts of the range [37, 10044) run each once, with no gap and no overlap. `execute()` takes a `sycl::handler`, so each part runs in a submit to the program's queue. |
| `Medium_RangeTaskDeathTest.AReversedRangeStopsAtConstruction` | `RangeTask(8, 4, ...)` stops the process. A death test starts a child process. |

No split tells the owner's own range apart without a handler, so the coverage check stays on the queue.

## test_run_outcome.cpp

| Test | What it proves |
| --- | --- |
| `TheFirstFailureWins` | The first recorded failure is kept and a later one is not. |
| `AFailureAbortsTheRun` | A recorded failure aborts the run. |
| `ClearingTheFailureResetsTheFailureAndTheAbort` | `clear_failure` resets the failure and the abort it caused. |
| `CancellationAbortsWithoutAFailure` | `cancel` aborts the run and records no failure. |
| `ClearingTheCancellationLiftsTheAbort` | `clear_cancellation` lifts the abort. |

## test_recording_task_migrator.cpp

| Test | What it proves |
| --- | --- |
| `KeepsEveryRequestInOrder` | The recording migrator keeps every request in arrival order. |

## test_recording_rebalance_strategy.cpp

| Test | What it proves |
| --- | --- |
| `CountsEvaluationsAndMigratesNothing` | The recording strategy counts its evaluations and asks for no migration. |

## test_task_scheduler.cpp

The file builds each scheduler from a topology view over `InMemoryTopologyProvider` and
`MatrixDistanceModel`, a device set and an allocator, the parts a `RuntimeContext` would supply,
because a module test names no execution header. The device set makes a queue only at its first use,
and the sets of the placement and offload fixtures are built once per process.

### Medium_NodePlacement

The pool has two floating workers over a device set of two entries on NUMA node 0, both over the
default SYCL device. Each entry has its own queue, and two queues compare equal only when they are
copies of one another, so the queue a node receives names its entry. Each node is a host node that
submits device work, so the scheduler picks a queue for it, and its work records the queue it
receives. `USMAllocator::adopt()` records a host address as owned by an entry without allocating
device memory. A placement logic test could run small only with a host `DeviceQueueSet`, which does
not exist yet.

| Test | What it proves |
| --- | --- |
| `RegionlessNodesRunOnDeviceZero` | Two nodes with no region and no placement both receive entry 0. The test first asserts that the two entries' queues differ. |
| `APlacedNodeRunsOnItsDevice` | A node placed with `place_on(1)` receives entry 1. |
| `ANodeKeepsItsDeviceOnEveryStep` | A node dispatched four times receives the same entry each time. |
| `ANodeOverAnOwnedRegionRunsOnTheOwner` | A node that writes an owned address and states no device runs on the owner. |
| `AStatedPlacementInTheOwnersContextOutranksTheOwner` | A node that states entry 0, in the owner's SYCL context, runs on entry 0 and not on the owner. |
| `AnUnownedRegionLeavesTheStatedPlacementInForce` | A host address with no record has no owner, so the node runs on the placement it states. |
| `AnUnownedRegionWithNothingStatedRunsOnDeviceZero` | The same node that states nothing runs on entry 0. |
| `APlacementPastTheDeviceSetFailsTheRun` | `place_on(2)` on a two-entry set fails the run with `ErrorCode::InvalidGraph`, naming the node, the index and the device count; the node's work does not run. |

### Medium_OffloadSeam

The file pins what the scheduler tells an offload coordinator. A coordinator bumps the version of
every buffer a reported node writes and may send an offered node to another machine, so a node of
an aborted run must reach neither call. Each aborted-run test first runs one node on a healthy run and
asserts the count it expects to stay fixed, so a coordinator that was never called cannot pass.
The coordinator is `InMemoryOffloadCoordinator`, which declines every offer until told to accept.
The pool has two floating workers and the nodes are native host nodes, so no test submits to a queue.

| Test | What it proves |
| --- | --- |
| `AHealthyRunOffersAnOffloadableNode` | A healthy run offers an offloadable node once. |
| `ACancelledRunOffersNoNode` | After `request_cancellation()` the offer count stays at one and the node does not run. |
| `AFailedRunOffersNoNode` | After a node whose work threw, the offer count stays at one and the node does not run. |
| `ANodeSkippedByACancelledRunIsNotReportedAsCompleted` | A node of a cancelled run does not reach `on_local_completion()`. |
| `ADisabledNodeIsNotReportedAsCompleted` | A late-bound node disabled for the step does not reach it. |
| `ANodeWhoseWorkThrewIsNotReportedAsCompleted` | A node whose work threw does not reach it. |
| `ADeclinedOfferIsNotCounted` | `offloaded_task_count()` stays at zero for a declined offer, and the node runs on the pool. |
| `AnAcceptedOfferIsCounted` | An accepted offer raises the count to one and the pool does not run the node. |
| `ACompletedRemoteNodeStaysCounted` | `complete_remote()` drops the reference the coordinator held and leaves the count at one. |

The poller calls `on_local_completion()` before it drops the in-flight count, so the recorded list is final when `wait_quiescent()` returns.

### Execution and stop

These checks are fast and deterministic; heavy load is tested by
`tests/engine/execution/benchmark_scheduler_execution_engine.cpp`.

| Test | What it proves |
| --- | --- |
| `Medium_SchedulerIntegration.ExecutesExactlySubmitted` | The scheduler runs exactly the 2000 tasks submitted; the test waits on `wait_quiescent()`. |
| `Medium_SchedulerIntegration.PriorityBypass` | A late high-priority task completes within the first window of the pool. It still polls with `sleep_for`, since the pool has no single-threaded executor to replay the order from a seed. |
| `Medium_TaskScheduler.AbruptStopDrains` | `stop()` right after `submit` drains every one of the 1000 submitted tasks, without a deadlock. |
| `Medium_TaskScheduler.AWorkerCountOfZeroAsksForOneWorkerPerLogicalCore` | A worker count of 0 gives the platform's `hardware_concurrency` count, up to the pool maximum. |

### Steal order

NUMA-aware hierarchical work stealing is checked on any hardware through a synthetic topology. An
`InMemoryTopologyProvider` gives core i the node i % 2, with a matching `MatrixDistanceModel`. The
schedulers are built and not started, with an empty device set.

| Test | What it proves |
| --- | --- |
| `Small_NumaStealTest.LocalVictimsPrecedeRemote` | With the pool pinned to cores on two nodes, every worker lists its same-node victims before any remote one. A machine with one core has no victim to order. |
| `Small_NumaStealTest.FlatTopologyKeepsAllVictims` | A flat topology gives each of four workers every other worker as a victim, once. |

### Pool lines

The scheduler is built over an empty device set, with no thread started. The lines go through
`LogCapture`, which installs a process-wide sink.

| Test | What it proves |
| --- | --- |
| `Small_TaskScheduler.ARequestPastTheLargestPoolIsCutToIt` | A request past the largest pool is cut to it. |
| `Small_TaskScheduler.ACutRequestIsLoggedAtWarnNamingBothCounts` | The cut is logged once, at Warn, naming the requested count and the maximum. |
| `Small_TaskScheduler.AWorkerCountInsideTheLimitsLogsNoWarning` | A request inside the limits logs no warning. |
| `Small_TaskScheduler.APinnedWorkerWhoseCoreHasNoNumaNodeIsLogged` | A pinned worker whose core the topology does not list is mapped to NUMA node 0 and a Debug line says so, one per worker the pool kept. |
| `Small_TaskScheduler.AFloatingPoolLogsNoCoreMapLine` | A floating pool logs no such line; the test first asserts the sink saw the constructor's own line. |
| `Medium_TaskSchedulerCancel.LogsTheTasksASkippedRunLeftUnrun` | A cancelled run logs exactly one INFO naming the five tasks it skipped. |
| `Medium_TaskSchedulerCancel.ARunNobodyCancelledLogsNoSkipLine` | A run nobody cancelled logs no skip line; the test first asserts the sink saw the pool's own line. |

## test_thermal_proxy_strategy.cpp

The file drives the real `ThermalProxyStrategy` with `InMemoryTelemetry` and `RecordingTaskMigrator`.
The strategy migrates work away from a throttled worker, whose queue backs up while its completion
crawls, toward the least-loaded one, and leaves alone a long queue that still drains fast.
`completed_task_count()` is cumulative, so the strategy reads the change since its previous evaluation.

| Test | What it proves |
| --- | --- |
| `RescuesThrottledWorker` | A worker with a deep queue that completes at most 5 tasks is migrated half the gap to the shortest queue. |
| `IgnoresFastWorkerWithLongQueue` | A deep queue that drains faster than 5 tasks is not migrated. |
| `NoMigrationWhenBalanced` | Balanced or shallow queues trigger no migration. |
| `SingleWorkerIsNoOp` | A single worker triggers no migration. |
| `AMigrationMovesAtLeastTenTasks` | Queues of 25 and 20 move 10 tasks, not half the gap of 2. |
| `TheCompletionRateIsTheChangeSinceThePreviousEvaluation` | The same completed totals look fast at the first evaluation and throttled at the second. |

## test_workload_coordinator.cpp

The file covers the heartbeat loop of `WorkloadCoordinator`. Fakes for all three collaborators
(telemetry, migrator and strategy) keep the test on the host. The loop must drive
the strategy, start and stop cleanly, and swap strategies at runtime.

The loop sleeps 5 ms on the system clock and takes no clock of its own, so the tests still wait with
`wait_until_at_least`, which polls with `sleep_for`. A clock the test advances is the seam that would
remove the waits, the fakes and the 50 ms sleep that proves no evaluation after `stop()`; it needs a
change to the coordinator.
