# api tests: graph recording

The tests of what a `Graph` or a `Region` records: the `add()` verbs, `Recorder`, `NodeOptions`
and the plan a recording compiles to.
They are part of `sr_api_tests`; [the page of the program](../../README.md) says how to run it.

## test_device_sized_launch.cpp

The file covers the launches of phase P3 of `docs/design/GRAPH_REPLAY.md` (sections D.2 and G): a
`Recorder::kernel(DeviceSized, ...)` and a `sized_from_device` task, both sized by a count a
predecessor wrote in the same step.

### `record_marking_rule`

`record_marking_rule` is the rule every case but two records. A resolver publishes two live words
into recorder scratch, the width and a salt, and a device-sized kernel folds its index and both
words into `marks[t]` for the work-items below the width. A word at or past the width keeps what an
earlier step left, so the bytes after a step depend on every width so far. `marked` is the host
model of one such write.

### `OnlyItemsBelowTheLiveWidthRun`

Widths of 10, 0, the capacity and 257 run in turn on one compiled plan. After each run every word
must equal the host model, and the word one past the capacity must still hold the canary. A launch
that ignored the live width, or read the width of the previous run, fails on the first comparison.

### `ANullLiveWidthRunsEveryItemAndTheArityPicksTheWords`

A null `live_width` lets every work-item run. A callable of one argument gets the index alone and
one of two arguments gets `live_words[0]` after it. `record_marking_rule` covers the callable of
three arguments.

### `ZeroCapacityLaunchRunsNothingAndReleasesItsSuccessor`

A zero-width `parallel_for` is not portable, so a launch of capacity zero submits an empty task. The
node must still complete: a successor that reads the same buffer copies the untouched canary.

### `DeviceSizedRuleChainMatchesSchedulerAsTheCountsChange`

On every step a producer kernel writes a new width, 0 and the capacity included, and the host hands
the resolver a new salt. The chain and the forced scheduler must record the same digest of the
whole buffer on each of 48 steps, bit for bit, and the digest must change on more than a quarter of
them.

### `SizedFromDeviceChainMatchesSchedulerAsTheCountChanges`

The same comparison for a `sized_from_device` task, whose live size comes from the same counter.

### `DeviceCountOverCapacityFailsAChainedRun`

A producer writes a count one above the capacity. The resolver cannot throw from a kernel, so it
writes a fault record and the run fails when the step ends. A second graph on the same runtime,
with a count that fits, runs clean on the chain.

## test_late_count.cpp

The file covers where `Recorder::resolve()` takes the live value of a `LateCount` from and when it
reads it. Every case records `record_live_count_copy`, a resolver whose fill copies the live count
into a buffer the test reads back.

### `FixedResolvesToItsCount`

A `LateCount::fixed` count has no live source, so the live count is the count it was built with.

### `ChangingHostCountKeepsOneCompile`

A host count that changes between runs, zero included, must not force a recompile. The capacity
fixes the plan and only the live value varies.

### `HostCountAboveCapacityFailsTheRunWithInvalidGraph`

A provider that returns 101 for a capacity of 100 makes the run return `InvalidGraph` with both
numbers in the message, as an out-of-window `based_at()` does. The fill never runs, so the output
keeps its canary. The provider then returns 100, and the same graph runs and writes 100: a fault
driven by run-time data fails one run and nothing else.

### `DeviceSlotResolvesFromHostResidentBuffer`

A `Residency::Host` device slot resolves to the value written into it, exactly as
`sized_from_device()` reads its own slot.

### `DeviceCountAboveCapacityFailsTheRunOnBothLayers`

A device count above the capacity fails the run on a default runtime and on one that forces the
scheduler. A second graph with a count that fits then runs clean on the same runtime.

### `ANonHostDeviceSlotStopsAtConstruction`

A device slot that is not `Residency::Host` stops `LateCount::from_device`, before a recorder sees
the count.

### `ResolverSeesTheCountItsPredecessorJustWrote`

A count sourced from the device must be read when the resolver is dispatched, after its producer,
and not polled at the step boundary. A resolver that dropped the device slot from its reads would
still compile and run, but it would report the count of the previous run. A producer node writes a
fresh count into a `Residency::Host` slot on every run and the test expects that count in the same
run.

