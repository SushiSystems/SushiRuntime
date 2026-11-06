# execution tests

The tests of the `execution` module, mirroring `modules/engine/execution/`.
`tests/engine/execution/CMakeLists.txt` builds them into `sr_execution_test_objects`, linked into `sr_engine_tests` and
`sr_execution_benchmarks`, built under `SR_BUILD_BENCHMARKS`. `sr test` runs them with the rest;
`sr run sr_engine_tests -- --gtest_filter='<pattern>'` runs one program alone.

A benchmark is a `Benchmark_` suite. It prints its figures and passes or fails nothing about
speed or correctness; `sr bench` runs it. The correctness promises live in the sized tests of
`test_*.cpp`.

Three design documents are cited by path in the sections that came from the integration tests:

- `docs/design/GRAPH_REPLAY/README.md`, the replay layers and their phases.
- `docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN/`, the work packages WP-0 to WP-7 and their
  numbered sections.
- `docs/design/ENGINE_BACKBONE_REFACTOR/`, the backbone items BB-0 to BB-8.

A section is named after a test file, and a subsection after the test, helper or topic it describes.
Test prefixes and the device fixture follow `tests/engine/scheduler/README.md`.

## compile_execution_log_category.cpp

The file includes `execution_log_category.hpp` alone, so the header carries every include it needs.
The header holds one constant, which has no behaviour of its own to test.

## benchmark_runtime_context.cpp

This is the benchmark behind section E, item 7 of
the g1 node event records design spec in the version history. That item decided
F2: a second, lazily built profiling queue per device, instead of always profiling and
filtering afterwards. The measured delta was about 24 microseconds per submission on an
RTX 3080 Ti under CUDA.

The warm-up round is untimed so that the first submission's driver and JIT costs reach neither
queue's timed rounds. The queue that goes first alternates each round, so a systematic drift,
thermal or driver warm-up, does not always favour the same queue.

The benchmark measures CPU and GPU concurrency. Two kernels dispatched without a wait between
them run together, so the wall time approaches `max(T_cpu, T_gpu)` instead of the serial sum.
Overlap only shows when the two run times are comparable, so the GPU iteration count is
calibrated to the CPU kernel first. The figure covers device queue overlap only: the scheduler
and the host-to-device and device-to-host transfers are outside it.

`best_of` keeps the fastest of several runs, which strips scheduling and operating system
noise from the timing.

`launch_heavy` runs a non-linear recurrence, `acc = sin(acc) * 1.0000001 + 0.5`, once per
iteration. A linear body would let the compiler collapse the loop, and the kernel would stop
scaling with `iters`.

The suite is `Benchmark_AsyncOverlap`. It prints the speedup and sets no bar. A speedup near
1.0x means the work serialized; `CUDA_LAUNCH_BLOCKING=1` makes every launch synchronous and
shows up that way. Under WSL2 the CUDA backend adds dispatch latency, which lowers the figure
even while the devices overlap.

The suite `Benchmark_ProfilingQueue` is the one named above. Its equal-result check is held by
`Medium_ProfilingQueue.USMFromThePlainQueueIsUsableOnTheProfilingQueue` in `test_runtime_context.cpp`.

## benchmark_scheduler_execution_engine.cpp

The file closes two gaps: how `TaskGraph::compile()` scales with graph size, and whether
overlapping host-to-device and device-to-host transfers with compute helps. Both live in
`Benchmark_GraphCompile`. The transfer test does identical work on each side and prints whether
each result matches the host reference, within the rounding bound `tests/support/tolerance.hpp`
derives from the `expected()` recurrence. It sets no bar, because real overlap needs a discrete GPU
with copy engines. That a compiled graph has roots is held by the `Medium_TaskGraphTest` tests of
`test_scheduler_execution_engine.cpp`.

`min_compile_us` uses a rolling set of 16 buffers so that consecutive tasks form real
dependencies, which is what exercises the dependency tracker.

