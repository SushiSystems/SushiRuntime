# api tests: graph/plumbing

The tests of the builder surface, the region composer and the step driver. They are part of `sr_surface_tests`;
[the page of the module's tests](../../README.md) says how to run them. Every test runs on the program's runtime
through `TestSupport::RuntimeTest`; the rows that need a runtime forced onto the scheduler make it in their fixture.

## test_builder_surface.cpp

The file covers the add() overloads of BuilderSurface that v0.4.0 adds or reorders: launch first,
then access, then callable. `seeded` and `seeded_state` fill a handle with its index plus `K_SEED`,
so a kernel that ran over the wrong elements or received the wrong pointer shows as a wrong word.
`refusal_of` clears a recorder, runs a builder call and returns the message of the refusal the call
left, then clears the recorder again.

### `CountLaunchWithCapturesPassesPointers`

`add(count, In(a), Out(b), kernel)` hands the kernel the two pointers after a `std::size_t` index. Up
to v0.3 the capture form took an Extent only.

### `NdRangeOriginShiftsTheIDTheKernelReceives`

An NdRange rebased with `at(origin)` and a kernel written as `kernel(nd_item, id, pointer)` zeroes
the elements from the origin on and leaves the rest, so the id the kernel receives carries the
origin. The origin and the count of the NdRange are held by `launch/test_nd_range.cpp`.

### `StateRuleOverACountUpdatesAPrefix`

`add(count, state, kernel)` updates the first `count` elements of the State.

### `ALaunchPastItsStateIsRefusedAtAdd`

An Extent whose origin plus size passes the State's size is refused with `InvalidGraph` by add().
The message names the rule, the extent with its origin and the State's size, and the rule is not
recorded.

### Coupled rules

| Test | What it proves |
| --- | --- |
| `AWholeFieldCoupledRuleReadsTheStateItNames` | `add_coupled(Coupled(out, in), kernel)` hands the kernel the current field of the State read after the two fields of the State written. |
| `ALaunchShapedCoupledRuleReadsItsPrefixAndTheSecondState` | `add_coupled(count, Coupled(out, in), kernel)` does the same over the first `count` elements. |
| `ACoupledInputSmallerThanTheOutputIsRefusedAtAdd` | A coupled rule over the whole field whose input State is shorter than its output is refused with `InvalidGraph` by add(). |

### `DynamicTakesItsCapacityBeforeItsAccess`

`add_dynamic(sized(...), capacity, Reads, Writes, kernel)` runs over the live count only.

### Untracked kernels and placement

| Test | What it proves |
| --- | --- |
| `UntrackedKernelRunsOnTheDeviceOfTheBufferNamed` | A buffer on the runtime's last device is written by `add_untracked(n, On(buffer), kernel)`. On a machine with one device that device is 0 and the test proves only that the form records and runs; with two it proves the routing. |
| `UntrackedKernelRunsOnTheDeviceNamed` | The same for a raw kernel placed with `On(DeviceIndex)`. |
| `ALaunchPlacementOnADeviceTheRuntimeLacksStops` | `add_untracked(launch, On(DeviceIndex{device_count}), kernel)` stops the process. The message names the call the user wrote, the index and the device count. |
| `AHandlerPlacementOnADeviceTheRuntimeLacksStops` | The raw form stops the same way. Both tests are in the suite `Medium_BuilderSurfaceDeathTest`, since each starts a child process. |
| `PlacementWatchesTheHandleItNames` | A graph whose `On(buffer)` handle was destroyed before the run is refused with `DanglingResource`. |

### `ExtendRecordsOneNamedRule`

`extend(extension)` calls `extension` once with the Recorder; the two kernels it records are one rule of two nodes.
`AnExtensionsKernelsRunAndTheExtensionIsNotReplayed` adds that the kernels add their results and a run does not call the extension again.

### `ARefusalNamesTheCallTheUserWrote`

One row per family, through the public add(): a State rule past its State, a State rule over a State
that owns no memory, a Coupled rule with a short input and a Coupled launch past its State. Each
message begins with the spelling of the call the user wrote and `rule 0`, and no rule is recorded.

### The kept refusal

| Test | What it proves |
| --- | --- |
| `TheFirstRefusalIsKept` | Three adds are made, the second with a launch past its State. `refusal()` then holds an `InvalidGraph` error whose message begins with `add_rule(launch, state, kernel), rule 1: `, and the graph holds the two valid rules. A later refused `add_rule` over an empty State does not replace it. |
| `ARunReturnsTheRefusalAndRunsNothingUntilCleared` | Two runs in a row return that error and leave the target as it was. After `recorder().clear()` the refusal is gone, and a rule added then runs. |

## test_region_composer.cpp

The file covers the cross-region ordering the composer makes from the pins of
`region_boundary`, whose own tests are in `tests/engine/region_boundary/`. The plan edges are not
public, so each case records host tasks on a `DynamicGraph` over a buffer of 20 elements, runs one
step and asserts the values the ordering makes right. The fixtures turn the rebalancer off.

### `AnEarlierWriterOrdersAReaderItsPredecessorHalfCovers`

Region 1 holds `w`, which writes elements `[15,20)`. Region 2 holds `p`, which writes `[0,10)`, and
`q`, which reads `[5,20)` and sums it. The sum is 40, which it can be only when `q` ran after `w`.

### `AReaderItsPredecessorCoversStillReadsTheNewestValues`

`p` writes the whole buffer here, and `q` is ordered behind `w` through `p`. The sum is 50.

### `AnEarlierReaderOrdersAWriterBehindAReadingPredecessor`

Region 1 holds `r`, which reads `[0,10)`. Region 2 holds `p`, which reads the same elements, and
`q`, which overwrites them. Both readers see the value the buffer had before `q` ran.

The file covers what a `DynamicGraph` keeps when a commit of its regions throws.

### `AThrowingRegionBuildComposesNothingAndARetryKeepsEveryRegionsValues`

Regions 0, 1 and 2 share one buffer: `a` writes it, `b` and `c` read it. After one run region 1 is
dropped and region 3 is added with a `Testing::RefusingFill`, a host task whose copy throws while
its flag is set and which writes a buffer of its own. The run returns `CompileFailed` and
`compose_count()` has not moved. The flag is then cleared, the next run succeeds and composes once,
and `c` holds what `a` wrote in the second step.

A second mid-commit failure, an allocation that fails after the first region is un-wired, has no
test: the composer offers no seam that makes `std::set::insert` throw.

### `Regression_ARuleChangeDuringACommitComposesOnTheNextRun`

Region 0 holds a `Testing::HookedCopyTask` whose hook sets an empty rule set. The commit of the
first run copies the task after it has read the placement record, so the hook's change lands
inside the commit. The next run composes once more, and the run after it composes nothing. Code
that reads the rules at the start of a commit and the generation at its end tags the old rules with
the new generation and never composes again.

## test_plan_cache.cpp

The file tests PlanCache through Graph: a plan is compiled once and again only when it changes.

| Test | What it proves |
| --- | --- |
| `CompilesOnlyWhenTheGraphChanges` | Runs reuse the plan, and a new node compiles it again. That an observer compiles nothing is held for both graph types by `AnObserverRebuildsNothing` in `test_step_driver.cpp`. |
| `ARuleChangeDuringACompileRecompilesOnTheNextRun` | A `Testing::HookedCopyTask` sets an empty rule set while `PlanCache::build()` copies it, after the build read the placement record. The next run compiles once more and the run after it does not. |

Three promises have no test here: that a coordinator is prepared with the rules the compile read, that a
dynamic graph hands the nodes of each rebuilt region to the coordinator, and that installing or removing
a coordinator makes the plan stale. A coordinator is installed through `Internal::ContextAccess`, which a
test of the api module does not reach.

## test_step_driver.cpp

The file covers the run entries `StepDriver` gives `Graph` and `DynamicGraph`: that both types
expose the same ones, which `ErrorCode` each kind of failure carries, and what a report counts. The
rows of `Medium_RunDriverGraphKinds` run one body on a `Graph` and on a `DynamicGraph`; `record_host`,
`record_add`, `plan_builds` and `on_graph_of` give the body one spelling for both.

### `RunEntries`

`RunEntries<G>` is the list of run entries as `static_assert`s: the eight forms of `run()`,
`run_for()` and `run_until()`, then `run_async()` and `cancel()`, each with its return type, and
`noexcept` where the contract has it.
The template is instantiated for `Graph` and for `DynamicGraph`, so an entry missing from
either, or declared differently, fails the compile.

### `expect_kernel_failed_everywhere`, `AThrowingHostTaskIsKernelFailedFromEveryEntry`

`expect_kernel_failed_everywhere` runs a graph whose host task throws `std::runtime_error` through
`run()`, `run(report)` and `run_async()` with `wait()`, and expects `ErrorCode::KernelFailed` with
the task's text from each. A host task that throws is a failed step, so both rows report
`KernelFailed` from the synchronous and the asynchronous path. The test guards against `CompileFailed` from
`run()` and against the task's own exception leaving `wait()`.

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

While a step is held open at a gate, `run()` and `run_async()` must return `InvalidState`. That a
second `wait()` on a settled handle is refused is held by `ASecondWaitIsRefusedAsInvalidState` in
`../test_run_handle.cpp`.

### Run observers

| Test | What it proves |
| --- | --- |
| `AddRunObserverReturnsAHandleThatRemovesIt` | `Runtime::add_run_observer()` returns a handle that stands for the observer. A graph run while it is added reaches the observer once, and a run after `reset()` does not. |
| `AnObserverRebuildsNothing` | Two rows, `Graph` and `DynamicGraph`. Adding the first observer, a second, removing one and removing the last must leave the plan built once: device timing is chosen per run, not per plan. Each observer receives every run while it is added. |
| `AnObserverOnADynamicGraphTimesItsNodes` | A `DynamicGraph` with one kernel runs four times: with no observer, after one is added, again with nothing changed, and after it is removed. The plan built before the observer must give records on its first run after the add, and the observer must see no run after the removal. |

### Moving a graph with a pending step

Moving a graph while its step is pending breaks a precondition. A debug build asserts, so the two tests are
not built there. In a release build the move must wait for the step, the handle must then report
`pending()` false and `ready()` true, and its `wait()` must return `InvalidState`
(`MovingAGraphWithAPendingStepEndsTheStep`); the destination must run
(`TheDestinationOfAMoveRunsAfterAPendingStepEnded`). The gate is released before the move, so the move returns
as soon as the task has run.

A coordinator that takes nodes offsite, and a report that counts the dispatches it took in its own run, are
reached only through `Internal::ContextAccess`; that promise has no test here.

### `CountedStep`

`CountedStep` owns a graph of three rules: a `State` increment, a kernel that writes a count into a
`Residency::Host` slot, and a launch sized from that slot with a capacity of eight. A count of nine
makes every step record a device fault, which the driver sees when it checks faults.

| Test | What it proves |
| --- | --- |
| `AFailedRunOfOneStepAdvancesNoState` | A `run()` whose step faults must return an error and leave the `State` on the field it started on, with the starting value five in it. The test guards against a run that advances before it checks, under which the `State` shows six. |
| `AFailedRunOfSeveralStepsNeverAdvancesTheFailedStep` | A `run(3)` whose steps fault must leave the `State` between five and seven. How many steps before the failed one have advanced depends on the layer, which is an inside detail, so the test holds only that the failed step never advanced. |

### `expect_run_for_lasts_until_cancelled`, `ABudgetPastTheClocksRangeRunsUntilCancelled`

`run_for` has no step limit, so the test ends the run from inside: the host task calls `cancel()` on
its own graph at the third step. With a budget of `hours::max()` or `nanoseconds::max()`, the two rows,
the report must say cancelled after exactly three steps. A deadline computed as `now + budget` overflowed for
these budgets and landed in the past, which ended the run after one step. The task throws after a
thousand steps, so a `cancel()` that reached nothing fails the test and does not hang it.

### Step hooks

A hook writes the count of finished steps and that count plus 100 into two host words. One kernel
over eight elements reads both and writes the second word into the element the first one names.

| Test | What it proves |
| --- | --- |
| `StepHooksFeedEveryReplayInTheOrderAdded` | Two rows, `Graph` and `DynamicGraph`. After `run(8)` element `i` holds `100 + i`, which needs the hook of step `i` to have run after the kernel of step `i - 1` read its words. A second hook records the word the first one wrote, which fixes the order the two run in. |
| `AStepHookRebuildsNothing` | After `run(8)` and a further `run()` the plan was built once. |
| `ANewRunCountsItsStepsFromZero` | After `run(8)` the hook last saw seven finished steps; a further `run()` shows it seeing zero. |
| `AMovedGraphKeepsItsStepHooks` | A graph with one hook is moved. The destination calls the hook when it runs. |
| `TheSourceOfAMoveHasNoStepHook` | The source records a task of its own, runs, and calls nothing. |
| `AStepHookLargerThanTheInlineStorageRuns` | The hook captures 128 bytes by value, which is more than the 64 a hook is stored in without an allocation, and reads the last of them back when it is called. |

### The error only the asynchronous handler sees

A task submits a `host_task` that throws, which SYCL reports through the asynchronous handler of
the queue and through nothing else. The two tests run in `Medium_RunDriverLayers`, one row on the chain and
one on a runtime forced onto the scheduler. `AnErrorOnlyTheAsynchronousHandlerSeesFailsTheRun` expects
`KernelFailed` from `run()`. `AnAsynchronousErrorDoesNotOutliveTheRun` then expects `Runtime::wait()` to find no error
left in a queue. They are compiled for DPC++ only, since AdaptiveCpp has no `host_task`.
