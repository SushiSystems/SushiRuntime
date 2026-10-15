# execution tests

The tests of the `execution` module, mirroring `modules/engine/execution/`.
`tests/engine/execution/CMakeLists.txt` builds them into `sr_execution_tests` and
`sr_execution_benchmarks`, built under `SR_BUILD_BENCHMARKS`. `sr test` runs them with the rest;
`sr run sr_execution_tests -- --gtest_filter='<pattern>'` runs one program alone.

A benchmark prints its figures and asserts correctness and forward progress; it asserts a
speed only where its section says so.

Three design documents are cited by path in the sections that came from the integration tests:

- `docs/design/GRAPH_REPLAY.md`, the replay layers and their phases.
- `docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md`, the work packages WP-0 to WP-7 and their
  numbered sections.
- `docs/design/ENGINE_BACKBONE_REFACTOR.md`, the backbone items BB-0 to BB-8.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## benchmark_runtime_context.cpp

This is the benchmark behind section E, item 7 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`. That item decided
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

The two thresholds are fractions of the serial time. An overlap time under 0.75 of the serial
time is healthy concurrency: the bar leaves slack for dispatch overhead and contention, and a
run that serializes, at about 1.0x, still fails it. An overlap time at or above 0.95 of the
serial time means no overlap at all and fails on every host. That second check also catches
`CUDA_LAUNCH_BLOCKING=1`, under which every launch runs synchronously.

A run that lands between the two bars is skipped on WSL2 and nowhere else. The CUDA backend
under WSL2 adds dispatch latency and exposes no PCI topology, which inflates the overlap wall
time enough to miss the 0.75 bar on a Pascal card even while the devices overlap. The result
is inconclusive there, and bare-metal CI still enforces the bar. `running_under_wsl` exists to
relax the check on that host, not to remove it.

## benchmark_scheduler_execution_engine.cpp

The file closes two gaps: how `TaskGraph::compile()` scales with graph size, and whether
overlapping host-to-device and device-to-host transfers with compute helps. Both tests do
identical work on each side and verify the result. They print figures and assert only that the
run made progress, because real overlap needs a discrete GPU with copy engines.

`min_compile_us` uses a rolling set of 16 buffers so that consecutive tasks form real
dependencies, which is what exercises the dependency tracker.

The benchmark measures dispatch overhead per task. Both sides do the same work, one relaxed
atomic increment, so the only difference is the path: inline in a tight loop, which is the
floor, or wrapped in a task and run through the work-stealing scheduler. The delta is the
amortized cost of submit, steal and dispatch. The test reports it and asserts correctness and
forward progress, not speed.

A real-hardware benchmark for NUMA-aware hierarchical stealing. It drives the scheduler with a
large batch of host tasks and reports throughput and the split between local and remote
steals. Locality exists only across real NUMA nodes, so the test skips on a single-node
machine or under WSL; `Medium_NumaStealTest` holds the logic check.

Locality is asserted only above 1000 steals. Below that the ratio means little, and a
perfectly balanced drain can legitimately produce few steals.

The benchmark compares NUMA-aware with NUMA-blind stealing on the same hardware. One workload
runs once on the real, flat topology and once on a synthetic two-node topology, and the test
reports both throughputs and the aware side's steal split. On a UMA machine, single socket or
WSL, there is no real speedup to claim, so it asserts correctness and that the aware path
keeps steals node-local.

The steal split is gathered while the workload is timed, so it reflects locality only when the
cores sit on separate NUMA nodes. On a single-socket machine the two synthetic nodes share one
set of cores, and OpenMP and runtime worker threads contending for them drag the local
fraction below the 0.5 bar with every toolchain; 0.46 to 0.48 was observed.
`Medium_NumaStealTest` verifies the locality ordering deterministically, so the assertion made
during timing fires only on real multi-socket hardware.

The gate benchmark for the deque-steal fast path, audit steal-1, and the global quiescence
counter, audit sched-1. It profiles them in the substrate's characteristic shape: many small
steps, not one long sustained stream. Each step submits a modest batch of independent tasks
and drains to quiescence. The tail of every step is where workers spin idle, stealing from
empty peers, and steal-1 removes a `std::memory_order_seq_cst` fence from each empty attempt. It is also where
`inflight_` and the shared counters are hammered, which is the contention sched-1 targets.

The benchmark reports drain latency per step and nanoseconds per task. That isolates the fixed
overhead per step that dominates the substrate, and complements the single sustained stream of
`Performance_SchedulerDispatchBench`. Attributing steal-1's fence removal and sched-1's line
contention in full needs hardware counters, `perf stat` and `perf c2c`. This benchmark is the
portable guard on throughput and latency, and the harness those counters attach to. It asserts
correctness and forward progress only.

## test_execution_engine.cpp

The file covers `ExecutionEngine::run_in_progress()`: false on an engine that ran nothing, true
while a run is in flight, false once the run has been collected, whether it succeeded or threw.
No test reads a clock. Where a run must still be in flight at the check, its host task waits on a
`std::promise` the test fulfils afterwards.

### `MinimalEngine`

An engine that implements only the two pure members. `TheInterfaceDefaultReportsNoRun` reads the
interface's default through it.

### `ABlockingRunIsInProgressUntilExecuteReturns`

The run's one host task asks the engine and stores the answer, which must be true; after
`execute()` returns the answer is false.

### `AnAsynchronousRunIsInProgressUntilCompleteReturns`

The answer is true between `submit()` and `complete()` and false after. The task cannot finish
before the first check, because it waits on the gate.

### `ARunWhoseTaskThrowsReturnsKernelFailedAndLeavesNoRunInProgress`

A task that throws ends a blocking run and an asynchronous one; `execute()` and `complete()`
return `KernelFailed`, with the task's text in the message, and the answer is false after each.

### `compile_chain_candidate` and the chained tests

`compile_chain_candidate` compiles one kernel over shared USM that the context allocated on device
0 and classifies the plan. Where `classify_chain()` leaves it on the scheduler the two chained
tests are skipped. `AChainedRunIsInProgressUntilFinishReturnsOrItIsDestroyed` checks both ends of a
`ChainRun`: `finish()`, and the destructor of a run that never finished.
`AScheduledRunInsideAChainedRunLeavesTheChainedRunShowing` runs a scheduled graph and ends a second
`ChainRun` while the first is alive; the answer stays true until the last one is gone.

## test_runtime_context.cpp

The file covers the profiling queue of `RuntimeContext::devices()`, sections A and F2 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`, built as task 3 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-plan.md`. A profiled node needs the
SYCL `command_submit`, `command_start` and `command_end` times, which only a queue built with
`enable_profiling` exposes. The plain queue that `devices().queue(index)` returns must stay
unchanged for every existing consumer.

### `ProfilingQueueAddsThePropertyAndSharesTheContext`

With `RuntimeSettings::profiling` off, which is the default, the plain queue carries no
`enable_profiling` property and the profiling queue carries it once built. The two share one
`sycl::context`, so USM allocated through the plain queue stays legally accessible from the
profiling queue.

### `NoProfilingQueueExistsBeforeTheFirstRequest`

Nothing is built before the first request. `RuntimeSettings::profiling` is off, so
the `RuntimeContext` constructor must not have built the profiling queue of any device.

### `SettingsProfilingBuildsEveryDeviceEagerly`

`RuntimeSettings::profiling` asks the `RuntimeContext` constructor to build the profiling queue of
every device eagerly, before any `run()` could have requested one.

### `SettingsProfilingReturnsThePlainQueueItself`

With `RuntimeSettings::profiling` on, the plain queue already carries `enable_profiling`, so
a request for the profiling flavour must return that same queue handle and not a second, duplicate
one.

### `USMFromThePlainQueueIsUsableOnTheProfilingQueue`

A kernel dispatched on the profiling queue correctly dereferences a USM allocation made through the
context of the plain queue. The shared context is usable, not only reported.

`Medium_RuntimeContext` covers the queue a NUMA ID routes to and the USM records of `memory()`. The
invalid-NUMA fallbacks check that the context degrades without failing on bad input. Discovery and
NUMA mapping alone are tested in `tests/platform/topology/`.

The file validates that an NVIDIA GPU is discovered through the CUDA SYCL
backend and mapped to a NUMA node by hwloc. The tests skip when there is no CUDA device.

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

## test_scheduler_execution_engine.cpp

CPU work and GPU work dispatched through independent per-device queues must run concurrently and
without a race. These tests assert correctness only. The benchmark suite measures the speedup from
the overlap (`AsyncOverlapBench`).

### `CPUGPUConcurrentVectorAdd`

The test submits to the GPU queue and then to the CPU queue without waiting on the GPU. Both submits
return immediately, and the two devices proceed in parallel.

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
follow. One case holds the word in Shared USM and one in Host USM, each on every device queue of
the context.

## test_chain_plan.cpp

The file tests classify_chain(): a one-device kernel chain, and the nodes that keep a plan off the
chain.

| Test | What it proves |
| --- | --- |
| `KernelsOnOneDeviceChainInOrder` | Returns work that adds one to an integer on the device. */ TaskWork add_one(int* value) { return [value](sycl::handler& handler) { handler.single_task([value] { *value += 1; }); }; } /** @brief Allocates one shared integer of the context on device 0, set to 0. */ int* shared_integer(RuntimeContext& context) { Internal::Memory::USMRequest request; request.kind = Internal::Memory::USMKind::Shared; request.bytes = sizeof(int); int* const value = static_cast<int*>(context.memory().allocate(request)); if (value != nullptr) *value = 0; return value; } } /** @brief Tests that two kernels on one device's memory chain in order. |
| `AHostNodeOrForeignMemoryKeepsThePlanOff` | A host node, and a node on memory no device owns, keep the plan off the chain. |

## test_chain_run.cpp

The file tests ChainRun: each step submits the whole chain in order, and finish() drains it.

| Test | What it proves |
| --- | --- |
| `EachStepRunsTheWholeChain` | Returns work that adds one to an integer on the device. */ TaskWork add_one(int* value) { return [value](sycl::handler& handler) { handler.single_task([value] { *value += 1; }); }; } /** @brief Allocates one shared integer of the context on device 0, set to 0. */ int* shared_integer(RuntimeContext& context) { Internal::Memory::USMRequest request; request.kind = Internal::Memory::USMKind::Shared; request.bytes = sizeof(int); int* const value = static_cast<int*>(context.memory().allocate(request)); if (value != nullptr) *value = 0; return value; } } /** @brief Tests that two steps of a two-node chain add four, in submission order. |
| `WaitReturnsAfterTheStep` | Wait() returns once the submitted step is done. |

## test_runtime_settings.cpp

The file tests RuntimeSettings: the defaults and the pool settings it hands the scheduler.

| Test | What it proves |
| --- | --- |
| `DefaultsAskForNothingExtra` | Checks the defaults: no profiling, one worker per core, pinned, no rebalancer. |
| `ThePoolSettingsCarryThePoolFields` | The pool settings carry the three fields of the pool, unchanged. |