`Benchmark_SchedulerDispatch` measures dispatch overhead per task. Both sides do the same work,
one relaxed atomic increment, so the only difference is the path: inline in a tight loop, which
is the floor, or wrapped in a task and run through the work-stealing scheduler. The delta is the
amortized cost of submit, steal and dispatch. It reports the delta and sets no bar. That the
scheduler runs every task it is given is held by `Medium_SchedulerIntegration.ExecutesExactlySubmitted`
in `tests/engine/scheduler/test_task_scheduler.cpp`.

`Benchmark_StealLocality` is a real-hardware benchmark for NUMA-aware hierarchical stealing. It
drives the scheduler with a large batch of host tasks and reports throughput and the split
between local and remote steals. Locality exists only across real NUMA nodes, so it skips on a
single-node machine; `Medium_NumaStealTest` holds the logic check.

`Benchmark_StealNumaEffect` compares NUMA-aware with NUMA-blind stealing on the same hardware.
One workload runs once on the real, flat topology and once on a synthetic two-node topology, and
the suite reports both throughputs and the aware side's steal split. On a UMA machine, single
socket or WSL, there is no real speedup to claim.

The steal split is gathered while the workload is timed, so it reflects locality only when the
cores sit on separate NUMA nodes. On a single-socket machine the two synthetic nodes share one
set of cores, and OpenMP and runtime worker threads contending for them drag the local
fraction down; 0.46 to 0.48 was observed. `Medium_NumaStealTest` verifies the locality
ordering deterministically.

`Benchmark_WideFanout` is the gate benchmark for the deque-steal fast path, audit steal-1, and the global quiescence
counter, audit sched-1. It profiles them in the substrate's characteristic shape: many small
steps, not one long sustained stream. Each step submits a modest batch of independent tasks
and drains to quiescence. The tail of every step is where workers spin idle, stealing from
empty peers, and steal-1 removes a `std::memory_order_seq_cst` fence from each empty attempt. It is also where
`inflight_` and the shared counters are hammered, which is the contention sched-1 targets.

The benchmark reports drain latency per step and nanoseconds per task. That isolates the fixed
overhead per step that dominates the substrate, and complements the single sustained stream of
`Benchmark_SchedulerDispatch`. Attributing steal-1's fence removal and sched-1's line
contention in full needs hardware counters, `perf stat` and `perf c2c`. This benchmark is the
portable measure of throughput and latency, and the harness those counters attach to. That a
scheduler armed once runs every task of each step it is handed is held by
`Medium_SchedulerSteps.EachStepRunsAllOfItsTasksOnTheArmedScheduler` in
`test_scheduler_execution_engine.cpp`.

## test_execution_engine.cpp

The file covers `ExecutionEngine::run_in_progress()`: false on the interface default, true
while a run is in flight, false once the run has been collected, whether it succeeded or threw.
No test reads a clock. Where a run must still be in flight at the check, its host task waits on a
`std::promise` the test fulfils afterwards.

### `MinimalEngine`

An engine that implements only the two pure members. `Small_RunInProgress.TheInterfaceDefaultReportsNoRun`
reads the interface's default through it.

### `ABlockingRunIsInProgressUntilExecuteReturns`

The run's one host task asks the engine and stores the answer, which must be true; after
`execute()` returns the answer is false.

### `AnAsynchronousRunIsInProgressUntilCompleteReturns`

The answer is true between `submit()` and `complete()` and false after. The task cannot finish
before the first check, because it waits on the gate.

### `ABlockingRunWhoseTaskThrowsReturnsKernelFailed` and `AnAsynchronousRunWhoseTaskThrowsReturnsKernelFailedFromComplete`

A task that throws ends a blocking run and an asynchronous one. `execute()` and `complete()` return
`KernelFailed`, `execute()` with the task's text in the message, and the answer is false after each.

### `Medium_ChainedRunInProgress`

The fixture compiles one kernel over shared USM that the context allocated on device 0 and classifies
the plan; the plan must come out chainable, and a plan left on the scheduler fails the test.
`AChainedRunIsInProgressUntilFinishReturns` and `AnAbandonedChainedRunLeavesNothingShowing` check the
two ends of a `ChainRun`: `finish()`, and the destructor of a run that never finished.
`AScheduledRunInsideAChainedRunLeavesTheChainedRunShowing` runs a scheduled graph inside a chained
run, and `OneChainedRunsEndLeavesTheOtherShowing` ends one `ChainRun` while a second is alive; the
answer stays true until the last one is gone.

