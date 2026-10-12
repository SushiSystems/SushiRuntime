# api tests: graph stepping

The tests of running a recorded graph: `run()`, `run_async()`, `RunHandle` and the report of a run.
They are part of `sr_api_tests`; [the page of the program](../../README.md) says how to run it.

## test_async_step.cpp

The file covers `Graph::run_async()`, `DynamicGraph::run_async()` and the `RunHandle` they return.

The claim under test is that a step can start without blocking the caller, so that a simulation tick
overlaps with work that needs no workers, such as a renderer or an audio extract of the previous
tick. A return value cannot show that a call did not block, so the overlap tests use a rendezvous:
the step's own task waits for a flag that the caller sets after `run_async()` has returned. If
`run_async()` blocked, nobody could set the flag in time and the task would time out. Every wait is
bounded, so a regression reports a failure and does not hang.

### `wait_for_gate`

`wait_for_gate` blocks until the flag is released or the deadline passes, and reports which
happened. It is the body of a host task, so a step can be held open while the test does something on
the calling thread.

### `SubmitReturnsBeforeTheStepFinishes`

`run_async()` must return before the step finishes. The step's host task waits for a flag the caller
sets only afterwards, so the task can see the flag only if the caller kept running.

### `MatchesTheSynchronousResult`

An asynchronous step must be observationally identical to a synchronous one: the same stepped
`State`, driven the same number of times, ends in the same place.

### `StateAdvancesOnWaitNotOnSubmit`

The `State` advance belongs to `wait()`, not to the submit. Until the step has been settled, the
field it just wrote is not yet current. A runtime that advanced on submit would let a reader see the
next field one step early.

### `DestroyingAnUnwaitedHandleStillFinishesTheStep`

A handle that goes out of scope unwaited must still finish the step, because the compiled plan
cannot be destroyed while workers hold pointers into it. The graph must also be runnable again
afterwards.

### `WaitingTwiceThrows`

A second wait on the same handle has no step to wait for. It must throw, because the alternative is
to block on a latch nobody will signal again.

### `ASecondRunIsRejectedWhileAStepIsInFlight`

A second step started while one is in flight, synchronously or asynchronously, must be rejected
before anything touches the plan. A synchronous run re-arms the in-degree of every node, so letting
it proceed would rewrite the scheduling state of the DAG the workers are still walking.

### `TaskExceptionSurfacesOnWaitAndLeavesTheGraphUsable`

A task exception surfaces from `wait()`, and the graph stays usable. One failing step must not turn
into a graph that fails forever.

### `ReadyTracksTheStep`

`ready()` reports whether a wait would return immediately, and it does so without blocking. It must
be false while the step is held open and true once the step has finished.

### `DynamicGraphRunsAsynchronously`

The region-partitioned graph behaves the same way: a step overlaps the caller, and a pending
structural change is committed before the step is submitted.

### `expect_refused_as_invalid_state`, `AnotherGraphIsRefusedWhileAStepIsPending`

A runtime has one engine, so one graph's pending step occupies it. While the step of the first graph
is held open at a gate, `run()` and `run_async()` on a second graph of the same
runtime must each report `InvalidState` and must not run the second graph's task. The first
graph's handle must then complete with a successful report, and the second graph must run once the
step is settled. `expect_refused_as_invalid_state` checks one `Result` for that code.

### `CancelReachesOnlyTheGraphItIsCalledOn`

`cancel()` on a graph with no run in flight must leave another graph's pending step alone: the
step's report says not cancelled. `cancel()` on the graph that owns the pending step must mark its
report cancelled. A `cancel()` sent while nothing runs must not mark the next run.

### `expect_step_ended_by_its_graph`, `AGraphDestroyedWithAPendingStepEndsTheStep`, `ADynamicGraphDestroyedWithAPendingStepEndsTheStep`

A handle may outlive its graph. The graph is destroyed in an inner scope while its step is pending,
with the gate released just before, and the handle is destroyed at the end of the test. The buffer
must hold the task's write when the scope closes, which shows that the destructor waited for the
step before the plan died. `expect_step_ended_by_its_graph` then expects `pending()` false,
`ready()` true and `InvalidState` from `wait()`, none of which may read the dead graph. The
`Graph` test also runs a new graph on the runtime, which a step left behind would refuse.

