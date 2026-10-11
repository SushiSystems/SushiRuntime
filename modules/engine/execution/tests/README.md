# execution tests

The tests of the `execution` module, built into one program, `sr_execution_tests`, which
`modules/engine/execution/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_execution_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_async_execution.cpp

CPU work and GPU work dispatched through independent per-device queues must run concurrently and
without a race. These tests assert correctness only. The benchmark suite measures the speedup from
the overlap (`AsyncOverlapBench`).

### `CPUGPUConcurrentVectorAdd`

The test submits to the GPU queue and then to the CPU queue without waiting on the GPU. Both submits
return immediately, and the two devices proceed in parallel.

## test_device_pipeline.cpp

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

## test_heterogeneous_pipeline.cpp

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

## test_profiling_queue.cpp

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

## test_replay_host_write.cpp

The file pins the rule `CompiledGraph::rearm` states: a replayed plan reads what the host wrote
between two executions. One kernel reads a word and writes it plus one. The plan is compiled
once and replayed eight times; before each replay the host writes the word, and the output must
follow. One case holds the word in Shared USM and one in Host USM, each on every device queue of
the context.

## test_run_in_progress.cpp

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

### `ARunWhoseTaskThrowsLeavesNoRunInProgress`

A task that throws ends a blocking run and an asynchronous one; `execute()` and `complete()`
rethrow, and the answer is false after each.

### `compile_chain_candidate` and the chained tests

`compile_chain_candidate` compiles one kernel over shared USM that the context allocated on device
0 and classifies the plan. Where `classify_chain()` leaves it on the scheduler the two chained
tests are skipped. `AChainedRunIsInProgressUntilFinishReturnsOrItIsDestroyed` checks both ends of a
`ChainRun`: `finish()`, and the destructor of a run that never finished.
`AScheduledRunInsideAChainedRunLeavesTheChainedRunShowing` runs a scheduled graph and ends a second
`ChainRun` while the first is alive; the answer stays true until the last one is gone.

## test_sycl_execution.cpp

### `MultiStageChain`

Both buffers are pinned to one device so that they share a context. The `Reduce` task touches `data`
and `sum` together, and the co-location guard requires the buffers of a task to live on one device.
`devices().device_for_numa(numa)` round-robins across the devices of a NUMA node, which would
scatter the two buffers across contexts on a host with several devices. A `USMRequest` with device
index 0 fixes the device.