## test_runtime_context.cpp

The file covers the profiling queue of `RuntimeContext::devices()`, sections A and F2 of
the g1 node event records design spec in the version history, built as task 3 of
the g1 node event records plan spec in the version history. A profiled node needs the
SYCL `command_submit`, `command_start` and `command_end` times, which only a queue built with
`enable_profiling` exposes. The plain queue that `devices().queue(index)` returns must stay
unchanged for every existing consumer.

### `ProfilingQueueAddsThePropertyAndSharesTheContext`

The plain queue carries no
`enable_profiling` property and the profiling queue carries it once built. The two share one
`sycl::context`, so USM allocated through the plain queue stays legally accessible from the
profiling queue.

### `AProfilingQueueIsBuiltOnlyWhenItIsAskedFor`

One row per action on a fresh context: no request, the plain queue of device 0, and the profiling
queue of device 0. Only the last builds a profiling queue, and only on device 0. The profiling flavour
waits for the first run of a plan that records timing, which only an added `RunObserver` causes.

### `TheProfilingFlavourIsNotThePlainQueue`

The plain queue carries no `enable_profiling` property, so a request for the profiling flavour returns a
second queue and never the plain queue handle.

### `USMFromThePlainQueueIsUsableOnTheProfilingQueue`

A kernel dispatched on the profiling queue correctly dereferences a USM allocation made through the
context of the plain queue. The shared context is usable, not only reported.

`Medium_InvalidNUMAQueue` and `Medium_InvalidNUMAAllocation` cover the queue a NUMA ID routes to and the
USM records of `memory()`: the context degrades without failing on an id no node has. The queue test runs
one row for 999 and one for -5. `Medium_LiveUSMCount` follows the live count through allocate and release,
and through adopt and forget. `Medium_AlignedAllocation` is one row per kind, alignment and byte count;
a shared row skips where the toolchain's aligned shared allocation ignores the alignment, which the
fixture probes before the body runs. `Medium_AlignedRecording` is one row per kind. Discovery and NUMA
mapping alone are tested in `tests/platform/topology/`.

`Large_RuntimeContextNvidia` validates that an NVIDIA GPU is discovered through the CUDA SYCL backend and
mapped to a NUMA node by hwloc. Its fixture skips when there is no CUDA device. The NUMA mapping test
skips only under WSL, which gives hwloc no PCI devices and shows itself in `WSL_DISTRO_NAME`; on Windows it never skips.

The file validates the injectable topology abstraction, `TopologyProvider` and
`NUMADistanceModel`. It exercises multi-NUMA logic deterministically on single-node hardware, which
the real hwloc path cannot do on a machine with one socket.

`RuntimeContext::shutdown()` must stop the installed offload coordinator before the engine stops
the worker pool, as the destructor does. A coordinator returns the nodes it still holds through the
sink inside `stop()`, and a pool that has already joined its workers would never run them. The test
names `RuntimeContext`, an execution header, so it is here and not beside the scheduler tests of
the seam in `tests/engine/scheduler/test_task_scheduler.cpp`.

### `PoolProbingCoordinator`

In its first `stop()` the fake hands a probe node to the sink's `resubmit()` and waits up to ten
seconds for a worker to run it. The probe was never accepted by `try_offload()`; the fake adds the
one reference `resubmit()` consumes. A later `stop()` only counts the call.

### `ShutdownStopsCoordinatorBeforeEngine`

The stop count is 1 after `shutdown()` and the probe has run, which only a live pool can do. The
count is 2 after the context is destroyed: the destructor stops the coordinator again, which
`OffloadCoordinator::stop()` allows.

### Observer wiring

`Medium_RuntimeContextObservers` keeps the one test of the context's wiring. The gate and delivery rules
are tested on `RunObserverRegistry`, which the context owns, in `test_run_observer_registry.cpp`.

| Test | What it proves |
| --- | --- |
| `Medium_RuntimeContextObservers.AnAddedObserverHearsRunsUntilItIsRemoved` | An observer added to the context hears the runs it delivers, the context wants timing while it is added, and removing it ends both. |

## test_scheduler_execution_engine.cpp