The producer reads its next count from a buffer and not from a host stack variable, because the
kernel may run on the device, which cannot reach the stack frame of the test.

## test_native_cpu_path.cpp

The file covers the native CPU execution path, work package WP-7 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md`. `add_host()` runs plain C++ on a worker thread
with no SYCL submission. The task is ordered into the graph by the data it touches and shares one
completion contract with kernel tasks.

### `HostTaskRunsAfterKernel`

A native host task ordered after a device kernel must see the kernel's output. The task is
dispatched only once the kernel's event has completed, so the shared USM the kernel wrote is visible
on the host. The ordering comes from the shared buffer `a`.

### `KernelRunsAfterHostTask`

A device kernel ordered after a native host task must see the host's writes. The kernel waits on the
inline completion of the host task before it is dispatched, and shared USM written on the host is
visible on the device.

### `UntrackedHostTaskRunsEveryStep`

An untracked native task runs once per step, which proves that the form without dependencies is both
a root and a leaf and replays on every step.

### `ThrowingHostTaskKeepsRuntimeUsable`

A native task that throws fails the run, which is the error isolation of work package WP-0, and the
runtime stays usable: a fresh graph runs cleanly afterwards. Native tasks therefore share the kernel
error path and do not latch the worker pool dead.

## test_node_options_width.cpp

The file covers NodeOptions. The width a node states when it is recorded is the `launched_width`
and the `live_width` of its node event records, and its name and operation are in its row of
`report.node_timings`. Each node of a rule reads a region the node before it wrote, so
`report.node_events` lands in recording order whichever layer ran it.

### `EachNodeOfARuleReportsItsStatedWidth`

Two nodes of one rule state 600 and 1. The buffers they name are oversized by 9 and by 8 elements,
so a stamp that read the size of either buffer and not the stated width fails.

### `ResolverAndDeviceSizedNodeReportTheirStatedWidths`

A resolver states a width of one and a device-sized kernel states its capacity. The `LateCount` is
built with `LateCount::fixed`, so the live count is the capacity and every work-item writes.

### `ANodeReportsTheNameAndTheOperationItStated`

One kernel is recorded through `Recorder::kernel` with a width, a name, an operation and two
parameters. The string and the array the options point at are destroyed before the run, so a
recorder that kept the caller's pointers reads freed memory. The row of the node holds the name
and the value of the operation.

### `ANodeNameReplacesTheNameOfItsRule`

Two rules carry the same rule name, and the node of the first states a name of its own. The first
row reports the node's name and no operation, the second the rule's name.

### `MoreParametersThanANodeCarriesAreRefused`

Nine parameters are refused: `refusal()` holds an `InvalidGraph` error whose message names
`NodeOptions::parameters`, rule 0 and the nine parameters, and the graph holds no rule afterwards.

## test_plan_view.cpp

The file covers `compile()`, which builds a plan and runs nothing, and `Graph::plan()`, which
returns a copy of what the plan orders. `zeroed` allocates a buffer of `K_WIDTH` zeroes, so a
kernel that ran is visible in the first word.

### `CompileThenRunCompilesOnce`

`compile()` leaves `compile_count()` at one and the buffer at zero. A second `compile()` and the
`run()` after it build nothing more.

### `CompileReturnsTheRecordersRefusal`, `CompileOfAnEmptyGraphIsRefused`

`compile()` answers as `run()` does before its first step. A graph whose recorder refused a rule
returns that refusal from `compile()` and from `plan()` and builds nothing. A graph with nothing
recorded is `InvalidGraph`.

### `ADynamicGraphCompiledThenRunComposesOnce`

A DynamicGraph takes `compile()` from the same driver: `compose_count()` is one after it and still
one after the run.

### `AChainOfThreeKernelsReportsItsPredecessorsAndRegions`

Three kernels write `first`, then `second` from `first`, then `third` from `second`. The view has
three nodes in recording order with predecessor counts 0, 1 and 1, and each node's `reads` and
`writes` hold the base address of the buffers it named. The second node was given a name and an
operation, and its entry carries both. `plan()` ran nothing, and the run after it does not
compile again.

### `AViewOutlivesItsGraph`

The view is read after the graph that returned it is destroyed.

## test_recorder_scratch.cpp

The file covers `Recorder::scratch()`. Every case records `record_staged_rule`: a rule of two nodes
in which the first writes `in[i] + 1` into scratch and the second writes twice that into `out`. A
wrong or freed scratch pointer, or a second node that ran before the first, gives a wrong `out`.

### `AConsumerSeesWhatPassedThroughTheScratch`

A task that reads the output of the rule is ordered after both of its nodes, not only after the
first: the copy it makes must hold the finished values.

### `EachRunReadsItsInputAsItStands`

The input changes before each of four runs and the plan compiles once. Each run must produce the
values of the input as it stood then, through the same scratch.

### `ScratchSurvivesARecompile`

Scratch is allocated when the node is recorded, not when the plan compiles. A node added after the
first run forces a second plan, and the kernels replayed into it still hold a valid pointer.

### `ScratchIsFreedWithTheRecorder`

Recording the rule raises the allocator's live count by one, and destroying the graph brings it
back to the baseline.

### `RegionsRecordRulesWithScratch`

Two regions of a `DynamicGraph` each record the rule through `Region::recorder()`, the second
reading what the first wrote. Before the last round the test renames the rule of region 1, which
changes its recording, so the composer replays its nodes onto a new sub-graph with the scratch they
captured. Asking for the region would not do it: `region(key)` is not a mutation.

### `AScratchRequestNoDeviceCanSatisfyReturnsAllocationFailed`

The memory module has no seam that makes an allocation fail, so the test asks for half the
address space in bytes, which no device has. `scratch()` returns `AllocationFailed`, and a request
of 16 bytes on the same recorder is granted.

## test_recorder_semantics.cpp

The file covers what the recorder's words mean: a rule against a node, a name, a move, a region
access and a liveness observer. `add_increment` adds one rule that adds a step to every word of a
buffer, so a wrong plan shows as a wrong sum. `record_three_node_rule` records three such kernels
inside one `Recorder::rule()` scope.

### `ReadingARegionBetweenRunsDoesNotRecompose`

`world.region(k).size()` and `node_count()` between two runs leave `compose_count()` at one, and
the second run still executes the rule.

### `ARecordingThroughAHeldRegionRecomposes`

A `Region&` taken before the first run records a second rule after it. The next run composes
again and executes both rules, so a recording is seen without a second `region(key)` call.

### `ClearingARegionReplacesItsWork`

`Region::clear()` empties the region, a new rule is recorded, and the next run executes only the
new one.

### `ADropBetweenRunsRecomposes`

Dropping one of two regions composes once more, and the dropped region's rule does not run again.

### `AThreeNodeRuleIsOneRuleWithOneName`

The acceptance case of the rule count. A graph whose only rule has three nodes reports `size() == 1`
and `node_count() == 3`. The three nodes share one registration, so the report holds one
`NodeTiming` row, which carries the name given by one `name()`, and `rule_registration(1)`
names no rule.

### `ARegionRuleReportsItsRegistrationAndName`

The same rule in the second of two regions. `DynamicGraph::rule_registration(key, rule)` reports no
index before the first run, then index 0 for the first region's rule and 1 for the three-node rule,
because a commit numbers rules by region key and then by `add()` order.

### `NameLastOwnsItsText`

One rule is named from a string that is overwritten and destroyed before the run, another from two
temporaries in a row. The report carries the first name intact and the last of the two.

### `ANullOrEmptyNameLeavesTheDefault`

A rule named and then given a null `const char*` reports the name an unnamed rule reports.

### `MovedFromGraphIsEmpty`

After a move the source reports no rule, no node and no compile, refuses a run with
`InvalidGraph`, and then records and runs a new rule. The destination runs without compiling again.

### `MovedFromDynamicGraphHasNoRegions`

The same for a `DynamicGraph`. The destination also composes again when its region is recorded on,
which shows that the region still reports to the graph that owns it after the move.

### `OneObserverPerAllocation`

Sixteen rules over one buffer leave `Recorder::watched_allocation_count()` at one; a second buffer
raises it to two, and naming the first again leaves it there.

### `ADroppedBufferIsStillDetected`, `ADroppedBufferIsStillDetectedInARegion`

A buffer named by the first and the last of six rules is destroyed after a good run. The next run
is refused with `DanglingResource`, on a `Graph` and on a `DynamicGraph`.

### `refusal_of`

`refusal_of` clears a recorder, runs a call and returns the `Error` the call left as the
recorder's refusal, or nothing when it left none. It clears the recorder again before it returns.

### `ATaskOfTwoCommandGroupsIsOrderedByItsAccess`

A kernel writes `i + 1` into a buffer. A task recorded with `add_task` reads it and submits two
command groups: the first doubles the values into a second buffer, the second adds one into a
third and waits on the first one's event. The task returns the second event. A kernel that reads
the third buffer multiplies by three. Every element of its output is `((i + 1) * 2 + 1) * 3` only
when the task ran after the first kernel and the last kernel waited for the event the task
returned.

### `ARecorderWhoseGraphMovedNeverRepeatsAGeneration`

A graph records one rule and runs, so its plan is built at the recorder's generation. The graph is
then moved into another, which moves its recorder, and a second rule is recorded on the graph
moved from. The generation of that graph's recorder must differ from every value read before, and
the next run must execute the second rule alone: a recorder that returned to the generation of the
plan would let the old plan run. The recorder moved to keeps the generation of the work it took. A
move assignment from a graph whose recorder has a generation no larger must still leave a larger
one. Only a Graph or a Region moves a Recorder, which the static_asserts at the top of the file
hold.

### `GraphClearLeavesAGraphThatRecordsAndRunsAgain`

A graph records one rule, runs and is cleared. It then holds no rule, `rule_registration(0)` is
`K_NO_REGISTRATION`, and a run returns `InvalidGraph`. A rule recorded after the clear runs
alone.

### `AClearInsideAnExtensionStartsANewRule`

An extension records a host node, calls `clear()` and records a second host node. The graph then
holds one rule and one node, and the run executes the second node only. `extend()` holds a rule
scope open around the extension, so a `clear()` that leaves the scope's state as it is records the
second node into a rule the recorder has already dropped.

### `ANamedRuleTakesAtMostEightParameters`

`add(count, Reads, Writes, kernel, options)` and `add_rule(state, kernel, options)`, each with an
operation and nine parameters in its `NodeOptions`, are refused with `InvalidGraph`; the first message holds `9 parameters` and `at most 8`, and
neither rule is recorded. Both forms record with eight. A `static_assert` ties the literals to
`TaskMetadata::K_MAX_PARAMS`. The graph is not run.

### `AnExtensionThatRecordsNothingAddsNoRule`

A rule named `integrate` is followed by an extension that records nothing. The graph still holds
one rule and one node, the recorder's generation is the one it had before the call, and the
profiled run reports the node under `integrate`.

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
origin.

### `StateRuleOverACountUpdatesAPrefix`

`add(count, state, kernel)` updates the first `count` elements of the State.

### `ALaunchPastItsStateIsRefusedAtAdd`

An Extent whose origin plus size passes the State's size is refused with `InvalidGraph` by add().
The message names the rule, the extent with its origin and the State's size, and the rule is not
recorded.

### `CoupledRulesReadEveryStateNamed`

`add_coupled(Coupled(out, in), kernel)` and `add_coupled(count, Coupled(out, in), kernel)` each hand the kernel the
current field of the State read after the two fields of the State written.

### `ACoupledInputSmallerThanTheOutputIsRefusedAtAdd`

A coupled rule over the whole field whose input State is shorter than its output is refused with
`InvalidGraph` by add().

### `DynamicTakesItsCapacityBeforeItsAccess`

`add_dynamic(sized(...), capacity, Reads, Writes, kernel)` runs over the live count only.

### `UntrackedKernelRunsOnTheDeviceOfTheBufferNamed`

Two buffers on the runtime's last device are written, one by `add_untracked(n, On(buffer), kernel)`
and one by a raw kernel placed with `On(DeviceIndex)`. On a machine with one device that device
is 0 and the test proves only that the forms record and run; with two it proves the routing.

### `PlacementOnADeviceTheRuntimeLacksStops`

`add_untracked` with `On(DeviceIndex{device_count})` stops the process, in the launch form and in
the raw form. The message names the call the user wrote, the index and the device count. The test
is in the suite `Integration_BuilderSurfaceDeathTest`.

### `PlacementWatchesTheHandleItNames`

A graph whose `On(buffer)` handle was destroyed before the run is refused with `DanglingResource`.

### `ExtendRecordsOneNamedRule`

`extend(extension)` calls `extension` once with the Recorder; the two kernels it records are one rule, and a
run does not call it again.

### `RefusalsNameTheCallTheUserWrote`

One refused call per family, through the public add(): a State rule past its State, a State rule
over a State that owns no memory, a Coupled rule with a short input and a Coupled launch past its
State. Each message begins with the spelling of the call the user wrote and `rule 0`, and no rule
is recorded.

### `ARefusedAddIsKeptAndReturnedByTheNextRun`

Three adds are made, the second with a launch past its State. `refusal()` then holds an
`InvalidGraph` error whose message begins with `add_rule(launch, state, kernel), rule 1: `, and the graph
holds the two valid rules. A later refused `add_rule` over an empty State does not replace it. Two runs in a row
return that error and leave the target as it was. After `recorder().clear()` the refusal is gone,
and a rule added then runs.

## test_registration_index.cpp

The file covers `Node::registration_index` and the grouping of the nodes of one `Recorder::rule()`
scope under one index. The design is sections D and F5 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`.