### `ARegionDroppedWhileAStepIsPendingFinishesTheStep`

Two regions each hold one host task that waits at the gate and then adds one to its own buffer. The
test starts a step, drops region 1 while both tasks wait, releases the gate and settles the handle.
The step must succeed with both buffers at one: the dropped region's task finished its write, and
settling walked that region's per-step hooks. The test guards against a `drop()` that destroys the region at once: the
settle then reads a freed recorder, which AddressSanitizer reports. A following `run()` must leave
the dropped region's buffer at one, raise the other to two and bring `compose_count()` to two.

### `ARegionDroppedAndRecordedAgainWhileAStepIsPendingRunsItsNewWork`

Region 7 is dropped while its step waits at the gate, and `region(7)` then records a task on a
second buffer. The pending step must run the first recording only. The next run must run the second
recording only, which shows that the map holds the new region while the old one is kept for the
step.

## test_run_driver.cpp

The file covers the run entries `StepDriver` gives `Graph` and `DynamicGraph`: that both types
expose the same ones, which `ErrorCode` each kind of failure carries, and what a report counts.

### `RunEntries`

`RunEntries<G>` is the list of run entries as `static_assert`s: the eight forms of `run()`,
`run_for()` and `run_until()`, then `run_async()`, `cancel()`, `set_profiling()` and
`is_profiling_requested()`, each with its return type, and `noexcept` where the contract has it.
The template is instantiated for `Graph` and for `DynamicGraph`, so an entry missing from
either, or declared differently, fails the compile.

### `expect_kernel_failed_everywhere`

`expect_kernel_failed_everywhere` runs a graph whose host task throws `std::runtime_error` through
`run()`, `run(report)` and `run_async()` with `wait()`, and expects `ErrorCode::KernelFailed` with
the task's text from each.

### `GraphReportsAThrowingHostTaskAsKernelFailed`, `DynamicGraphReportsAThrowingHostTaskAsKernelFailed`

A host task that throws is a failed step, so both graph types report `KernelFailed` from the
synchronous and the asynchronous path. The tests guard against `CompileFailed` from `run()` and against the
task's own exception leaving `wait()`.

### `AThrowingBuildIsCompileFailed`

No code of the caller runs when the plan is built except the copy of each recorded callable into
its node. The second task is a `Testing::RefusingFill` whose copy throws, so the build throws. It
must yield `CompileFailed` from `run()` and from `run_async()`, which shows that the code
follows the phase and not the exception type.

### `EveryTryEntryRunsOnBothGraphs`

`expect_every_run_entry_runs` calls the eight run entries once each on a graph whose single host
task counts its runs. The count must be twelve on a `Graph` and on a `DynamicGraph`: one, two, one
and two steps from the `run` forms, one each from the two `run_for` forms with a zero
budget, and two each from the two `run_until` forms.

### `ARunWhileAStepIsPendingIsInvalidState`

While a step is held open at a gate, `run()` and `run_async()` must return `InvalidState`,
and so must a second `wait()` on the handle once the step has been waited for.

### `SetProfilingChainsAndReadsBack`

`set_profiling()` returns the graph it was called on and `is_profiling_requested()` reads the
request back, on both graph types.

### `SettingTheSameProfilingValueRebuildsNothing`

`set_profiling(true)` followed by a run, twice, must leave `Graph::compile_count()` and
`DynamicGraph::compose_count()` at one, and `set_profiling(false)` followed by a run must raise each
to two. The test guards against a `PlanCache::set_profiling` that marks the plan dirty on every call,
under which the `Graph` count reads two after the second run.

### `AProfilingToggleOnADynamicGraphComposesOnceAndTimesItsNodes`

A `DynamicGraph` with one kernel runs four times: with no request, after `set_profiling(true)`,
again with nothing changed, and after `set_profiling(false)`. `RunReport::node_timings` must be
empty, filled, filled and empty, and `compose_count()` must read one, two, two and three, so the
request the composer keeps reaches the plan at the commit after each change and at no other.

### `MovingAGraphWithAPendingStepEndsTheStep`