CPU work and GPU work dispatched through independent per-device queues must run concurrently and
without a race. These tests assert correctness only. The benchmark suite measures the speedup from
the overlap (`Benchmark_AsyncOverlap`).

### `CPUAndGPUVectorAddsBothProduceTheirSums`

The test submits to the GPU queue and then to the CPU queue without waiting on the GPU, and each half
must produce its sum. The fixture `Large_AsyncExecutionTest` skips on a machine without both devices.

### `AsyncDispatchDoesNotBlock`

A host task that waits on a gate goes to the GPU queue. It cannot finish before the gate opens, so an
event that is not complete when `submit()` returns shows that `submit()` did not wait for it.

The file exercises the device-kernel path. `add_node(TaskWork, reads, writes)` hands the engine a
raw `void(sycl::handler&)` body, so the engine owns the submission, the queue choice and the event
wiring. The graph is a diamond:

```
       init (write A)
        /          \
  doubleA          incA      (both read A -> parallel readers)
  (A->B)            (A->C)
        \          /
       combine (read B,C -> write D)
```

With `A[i] = i` the invariant is `D[i] = 2*A[i] + (A[i] + 1) = 3i + 1`.

The file combines the work-stealing scheduler, mixed host and device tasks, and concurrent device
execution through the full path from `TaskGraph` through `SchedulerExecutionEngine` to `TaskScheduler`. Many
independent chains of a host task, a device kernel and a host task run at once.

Shared USM is bound to a context: a buffer allocated on the queue of one device is not valid on the
queue of another. Each chain therefore pins its allocation and its kernel to the same explicit
device queue, and a chain is either all device or all host.

### `ManyChainsHostDeviceHost`

The test uses one explicit queue per device. It iterates the deduplicated queue set of `context` and not
`sycl::device::get_devices()`. The SYCL list can hold more devices than `context` exposes, for example
the duplicate OpenMP host device of AdaptiveCpp, so indexing `devices().queue()` by the raw SYCL
list overruns it and fails with "Queue index out of bounds".

### `MultiStageChain`

Both buffers are pinned to one device so that they share a context. The `Reduce` task touches `data`
and `sum` together, and the co-location guard requires the buffers of a task to live on one device.
`devices().device_for_numa(numa)` round-robins across the devices of a NUMA node, which would
scatter the two buffers across contexts on a host with several devices. A `USMRequest` with device
index 0 fixes the device.

The file pins the rule `CompiledGraph::rearm` states: a replayed plan reads what the host wrote
between two executions. One kernel reads a word and writes it plus one. The plan is compiled
once and replayed eight times; before each replay the host writes the word, and the output must
follow. `Medium_ReplayHostWrite` has one row for a word in Shared USM and one for a word in Host USM, each
replayed on every device queue of the context.

### `Medium_SchedulerSteps`

One scheduler is armed once and handed three steps of 32 counter tasks. After each step drains,
the counter holds the tasks of every step so far. The step waits on the scheduler's quiescence
latch, not on a clock.

### `Large_TaskSchedulerStress`

Four scale tests moved here from the benchmark file, since they measure nothing: one million
tasks submitted at once, eight producer threads submitting together, 500 rounds of arming and
stopping a scheduler with tasks in flight, and a high-priority task submitted behind 50 low-priority
ones. The first two and the last wait on `wait_quiescent()`, not on a clock. The rounds test asserts
that every round ran all 1000 of its tasks, since `stop()` drains.

## test_chain_plan.cpp

The file tests classify_chain() on `InMemoryPlacementMemory`: a one-device kernel chain, and the
nodes that keep a plan off the chain. No test makes a context or a device.

| Test | What it proves |
| --- | --- |
| `Small_ChainPlan.KernelsOnOneDeviceChainInOrder` | Two kernels on one device's memory chain in order. |
| `Small_ChainPlan.TheChainTakesTheQueueOfTheDeviceThatOwnsTheMemory` | The chain submits to the device that owns the nodes' memory, here device 1. |
| `Small_ChainPlan.KernelsOnTwoDevicesKeepThePlanOff` | Nodes on the memory of two devices keep the plan off the chain. |
| `Small_ChainPlan.AHostNodeKeepsThePlanOff` | A host node keeps the plan off the chain. |
| `Small_ChainPlan.ForeignMemoryKeepsThePlanOff` | A node on memory no device owns keeps the plan off the chain. |
| `Small_ChainPlan.AnEmptyPlanIsNotChainable` | An empty plan is not chainable. |