`record_grouped_rule` records three kernels in one scope, each reading what the one before it
wrote. `grouping_of` reads a compiled plan back: the index of the nodes named "before" and "after",
and the one index every other node must share.

### `RuleScopeGroupSurvivesRebuild`

The group of a rule scope must survive a rebuild onto a fresh `TaskGraph`, whose registration
counter restarts at 0.

The test adds a plain node after the rule, which marks the graph dirty, and then runs twice, so
that the plan cache replays every recorded node into a second `TaskGraph`. The nodes of the
rule must still share one index, distinct from the index of every other node, and the indices of
all nodes must be dense from 0 to n-1 in registration order.

### `RuleScopeGroupsResolverAndDeviceSizedNodesUnderOneIndex`

A rule scope groups every node shape the same way. `record_late_bound_rule` records a late-count
resolver, a device-sized kernel and a plain kernel in one scope, and the three share one index
while the node after the scope takes the next one.

## test_region_emitter.cpp

The file covers `extend()` as a library above the runtime uses it: the extension receives the
Recorder and records device tasks with `Recorder::task`.

### `record_device_task`

`record_device_task` records one `Recorder::task` node whose body is a `parallel_for` on the queue
it is given, named and given its width through NodeOptions. The tasks are device kernels, not host
tasks, because a host access to a `Residency::Shared` buffer while a kernel runs on the same GPU
faults on Windows with CUDA; see `docs/reference/KNOWN_ISSUES.md`.

### `ExtensionTasksOrderAgainstANeighbourRegion`

One extension records two tasks on a buffer, a fill and a tripling, and a higher-keyed region reads
that buffer through an ordinary `add()`. The consumer sees the tripled values, so the two tasks ran
in their recorded order and the boundary edge ordered the consumer behind them.

### `AnExtensionIsOneRule`

An extension that records two tasks reports a region size of one, a node count of two and a graph
size of one: `size()` counts an extension once, however many nodes it records.

### `AnExtensionRunsOnceWhenItIsCalled`

The extension counts its own calls. The count is one when `extend()` returns and stays one over
three replayed steps and over a rebuild of the neighbouring region. Dropping its own region and
extending it again raises the count to two.

### `AStaticGraphTakesAnExtension`

A static `Graph` records an extension beside an ordinary `add()` that reads the extension's buffer.
Four steps advance both, so the form is not specific to regions and survives a chained replay.