Moving a graph while its step is pending breaks a precondition. A debug build asserts, so the test
is skipped there. In a release build the move must wait for the step, the handle must then report
`pending()` false and `ready()` true, its `wait()` must return `InvalidState`, and the
destination must run. The gate is released before the move, so the move returns as soon as the task
has run.

### `AcceptingCoordinator`, `OffloadedCountIsOfThisRun`

`AcceptingCoordinator` marks every node of a plan offloadable, takes each one it is offered and
returns it as finished from a thread of its own, the way a worker's answer arrives. A graph of one
host task reports zero offloaded tasks before the coordinator is installed. A graph recorded after
the install reports one for a one-step run, two for a two-step run and one for an asynchronous
step; a report that copied the scheduler's lifetime count would say one, three and four. The host
task ran once in all, in the run without a coordinator.

### `CountedStep`, `counted_step_runs_chained`

`CountedStep` owns a graph of three rules: a `State` increment, a kernel that writes a count into a
`Residency::Host` slot, and a launch sized from that slot with a capacity of eight. A count of nine
makes every step record a device fault, which the driver sees when it checks faults. The plan is
kernels and a device-sized launch only, so it takes the chain; `counted_step_runs_chained` runs a
clean one and reads the report's layer, and the two tests below are skipped when it is not the
chain, because on the scheduler they would pass without the behaviour they test.

### `AFailedChainedRunOfOneStepAdvancesNoState`, `AFailedChainedRunOfSeveralStepsAdvancesAllButTheLast`

A chained `run()` whose step faults must return an error and leave the `State` on the field it
started on, with the starting value five in it. The test guards against a chain that advances before it checks, under which the
`State` shows six. A chained `run(3)` whose steps fault must leave seven: the two steps before
the last have advanced, the last has not.

### `expect_run_for_lasts_until_cancelled`, `ABudgetPastTheClocksRangeRunsUntilCancelled`

`run_for` has no step limit, so the test ends the run from inside: the host task calls `cancel()` on
its own graph at the third step. With a budget of `hours::max()` or `nanoseconds::max()` the report
must say cancelled after exactly three steps. A deadline computed as `now + budget` overflowed for
these budgets and landed in the past, which ended the run after one step. The task throws after a
thousand steps, so a `cancel()` that reached nothing fails the test and does not hang it.

### `AStepHookFeedsEveryReplayOfOnePlan`, `AStepHookFeedsEveryReplayOfADynamicGraph`

A hook writes the count of finished steps and that count plus 100 into two host words. One kernel
over eight elements reads both and writes the second word into the element the first one names.
After `run(8)` element `i` holds `100 + i`, which needs the hook of step `i` to have run after
the kernel of step `i - 1` read its words, and the graph compiled or composed once. On a `Graph` a
second hook records the word the first one wrote, which fixes the order the two run in, and a
further `run()` shows the count starting at zero again with no recompile.

### `AStepHookMovesWithItsGraph`

A graph with one hook is moved. The destination calls the hook when it runs; the source records a
task of its own, runs, and calls nothing.

### `AStepHookLargerThanTheInlineStorageRuns`

The hook captures 128 bytes by value, which is more than the 64 a hook is stored in without an
allocation, and reads the last of them back when it is called.

### `AnErrorOnlyTheAsynchronousHandlerSeesFailsTheRunOnBothLayers`

A task submits a `host_task` that throws, which SYCL reports through the asynchronous handler of
the queue and through nothing else. The test runs the graph with and without `force_scheduler` and
expects `KernelFailed` from `run()` on both layers, then expects `Runtime::wait()` to find no error
left in a queue. It is compiled for DPC++ only, since AdaptiveCpp has no `host_task`. The chain
layer waits with `wait_and_throw()`; the scheduler layer polls events and waits with `wait()`, and
`SchedulerExecutionEngine::complete()` then makes each queue raise what its handler holds.

## test_run_report_reuse.cpp

The file covers the overloads that take a caller-owned `RunReport`: `run(RunReport&)`,
`run(RunReport&)` and `RunHandle::wait(RunReport&)`.