## test_chain_run.cpp

The file tests ChainRun: each step submits the whole chain in order, and finish() drains it.

| Test | What it proves |
| --- | --- |
| `Medium_ChainRun.EachStepRunsTheWholeChain` | Each step runs its add-one node before its times-three node: two steps give 3, then 12, which no other order gives. |
| `Medium_ChainRun.WaitReturnsAfterTheStep` | wait() returns once the submitted step is done. |

## test_runtime_settings.cpp

The file tests RuntimeSettings: the pool settings it hands the scheduler.

| Test | What it proves |
| --- | --- |
| `Small_RuntimeSettings.UntouchedSettingsAskThePoolForOneWorkerPerCorePinned` | Settings nobody touched ask the pool for one worker per core, from core 0, pinned. |
| `Small_RuntimeSettings.ThePoolSettingsCarryThePoolFields` | The pool settings carry the three fields of the pool, unchanged. |

## test_dropped_dispatches.cpp

The one line a run logs for the dispatches it left out of its timing.

| Test | What it proves |
| --- | --- |
| `Small_DroppedDispatches.LogsNothingWhenNoneWasLost` | A run that lost nothing logs nothing. |
| `Small_DroppedDispatches.LogsOneLineNamingEachCause` | A run that lost dispatches logs one WARN under the execution module. |
| `Small_DroppedDispatches.ALossThatNamesNoNodeLeavesTheNodeOut` | A loss that names no node leaves the first node out of the line. |

## test_run_observer_registry.cpp

The file tests `RunObserverRegistry` on `RecordingObserver` and two fixed gates, one open and one closed. No
test makes a device or a context.

| Test | What it proves |
| --- | --- |
| `Small_RunObserverRegistry.AnEmptyRegistryWantsNoTiming` | A registry with no observer wants no device timing. |
| `Small_RunObserverRegistry.AnUngatedObserverWantsTimingUntilItIsRemoved` | An observer with no gate wants timing until it is removed. |
| `Small_RunObserverRegistry.AnOpenGateWantsTiming` | An observer behind an open gate wants timing. |
| `Small_RunObserverRegistry.AClosedGateWantsNoTiming` | An observer behind a closed gate costs no device timing. |
| `Small_RunObserverRegistry.AnUngatedObserverKeepsTimingOnWhateverAnotherGateSays` | With a closed-gate and an ungated observer added, the registry wants timing. |
| `Small_RunObserverRegistry.ADeliveryReachesAnObserverWhoseGateIsOpenOrAbsent` | A delivery reaches an observer behind an open gate and one with no gate. |
| `Small_RunObserverRegistry.ADeliverySkipsAnObserverWhoseGateIsClosed` | A delivery skips an observer behind a closed gate. |
| `Small_RunObserverRegistry.ARemovedObserverHearsNothingMore` | A removed observer hears no delivery while the others still do. |
| `Small_RunObserverRegistry.RemovingAnUnknownIdLeavesTheObserversAdded` | Removing an id nobody holds removes nothing. |
| `Small_RunObserverRegistry.ClearRemovesEveryObserver` | clear() removes every observer. |

## test_run_failures.cpp

How a run picks the failure it returns when a task and a device queue both failed.

| Test | What it proves |
| --- | --- |
| `Small_RunFailures.NeitherFailureSucceeds` | A run with neither failure succeeds. |
| `Small_RunFailures.ATaskFailureAloneIsReturned` | A task failure alone is returned as it is. |
| `Small_RunFailures.AQueueFailureAloneIsReturned` | The queue's failure alone is returned as it is. |
| `Small_RunFailures.TheTaskFailureWinsOverTheQueuesFailure` | With both failures the task failure is the one returned. |
| `Small_RunFailures.LogsTheQueueErrorADroppedTaskErrorHid` | With both failures the queue's message is logged at WARN with the task failure's. |