The tests pin down two properties that pull against each other. The overloads exist so that a frame
loop keeps one report alive and the per-run vectors stop being reallocated, so the report must not
be rebuilt from scratch. A report carried across ticks must also never let the numbers of the
previous run survive into the current one, and not rebuilding invites exactly that. The resolution
is to reset and then refill: the capacity stays and the values do not. Both halves are asserted,
also on the failure paths, where stale numbers would mislead most: a caller that ignored the
returned error would otherwise read a report that looks successful.

### `record_write`

`record_write` zeroes `buf` and records the one task every test in the file runs: element i gets
i+1. It takes both handles by reference and does not return them, because a `Buffer` must not be
moved once a graph has recorded it; the graph would keep pointing at the old address.

### `ReuseReportsWhatTheValueFormReports`

The reuse overload must report what the value-returning one reports. If the two drivers ever
diverged, every number a consumer reads from the cheap path would silently become a different
measurement from the documented one.

### `SteadyStateRunsDoNotReallocateTheReport`

The overload exists so that the per-run vectors are refilled in place. Comparing the address of the
row before and after proves that no reallocation happened. A capacity check alone would pass even if
the vector were rebuilt into a new block of the same size.

### `EachRunOverwritesTheLastOne`

Reuse must not accumulate. A short run that follows a long one has to read as the short run, because
the count belongs to the run, not to the report.

### `ARejectedRunLeavesNoStaleNumbers`

A rejected run must clear the report it was handed. The dangerous case is a caller that checks the
returned error only sometimes. Reading a stale `is_successful` of true from a run that never
happened is worse than reading zeroes, so the failure path resets the report and marks it
unsuccessful.

### `TryRunReturnsTheCallersReport`

`run(report)` returns a pointer to the report it was given, so a caller can keep using the result
of the call without holding both.

### `AsyncWaitFillsTheCallersReport`

The asynchronous path settles into a caller-owned report too. The overlapped frame loop is the one
that most wants the overload.

## test_node_event_records.cpp

The file covers per-dispatch node event records, the clock pairing and steady-state capture. The
design is section C and items 1, 2, 3, 4 and 6 of section E in
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`, built as task 5 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-plan.md`. The item numbers in the
subsections refer to section E of the design.

### `NodeEventRecordsTestAccess`

`NodeEventRecordsTestAccess` is a friend used only by tests to reach the compiled plan of a
`Graph`. It mirrors
`RegistrationIndexTestAccess` in `test_registration_index.cpp`.

### `RequestedGraphRecordsOnePerDispatch`

Item 2: three independent kernel nodes and `run(5)` give fifteen records, invocations 0 to 4 per
node, and `submit <= start <= end` for every record.

### Steady-state capture

Item 3 is tested in `test_steady_state_allocation.cpp`, which is built into the
`sr_allocation_tests` executable and so runs in its own process. A replacement `operator new` in
`test_node_event_records.cpp` would see only the allocations of its own executable and never those
of the shared library. The section on `test_steady_state_allocation.cpp` in
`tests/allocation/README.md` explains why that test needs its own binary.

### `NodeIndexIsRegistrationOrder`

Item 4: `node_index` follows the registration order, not the compiled order, which is reversed. The
test cross-checks it against the plan's own `registration_index`.

A strict dependency chain, in which B reads what A wrote and C reads what B wrote, forces the
execution order A, B, C. That gives an observation of identity which does not depend on trusting the
`registration_index` read from the `Node` object. If the registration order of the plan and the
dispatch order of the run ever disagreed, sorting by earliest submit would catch it, even though
both would still read as {0, 1, 2} on their own.

### `HostNodeLandsOnEventClock`

Item 6: the converted record of a native host node lands inside the event window of the run, within
the `uncertainty_ns` of the pairing.

The host node reads what the kernel writes. That chains it strictly behind the kernel's completion;
the two are not independent roots. The window of the host record therefore sits inside the
event-clock window of the run, not merely near it.

The converted `submit_ns` and `start_ns` of a host record are identical, because the scheduler
stamps the same raw steady time into both. Those of a device record rarely are. The test tells the
two kinds apart by that difference and not by a stored flag, because `NodeEventRecord` stays field
for field identical to the record the engine fills, as the global constraints of the design require.

The chain guarantees that the host node is dispatched no earlier than the kernel's completion. The
tolerance covers only the measurement uncertainty of the pairing, not scheduling jitter between
unrelated roots.
