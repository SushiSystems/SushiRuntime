# api tests

The tests of the `api` module, built into one program, `sr_api_tests`, which
`modules/surface/api/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_api_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

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
settling walked that region's per-step hooks. `drop()` used to destroy the region at once, so the
settle read a freed recorder; AddressSanitizer reports that read. A following `run()` must leave
the dropped region's buffer at one, raise the other to two and bring `compose_count()` to two.

### `ARegionDroppedAndRecordedAgainWhileAStepIsPendingRunsItsNewWork`

Region 7 is dropped while its step waits at the gate, and `region(7)` then records a task on a
second buffer. The pending step must run the first recording only. The next run must run the second
recording only, which shows that the map holds the new region while the old one is kept for the
step.

## test_data_handles.cpp

The file covers what `Buffer` and `State` promise alike: what a range call does with a window, what
an allocation does with a count it cannot hold, the queries both answer, which device indices and
residencies are refused, and which element types are accepted. Most handles are shared USM on
device 0. The device-resident handles allocate on device 0 as `test_device_resident.cpp` does, so
the file needs no cluster and no more devices than that file.

### `window_pattern`, `expect_every_range_call_stops`

`window_pattern` returns the pattern of the line a range call writes when it stops: the call,
`offset <n>, count <n>` and `size <n>`. `expect_every_range_call_stops` runs `read_range`,
`write_range` and `region` of one handle as death tests against it, which is how the Buffer test
and the State test stay the same test. The tests that stop the process are in the suite
`Integration_DataHandlesDeathTest`.

### `BufferStopsOnAWindowPastTheEnd`, `StateStopsOnAWindowPastTheEnd`

A window `{size, 1}` starts one past the end, and `{size - 2, 5}` starts inside and runs over. All
three range calls stop the process for both, and the line holds the offset, the count and the
size.

### `WindowWhoseEndWrapsStops`

An offset or a count near the largest `std::size_t` makes `offset + count` wrap to a small number.
The call stops all the same.

### `EmptyWindowAtTheEndIsAccepted`

A window `{size, 0}` is valid on a Buffer and on a State: `read_range` returns an empty vector,
`write_range` accepts an empty span with a null pointer, and `region` returns a zero-length region
at the end.

### `FittingWindowRoundTrips`

The last five elements are written and read back, and the element before the window keeps its
value.

### `NullOrShortSourceWithACountStops`

`write_range` with a count of four stops the process for a null source, on a Buffer and on a
State, and for a source of two elements.

### `OverflowingElementCountStopsBeforeAllocating`

The count is chosen so that `count * sizeof(double)` wraps to eight bytes, a size any allocator
grants. `Runtime::buffer` and `Runtime::state` stop the process with the count in the message.

### `StateAndBufferAnswerTheSharedQueriesAlike`

`device_index`, `size`, `empty`, `residency` and `is_host_accessible` return the same on a State and
on a Buffer allocated alike, and `State::region` names a window of the current field.

### `TriviallyCopyableStructRoundTrips`

A struct of two floats instantiates `Buffer` and `State`, and a window written with `write_range`
reads back equal through `read_range`. The negative case, a `T` that is not trivially copyable, is
a compile error from the `static_assert` in `Buffer`, so no test can run it.

### `HostOnADeviceResidentHandleStops`, `IndexingADeviceResidentHandleStops`

`host()` on a device-resident Buffer or State stops the process, and the line names
`Buffer::host` or `State::host`. `operator[]` is checked with `SR_ASSERT`, so its test is compiled
only where `SR_ENABLE_ASSERTIONS` is 1.

### `DeviceIndexThatNamesNoDeviceStops`

`buffer()` and `state()` with a device index equal to the device count stop the process. The
line names `Runtime::buffer` or `Runtime::state`, the index, the device count and
`advanced().devices()`.

### `AlignedAllocationStartsOnAMultipleOfTheAlignment`

`buffer()` and `state()` are asked for 37 floats aligned to 128 bytes in each of the three
residencies. Where the device returns memory, the buffer's address and both fields of the state
must be multiples of 128. A residency that returns `AllocationFailed` is not checked, and the test
fails when none could be.

### `AlignmentThatIsNotAPowerOfTwoStops`

An alignment of 48 stops the process in `buffer()` and in `state()`; the line names the call and
the value.

## test_device_count.cpp

The file covers `sized_from_device`: a task whose iteration size is a count that a predecessor
kernel produced within the same step.

A collision pipeline has this shape. The broadphase writes a pair count, the narrowphase iterates
exactly that many pairs and writes a manifold count, and the solver iterates exactly that many
manifolds. None of those numbers exists when the step boundary polls the host-side `sized()`
providers. Without `sized_from_device` the choices are a readback in the middle of the step or a
dispatch at the worst case on every tick.

### `ConsumerIteratesTheCountTheProducerJustWrote`

A producer kernel writes a count, and a consumer sized from that count iterates exactly that many
elements, in one `run()`. The consumer must see the count this step wrote, not the count of the
previous step and not the capacity.

The producer computes the count on the device. The test reads it through a pointer, so the host
cannot have supplied it.

### `CounterIsRegisteredAsAReadAutomatically`

The caller does not have to remember the edge between producer and consumer. The counter is
registered as a read of the consuming task, so the consumer is ordered after the producer even
though the caller named neither in `Reads()`. Without that edge the consumer could read the count
before it was written and would iterate a stale value, 0 on the first step.

### `ChainedProducerCountsInOneComposition`

A three-stage chain is the real shape: the output count of each stage is the iteration size of the
next, all in one `run()` with one compilation.

### `WhenComposesWithDeviceSize`

`when()` still composes with a size driven by the device. A disabled step runs nothing, whatever the
counter says.

### `CountOverCapacityFailsTheRun`

A count larger than the compiled capacity fails the run with a message that names both numbers.
Truncating would silently drop work, which is the failure `sized_from_device` exists to remove.

### `ACounterThatIsNotHostResidentStops`

A counter the host cannot read at every moment of a run stops the call that names it, before any
run starts. `Device` memory is never host-addressable, and `Shared` memory is managed memory
the host may not touch while a kernel runs on some backends.

### `ReadsTheCountAtTheGivenIndex`

A caller with one plan array per device names an element of the array by index. The consumer must
read that element: element 5 holds 3, and exactly three elements run.

### `AcceptsABaseSlotAtTheGivenIndex`

`based_at_device` reads its slot exactly as `sized_from_device` does, through the same check, so a
base slot at a non-zero index is accepted at `add()`.

### `MemberSettersStopOnASlotThatIsNotHostResident`

The members `Dynamic::sized_from_device` and `Dynamic::based_at_device` take the `Buffer` and check
it as the free builders do. A device-resident slot and an index one past the buffer stop the process
where the member is called, and each message names the call. Up to v0.3 the members took a bare
pointer, and the tests here that handed them an interior pointer, a device address and a stack
address have no call left to make.

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

## test_device_resident.cpp

The file covers device-resident state, work package WP-6 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md`. A buffer or a state allocated with
`Residency::Device` stays on its device and is not host-addressable. The host seeds it and reads it
back through `write_range()` and `read_range()`, while kernels run against it exactly as they do
against shared USM.

### `BufferRoundTrip`

A device-resident buffer reports its residency and is not host-accessible. A host seed, a kernel and
a host readback round-trip correctly without the host ever indexing the allocation.

### `StateStepsResident`

A device-resident `State` steps with the fluent `add_rule(state, kernel)` rule and is seeded and read back
through its range API. The field stays resident across all steps, and only the windowed readback
touches the host.

### `SharedDefaultUnchanged`

Shared residency stays the default and is host-addressable. Its range API works alongside direct
indexing, so existing code is unaffected.

### `HostSpanIndexesSharedBuffer`

`host()` is the branch-free bulk access to shared USM: one residency check, then unchecked indexing
over the whole buffer.

### `HostOnADeviceBufferStops`

A device-resident buffer is not host-addressable, and `host()` on one stops the process in every
build.

### `HostBufferIsWrittenByAKernelAndReadOnTheHost`

A host-resident buffer is host-addressable, a kernel on its device writes it, and the host reads the
result by direct indexing.

## test_dynamic_graph.cpp

The file covers the dynamic, region-partitioned graph (`Runtime::dynamic_graph()`). Each test
asserts the numerical outcome and the composition behaviour: regions run together, data dependencies
across regions order correctly, dropping a region removes its work, and a run without a mutation
never recomposes.

### `IndependentRegionsRunAndComposeOnce`

Two independent regions each own a buffer and increment it once per step. Over several steps both
buffers advance, and a run that mutates nothing composes the combined plan exactly once.

### `CrossRegionDependencyOrders`

A producer region writes `x`, and a consumer in a different region reads `x` and writes `y`. Both
touch `x`, so the composer must wire an edge across the regions for the consumer to see the
producer's writes: `y[i] == 2*(i+1)`. Ordering across regions follows the ascending region key, so
the producer has key 10 and the consumer key 11, as the documented contract requires.

### `DropRegionStopsItsWork`

Dropping a region removes its work at the next step boundary. After the drop the surviving region
keeps advancing, and the buffer of the dropped region freezes.

### `AddRegionBetweenSteps`

Adding a region between steps recomposes exactly once, and the work of the new region takes effect
on the next run.

### `SingleRecomposePerMutation`

Mutating one region out of many recomposes once, and the unchanged regions are not rebuilt. The
proof is indirect: the results stay correct, and `compile_count` advances by exactly one per
mutation, not per region.

### `FindRegionReadsWithoutRecomposing`

Reading a region through `find_region()` is not a mutation. The plan must not recompose on the next
run, and an absent key returns `nullptr` where `region()` would create the region.

### `ThreeRegionChainThreadsValue`

The chain has three hops across three buffers: region 0 writes `x`, region 1 reads `x` and writes
`y`, and region 2 reads `y` and writes `z`. Each hop is a separate boundary. The composer must order
the regions 0, 1, 2, so that `z` carries the value threaded through both hops: `z[i] == 2*(i+1) +
1`.

### `ReadReadSharingStaysConsistent`

One producer writes `base`, and two consumer regions both read `base` and write their own outputs.
The two readers share no hazard, so the composer must not order them against each other; both see
the producer's writes. The observable is correctness: the output of each reader reflects `base`.

### `StreamInConsumerRewiresProducer`

Streaming a consumer in beside an existing producer must re-stitch the producer's boundary. Region
0, which writes `x`, runs alone first. Region 1, which reads `x`, then streams in and observes `x`
on the next run. The test exercises the path that re-wires a neighbour, where the exit pin of an
unchanged region gains a successor in another region.

### `DropProducerLeavesConsumerRunnable`

Dropping the producer must cleanly un-wire the boundary and leave the consumer runnable, now without
an upstream edge. Region 1 keeps running after region 0 is dropped, and the run neither deadlocks
nor faults.

`y` accumulates `px` across two steps. The test does not depend on the exact value; it checks that
the run completed and the consumer kept executing.

### `DisjointRangeRegionsAreNotOrdered`

Two regions that write disjoint element ranges of one buffer must not be ordered against each other.
No public interface exposes the boundary edges, so the test proves the property by behaviour. Both
region tasks rendezvous on a shared counter with a bounded deadline, and each records whether it saw
the other arrive. If the tasks run concurrently, both see it. If they are serialized, the first one
times out and only the second counts, which is what a boundary at allocation granularity produces.

The deadline means a failure reports and does not hang. The test is skipped on a machine that cannot
run two tasks at once, where the property cannot be observed.

### `OverlappingRangeRegionsStillSerialize`

This test is the counterpart of `DisjointRangeRegionsAreNotOrdered`: two regions that write the same
range of one buffer must still be ordered. When the tasks are serialized the earlier one cannot see
the later one arrive, so exactly one of the two records the rendezvous. The arithmetic is sensitive
to order and comes out to the ordered answer.

### `WholeBufferReaderOrdersAfterBothHalfWriters`

Boundary ordering on sub-ranges must still produce the right values. Two regions write disjoint
halves and a third reads the whole buffer, so the reader overlaps both writers and has to run after
each of them.

## test_dynamic_graph_node_events.cpp

The file covers the per-graph profiling switch of `DynamicGraph` and its registration index, which
is dense and ordered by ascending region key and then by `add()` order at every recompose. The
design is sections D, E (item 8) and F.6 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`, built as task 6 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-plan.md`.

The file reaches the plan through `dynamic_graph_plan_access.hpp`.

### `registration_index_of`

`registration_index_of` returns the registration index of the one compiled node called `name`,
which `find_plan_node` looks up. `plan.all_nodes` is populated only while the graph requests
profiling (`publish_plan()` in `region_composer.hpp`), and every test in the file relies on that.

### `NotRequestedRecordsNothing`

A graph that never requested profiling records nothing, whatever the default of the runtime is.

### `RequestedGraphProducesRecordsMatchingRenumberedIndex`

A `DynamicGraph` that requested profiling must produce records; a correctly stamped flag is not
enough. It needs its own call to `CompiledGraph::capture_profiling_state()`, because it has no
`capture_base_state()` of its own that could make the call for it.

### `IndexDenseAndOrderedByRegionThenAddOrder`

With two regions that each make more than one `add()` call, `node_index` is dense and ordered by
ascending region key first and by `add()` order within a region second.

Region 5 is added first but has a higher key than region 2, so its two calls must land after the one
call of region 2 once the order follows the key.

### `IndexStaysDenseAfterDropAndReAdd`

Dropping a region and adding it again must not leave a gap in the index space. The next recompose
renumbers every live region densely from zero.

## test_fluent_api.cpp

The file covers the parts of the fluent API that `test_fluent_lifecycle.cpp` does not exercise:

- `run_for(duration)`
- the success path of `run()` and the fields of `RunReport`
- the `add(kernel)` overload that takes a raw `sycl::handler`
- the `add(n, kernel)` range kernel overload
- the pointer-tracked `add(reads, writes, n, kernel)` overload
- `Reads` and `Writes` over `Buffer::region()`, which track a sub-region
- a standalone `Buffer<T>` allocation used in a graph
- `Graph::size()`
- the `Runtime::advanced()` and `unsafe_context()` accessors
- `run(0)`

### `TryRunSuccessPath`

`run()` on a non-empty graph must return a successful result with `total_tasks_executed` filled
in. The test proves that the non-throwing path returns a valid `RunReport` and not a
default-constructed one.

### `RunReportFieldsAfterMultiStep`

After a multi-step run, `total_tasks_executed` must equal the tasks per step times the steps, and
`total_duration_ms` must not be negative.

### `RunForRunsAtLeastOneStep`

`run_for(duration)` must run at least one step and then stop. The budget is very short so that the
test does not wait; the test validates the one-step guarantee.

### `RunForRunsMultipleStepsWithinBudget`

With a real budget, `run_for` should complete several steps and the report must reflect them. The
test measures steady-state throughput, not a cold start.

The kernel is warmed before the timing starts. The first dispatch pays for JIT compilation and for
queue and device setup once, and that alone can exceed the 50 ms budget. `run_for` fixes the
deadline before its first step, so an unwarmed kernel overshoots and stops after a single step. In
steady state this trivial kernel costs less than a millisecond, so a warmed run fits many steps.

### `RawHandlerKernelAdd`

`add_untracked(kernel)` takes a function that receives `sycl::handler&` directly, the raw kernel
overload. The kernel must run and produce the expected values.

### `RangeKernelAdd`

`add_untracked(n, kernel)` takes a function that receives `(begin, end, sycl::handler&)`, the range
kernel overload. The function must see `begin = 0` and `end = N` and launch its own work.

### `PointerTrackedAddOrdering`

`add(reads, writes, n, kernel)` is the overload with explicit dependency vectors. Two tasks share one
buffer: a writer, then a reader that squares the values. The dependency tracker must order them
correctly without a `State`.

### `TrackedRangeKernelOrdering`

The tracked range-kernel overload runs the writer and squarer pair of `PointerTrackedAddOrdering`,
but the second task drives the handler itself. The declared `Reads` and `Writes` must still order it
after the writer. An untracked overload would let the square race the write.

### `TrackedRawKernelOrdering`

With the tracked raw-kernel overload the reader owns the whole command group, and its declared
regions still order it after the writer.

### `TrackedNdRangeOrdering`

With the tracked `NdRange` overload a tiled work-group kernel keeps dependency tracking and still
gets an `nd_item`. The kernel is ordered after the writer, and the `nd_item` indexing must cover the
whole global range.

### `SubRegionDisjointWritesThenWholeRead`

Two writers touch disjoint halves of one buffer, so the tracker leaves them unordered. A
whole-buffer reader then overlaps both and must run after each. The test proves sub-region identity
end to end through the fluent layer: disjoint slices are independent, and an overlapping access
still orders. An identity based on the base pointer would either serialize the halves or let the
reader observe an unwritten half.

### `BufferAllocateUseAndReadBack`

A standalone `Buffer<T>` is allocated, seeded on the host, used in a graph task through
`Reads` and `Writes` on its data pointer and read back. The test verifies the lifecycle of owning USM.

### `GraphSizeTracksAdds`

`Graph::size()` tracks the number of `add()` calls made so far, also after a mutation that triggers
a recompilation.

### `ContextAccessorIsConsistent`

`Runtime::unsafe_context()` must return a reference to the `RuntimeContext` the fluent `Runtime`
wraps, and `advanced()` must read through to it. The test verifies both by comparing device counts.

### `RunZeroStepsIsNoOp`

`run(0)` is documented as zero steps. It must return immediately, and the `RunReport` should show no
work done.

## test_fluent_hardening.cpp

The file covers the hardened fluent surface:

- `add_untracked(Extent<2>, kernel)`, a multidimensional per-element map
- `add_untracked(NdRange<2>, kernel)`, a tiled work-group kernel
- `add_rule(Extent<2>, state, kernel)`, a multidimensional stepped stencil
- `add(out, in0, in1, kernel)`, a variadic coupled update that reads three fields
- `add(Reads, Writes, n, kernel)`, the handle-based dependency wrappers
- `add(Extent, In/Out, kernel)`, buffer arguments captured automatically
- `add_rule(Extent with offset, state, kernel)`, an interior-only (halo) update

### `Extent2DPerElementMap`

With `add_untracked(Extent<2>, kernel)` the kernel sees a `sycl::id<2>` that covers the whole box and
writes a row-major linear index, which proves 2-D iteration and indexing.

### `NdRange2DTiledKernel`

`add_untracked(NdRange<2>, kernel)` runs a tiled kernel in the work-group model. Each item writes its
global linear index, and a correct result proves that `nd_item` indexing works.

### `State2DSteppedUpdate`

`add_rule(Extent<2>, state, kernel)` is a multidimensional stepped update. Each step adds the row index to
every cell, so after S steps a cell in row y holds `S*y`.

### `VariadicCoupledThreeInputs`

`add(out, in0, in1, kernel)` is a coupled update that reads three fields, `out` and two others: `next =
cur + a + b`. All reads see data of the previous step (Jacobi).

### `ReadsWritesWrappersOrdering`

With `add(Reads, Writes, n, kernel)` the dependency wrappers must order a writer before a dependent
reader exactly as the raw `void*` vectors do.

### `InOutCapturedArguments`

With `add(Extent, In/Out, kernel)` the captured buffer arguments give the kernel raw pointers and
register the dependency in one step. The kernel computes `dst = src * 2`.

### `InteriorOnlyHaloUpdate`

With `add(Reads, Writes, Extent-with-offset, kernel)` an interior-only update must leave the one-cell
border, the halo, untouched, which proves sub-range iteration. The test uses an in-place `Buffer` so
that the border keeps its initial value deterministically.

## test_late_bindings.cpp

The file covers the persistent-graph, late-binding path, work package WP-1 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md`. A graph whose live size or per-step participation
changes between steps must be compiled exactly once and replayed, never recompiled. Each test
asserts the numerical result and `Graph::compile_count() == 1`.

### `DynamicSizePerElement`

`sized()` shrinks the iteration range to a live prefix on each step. Driving the live count from 1
to `CAP` over `CAP` successive `run(1)` calls increments `buf[i]` exactly on the steps where `i` is
inside the live range, so `buf[i] == CAP - i`. The plan must compile once and follow the changing
size on replay.

### `EnableTogglePerElement`

`when()` gates whether the node runs at all. Toggling the predicate over ten steps runs the
increment only on the enabled steps, so each element ends at the count of enabled steps, with a
single compile.

### `DisabledNodeStillReleasesSuccessors`

A disabled step must release its successors so that the rest of the graph still runs. A downstream
task that always runs should advance on every step whatever the enable state of the upstream node,
which proves that an instantly completed node drains the DAG.

Upstream writes the gate buffer. Downstream reads the gate buffer and writes the sink, so the
tracker orders downstream after upstream on the gate buffer.

### `DynamicSizeState`

`sized()` works on a stepped `State`: the live prefix grows on each step while the graph is compiled
once. Cells that are active on every step accumulate one increment per step, which is what a
single-threaded reference loop would produce for that prefix.

### `ComposedWhenAndSized`

`when()` and `sized()` compose. The node runs only while enabled, and when it runs it iterates the
live count. Over the run an element is incremented once per step that is both enabled and in range,
still with a single compile.

### `BaseOffsetIteratesASlice`

`based_at()` moves where a late-bound node starts, without a recompile. Graph colouring has this
shape: each colour is a contiguous slice of one constraint buffer, and every slice shifts as
constraints are added or removed. The callable still receives absolute indices, so a kernel written
without knowledge of bases stays correct.

### `BaseWithoutASizeRunsToTheEnd`

A base with no size runs from the base to the end of the capacity, not over the whole buffer. The
sentinel means "the rest", and with an offset the rest starts at the offset.

### `BaseAndSizeBothFromTheDevice`

`based_at_device()` takes the offset from a slot a predecessor wrote, so a colouring pass that
computes the start and the length of a slice on the device can hand over both in the same way. The
slot is registered as a read, so the producer is ordered first without the caller naming it.

### `WindowPastCapacityFailsTheRun`

A window that runs off the end of the capacity fails the run and names what it asked for. Clamping
would silently drop the tail of a colour, the kind of failure `sized()` and `based_at()` exist to
rule out.

### `BasedAtDeviceStopsOnAnUnreadableSlot`

`based_at_device` stops the process on a slot it could not read at dispatch, and it does so at the
call site, not in the middle of a run.

### `TheMemberSetterStopsOnASharedSlot`

`Dynamic::sized_from_device` on a named Dynamic stops the process on a slot inside `Shared` memory
where it is called, as the free builder does.

## test_late_bound_vocabulary.cpp

The file covers what `LateCount` and `Dynamic` let a caller write and which arguments stop the
process when they are built. Eight `static_assert` lines at the top state the shape: a `LateCount` has no default
constructor and no assignable `capacity`, it copies, and its `Provider` is `Dynamic::CountProvider`;
a `Dynamic` has no public `when_pred`, no setter that takes a bare pointer, and moves without
copying. A field made public again, or a raw-pointer overload added back, fails the compile.

### `record_live_count_copy`, `count_elements_run`

`record_live_count_copy` records a resolver whose fill copies the live count into a buffer the test
reads back. `count_elements_run` adds one late-bound task that sets `marks[i]` to one, runs it once
and returns how many elements were set.

### `FromDeviceStopsOnAnIndexPastTheSlot`

`LateCount::from_device(64, slot, slot.size())` on a slot of three elements stops the process
where it is called, and the line holds the call, `index 3` and `3 elements`. The last valid
index is accepted.

### `FromDeviceStopsOnASlotThatIsNotHostResident`

A `Device` slot and a `Shared` slot both stop the call, and the line names `Residency::Host`.

### `FromHostStopsOnAnEmptyProvider`

An empty provider would read as a fixed count, which `fixed()` already spells, so `from_host`
stops the process.

### `EveryFactoryStopsOnACapacityPastAUint32`

A capacity one above the largest uint32 stops all three factories, and the largest uint32 itself
is accepted. The test is skipped where `std::size_t` is no wider than a uint32.

### `CapacityReportsThePlanSize`

`capacity()` returns the first argument of each factory.

### `ARecordedCopyKeepsTheHostProvider`

A count is built, copied and recorded inside a scope that then ends. Two runs with two different
live values must both reach the fill: the recorded copy shares the provider and does not point into
the destroyed original.

### `FromDeviceReadsTheWordAtItsIndex`

Element 2 of a four-word slot holds 77 and the others hold 5. A count built with index 2 resolves
to 77.

### `TheSizeSourceSetLastWins`

A device slot holds 3 and a host provider returns 5. `sized_from_device(counter).sized(host)` runs
five elements and `sized(host).sized_from_device(counter)` runs three.
### `MembersChainOnANamedDynamic`

`when`, `sized` and `based_at` chain on a named `Dynamic`, which then moves into `add()`. Elements
10 to 13 run once, and a second run with the predicate false runs none.

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

## test_logging_names.cpp

The file includes `api/runtime/logging.hpp` and no other header of the library.

### `TheControlsAreCallableFromTheSurfaceHeader`

Two `static_assert` lines state that the header declares `set_log_synchronous(bool)` and
`set_log_sink(LogSink)` in `SushiRuntime`. The test calls `set_log_synchronous`, `set_log_sink`,
`set_log_level` and `log_level` and puts back what it changed. It logs nothing: the header has no
call that writes a record, and `modules/foundation/logging/tests/test_logger.cpp` covers what
synchronous delivery does.

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

### `AMovedFromRecorderNeverRepeatsAGeneration`

A graph records one rule and runs, so its plan is built at the recorder's generation. The recorder
is then moved out of the graph and a second rule is recorded on the graph. The generation of the
recorder moved from must differ from every value read before, and the next run must execute the
second rule: a recorder that returned to the generation of the plan would let the old plan run. The
recorder moved to keeps the generation of the work it took. A move assignment from a recorder with
a generation no larger than its own must still leave it with a larger one.

### `AClearInsideAnExtensionStartsANewRule`

An extension records a host node, calls `clear()` and records a second host node. The graph then
holds one rule and one node, and the run executes the second node only. `extend()` holds a rule
scope open around the extension, so a `clear()` that left the scope's state as it was recorded the
second node into a rule that no longer existed.

### `ANamedRuleTakesAtMostEightParameters`

`add(count, Reads, Writes, kernel, options)` and `add_rule(state, kernel, options)`, each with an
operation and nine parameters in its `NodeOptions`, are refused with `InvalidGraph`; the first message holds `9 parameters` and `at most 8`, and
neither rule is recorded. Both forms record with eight. A `static_assert` ties the literals to
`TaskMetadata::K_MAX_PARAMS`. The graph is not run.

### `AnExtensionThatRecordsNothingAddsNoRule`

A rule named `integrate` is followed by an extension that records nothing. The graph still holds
one rule and one node, the recorder's generation is the one it had before the call, and the
profiled run reports the node under `integrate`.

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
synchronous and the asynchronous path. `run()` used to report `CompileFailed` for it, and
`wait()` used to let the task's own exception through.

### `AThrowingBuildIsCompileFailed`

No code of the caller runs when the plan is built except the copy of each recorded callable into
its node. The second task is a `Testing::RefusingFill` whose copy throws, so the build throws. It
must yield `CompileFailed` from `run()` and from `run_async()`, which shows that the code
follows the phase and not the exception type.

### `EveryTryEntryRunsOnBothGraphs`

`expect_every_try_entry_runs` calls the eight `try_` entries once each on a graph whose single host
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
to two. `PlanCache::set_profiling` used to mark the plan dirty on every call, so the `Graph` count
read two after the second run.

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
started on, with the starting value five in it. The chain used to advance before it checked, so the
`State` showed six. A chained `run(3)` whose steps fault must leave seven: the two steps before
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

## test_runtime_entry.cpp

The file covers `Runtime::create()`, `RuntimeConfig` and the `AdvancedControls` view. Four
`static_assert` lines at the top state that a `Runtime` moves without throwing and does not copy, so
a copy constructor that returns fails the compile and not a test.

### `OneStructConfiguresAndTheViewOutlivesTheRuntime`

One `RuntimeConfig` with `worker_count` 2 must produce a pool of two workers. The view is taken
while the `Runtime` lives and questioned again after the `Runtime` is destroyed: it shares ownership
of the context, so it must give the same device count. A view that held a reference would read a
destroyed context here, which AddressSanitizer reports.

### `DevicesListsEveryDeviceInOrder`

`devices()` must hold one entry per device, and entry `i` must describe the device `DeviceIndex{i}`
selects. The test compares each entry's name with the name the SYCL device behind queue `i` reports,
then allocates a buffer with no `DeviceIndex` and asks the allocator which device owns it: that is
entry 0.

### `DefaultConfigCreates`

`create()` with no argument must yield a runtime with profiling and the rebalancer off that runs a
graph. Every factory is called through a `const Runtime`, so a factory that lost its `const` fails
the compile.

### `EveryFieldReachesTheRuntime`

Each field of `RuntimeConfig` but `first_core` is set away from its default and read back from the
runtime; `tests/integration/test_co_tenancy.cpp` reads `first_core` back on a machine with cores to
reserve. The api struct is mapped to the engine's settings by hand in one function, so a field that
function forgets would otherwise keep its default silently.

### `AMovedRuntimeKeepsItsContext`

A move hands the context over and does not build a second one.

### `ACallerWaitsAndCopiesThroughTheDeviceQueue`

The test submits a kernel on the queue `advanced().queue()` returns and does not wait for its
event. `Runtime::wait()` returns once the kernel has written the buffer, and a `memcpy` through
the same queue brings the values to a host vector. The test names no type of `Internal::`.

### `AQueueOfAnUnknownDeviceStops`

`advanced().queue()` asked for the device after the last one stops the process.

### `ARequestNoDeviceCanSatisfyReturnsAllocationFailed`

`buffer()` and `state()` are asked for half the address space in bytes, which no device has; the
memory module has no seam that makes an allocation fail. Both return `AllocationFailed`, the
buffer's message holds the element count, and a buffer of 16 bytes is granted afterwards.

### `LiveAllocationCountFollowsABuffer`

`advanced().live_allocation_count()` is read before anything is allocated. A buffer raises it by
one and a state by two more. When both leave their scope the count is back at its first value.

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

### `PlacementIsCheckedAndItsHandleWatched`

`On(DeviceIndex{device_count})` is refused with `InvalidAccess`, naming the index and the count. A
graph whose `On(buffer)` handle was destroyed before the run is refused with `DanglingResource`.

### `ExtendRecordsOneNamedRule`

`extend(extension)` calls `extension` once with the Recorder; the two kernels it records are one rule, and a
run does not call it again.

### `RefusalsNameTheCallTheUserWrote`

One refused call per family, through the public add(): a State rule past its State, a Coupled
rule with a short input, a Coupled launch past its State, `add_untracked` with an `On` the runtime
lacks (both the launch and the raw form), and an offload with nine parameters (both the task and
the State rule). Each message begins with the spelling of the call the user wrote and `rule 0`,
and no rule is recorded.

### `ARefusedAddIsKeptAndReturnedByTheNextRun`

Three adds are made, the second with a launch past its State. `refusal()` then holds an
`InvalidGraph` error whose message begins with `add_rule(launch, state, kernel), rule 1: `, and the graph
holds the two valid rules. A later refused `add_untracked` does not replace it. Two runs in a row
return that error and leave the target as it was. After `recorder().clear()` the refusal is gone,
and a rule added then runs.

## test_callable_traits.cpp

The file covers the callable traits of `detail/callable_traits.hpp` and `is_launch_kernel_v` of
`detail/callable_checks.hpp`. No case runs a graph: each reads a trait for a lambda declared in
the test.

### `AMutablePerElementKernelIsRejected`, `AMutableRangeKernelIsRejected`

`is_per_element_v` and `is_range_kernel_v` are true for a lambda and false for the same lambda
declared `mutable` while it captures a counter. The emitters call both kernels as const objects, so
a trait that accepted the `mutable` one let it past the `static_assert` and failed inside the
emitter. A captureless `mutable` lambda is not covered here: it converts to a function pointer.

### `AMutableRawKernelIsAccepted`

`is_raw_kernel_v` is true for a `mutable` lambda: `raw_node()` stores the command group in a
`std::function`, which calls it as a non-const object.

### `ACoupledKernelMustTakeItsInputsAsConst`

`accepts_coupled_rule()` holds a `static_assert`, so the test reads the trait under it.
`is_launch_kernel_v` with a `const Word*` input is true for a kernel that declares the input
`const Word*` and false for one that declares it `Word*`, for a count and for an `Extent<1>`.

## test_state_emitters.cpp

The file covers the nodes `detail/emit_state.hpp` and `detail/emit_dynamic.hpp` build for State
and coupled rules. `seeded_state` fills the current field with the index plus `K_SEED` and the next
field with `K_CANARY`, so an element a kernel did not write is recognisable.

### `ACoupledRuleReadsItsInputsThroughConstPointers`

`add_coupled(Coupled(out, left, right), kernel)` with a kernel that takes both inputs as `const Word*` writes
`current + a * b` into `out` and leaves both inputs as they were.

### `StateCoupledAndUntrackedNodesReportTheirLaunchWidth`

A profiled graph holds `add_rule(state, kernel)`, `add_coupled(count, Coupled, kernel)` and `add_untracked(count, kernel)`.
Their node events report the State's size and the two counts as `launched_width`. The three nodes
share no region and may finish in any order, so the widths are compared sorted.

### `AStateNodeBuiltFromItsViewRunsTheRule`, `ACoupledNodeBuiltFromItsViewsRunsTheRule`, `ADynamicStateNodeBuiltFromItsViewRunsTheLiveWindow`

Each case builds a replay callable outside a recorder and hands it the State's own view from
`HandleAccess::view()`. `run_once` replays the callable onto an engine TaskGraph of its own,
compiles that graph and executes the plan on the runtime's engine. One step runs and the State's
next field holds the rule's result. No advance is registered, so the test reads `next()`.
The late-bound case sets the live count on its `LateBindings` and checks that the elements past it
keep `K_CANARY`.

## test_device_slot_liveness.cpp

The file covers the liveness of the buffer a `Dynamic` reads a device value from. `host_slot`
allocates the one-word `Residency::Host` buffer.

### `ADestroyedCountBufferRefusesTheRun`, `ADestroyedBaseBufferRefusesTheRun`

A graph records a rule with `sized_from_device(counter)` or `based_at_device(base)` and runs once.
The buffer is then destroyed and the next `run()` returns `DanglingResource`. The count case
also checks that the recorder watches two allocations, the target and the counter.

### `ADestroyedCountBufferRefusesAStateRule`

The same refusal for `add(sized_from_device(counter), state, kernel)`, whose observers the recorder
gathers in a different member.

## test_launch_reach.cpp

The file covers `Detail::launch_reach()` for a rank-1 launch whose origin plus size does not fit
a `std::size_t`.

### `ReachSaturatesAtTheLargestSize`

An `Extent<1>` of 8 elements at origin 16 reaches 24. The same extent and an `NdRange<1>` at
origin `SIZE_MAX - 3` reach `SIZE_MAX`.

### `AnExtentWhoseEndWrapsIsRefusedAtAdd`

`add(extent, state, kernel)` with that extent over a State of 8 elements is refused with
`InvalidGraph` and records nothing. A wrapped sum would be 4, which is inside the State.

## test_device_fault.cpp

The file covers `Detail::record_device_fault()` for values past a uint32. Two `static_assert`s pin
`saturated_fault_word()` at compile time.

### `AFieldPastAUint32IsStoredSaturated`

A record written with a base and a capacity of `UINT32_MAX + 5` holds `UINT32_MAX` in both words
and the count as given. `take_device_fault()` then returns an `InvalidGraph` error that names
`4294967295` as the start of the window, clears the record, and returns nothing when asked again.
The test is skipped where `std::size_t` is 32 bits wide.

## test_state_move.cpp

The file covers a State that moves after `add()` recorded a rule on it, and the view a State shares
with the graphs that record it. `seeded_state` fills the current field with the index plus `K_SEED`
and the next field with `K_CANARY`, `add_increment` records `next = current + 1` over the whole
field, and `expect_advanced` compares the current field with the seed plus a number of steps.
Three `static_assert` lines state that a State moves without throwing and does not copy.

### `ARuleFollowsAStateMovedAfterAdd`

A rule is recorded, the State is moved into a new variable, and three steps run. The moved-to State
holds the seed plus three. A recorder that kept the address of the State read a moved-from object
here.

### `ARuleFollowsAStateItsVectorReallocated`

The rule is recorded on element 0 of a `std::vector<State>`, and States are pushed until the
address of element 0 changes. Three steps then advance element 0 and leave element 1 as seeded.

### `ACoupledRuleFollowsItsMovedStates`

`add_coupled(Coupled(out, input), kernel)` is recorded and both States are moved. Two steps leave three times
the seed in the State written and the seed in the State read.

### `ADynamicStateRuleFollowsAMovedState`

`add_dynamic(sized(...), state, kernel)` is recorded and the State moved. Three steps advance the live prefix
on one compiled plan.

### `TheViewFollowsAdvanceMoveAndDestruction`

The case reads the view through `HandleAccess::view()`. It holds the State's two field pointers and
its size; an advance swaps the pointers; the moved-to State returns the same view object, and an
advance through that view swaps the moved-to State. After the State is destroyed the view holds
null pointers and a size of zero, and an advance through it does nothing. A default-constructed
State yields an empty view and not a null pointer.

### `AStateAssignedOverRefusesTheRun`

A State with a recorded rule is move-assigned a new State. The next `run()` returns
`DanglingResource`.

### `ADynamicStateRuleReplayedAfterAStepReadsTheCurrentField`

A late-bound State rule used to take its read key when it was recorded and its write key at each
replay. After one step the two fields have swapped, so a plan rebuilt then registered the rule as a
reader and a writer of the same field and not as a reader of the current one.

The graph holds a kernel that writes `K_MARK` into the State's second field, named with
`Writes(state)`, and then the late-bound rule `next = current + 1`. After one step the second field
is current. The test renames the rule, which makes the plan stale, and reads `graph.plan()`: the
rule's node must read the second field and have one predecessor, the writer of that field. It then
runs a second step and expects `K_MARK + 1` everywhere and two compiles in all.
The predecessor count is the deterministic check; the values depend on the order only when the scheduler
runs the two unordered nodes the other way round.

## refusing_fill.hpp

### `RefusingFill`

`Testing::RefusingFill` is a host task that fills a range, and whose copy constructor throws a
`std::runtime_error` while a flag the test holds is set. Recording moves the task, and every
build of a plan copies it into the node, so the flag decides whether a build throws.
`test_run_driver.cpp` and `test_region_commit.cpp` include the header by its relative path.

## dynamic_graph_plan_access.hpp

The header is the seam the tests use to read the composed plan of a `DynamicGraph`, which has no
public accessor. `test_dynamic_graph_node_events.cpp`, `test_boundary_access.cpp` and
`test_region_commit.cpp` include it by its relative path.

### `DynamicGraphNodeEventsTestAccess`

`DynamicGraphNodeEventsTestAccess` is the friend `dynamic_graph.hpp` names, used only by tests to
reach `DynamicGraph::plan_`. It mirrors `NodeEventRecordsTestAccess` on `Graph`.

### `find_plan_node`, `has_boundary_edge`, `boundary_in_degree`

`find_plan_node` returns the one node of `plan.all_nodes` that carries a name and fails the test
when none or several do; the list is filled only while the graph requests profiling.
`has_boundary_edge` reports whether `from` lists `to` among its cross-region successors, and
`boundary_in_degree` returns how many cross-region edges end at a node. With them a test asserts an
edge of the boundary layer directly and needs no timing.

## test_boundary_access.cpp

The file covers the entry and exit pins `compute_boundary_access()` gives a region and the
cross-region edges the composer makes from them. The `Unit_BoundaryAccess` cases build the nodes of
one region on a `TaskGraph` with `add_native` and read the pins; they start no runtime. The
`Integration_BoundaryAccess` cases record host tasks on a profiled `DynamicGraph` over a buffer of
20 elements, run one step and assert the edges and the values.

### `AReadPastItsPredecessorsWriteKeepsAnEntryPin`

`p` writes bytes `[0,10)` and `node` reads `[5,20)`. `node` has an intra edge from `p`, yet `p` covers
only part of what `node` reads, so `node` keeps an entry pin whose read interval contains `[15,20)`.

### `AReadInsideItsPredecessorsWriteHasNoEntryPin`

`p` writes `[0,20)` and `node` reads `[5,20)`. `p` contains everything `node` reads, so the base has one
entry pin, that of `p`.

### `AWriteBehindAReadingPredecessorKeepsItsWritePin`

`p` reads `[0,10)` and `node` writes `[0,10)`. A reader takes no edge from the readers of earlier
regions, so it cannot stand in for a write: `node` keeps an entry pin with its write interval.

### `OnlyAContainingSuccessorWriteShadowsAnExit`

A write of `[5,10)` followed by a write of `[0,20)` leaves one exit pin, the later one. In the
reverse order the wide write keeps its exit pin, because the narrow one rewrites only part of it.

### `AnEarlierWriterOrdersAReaderItsPredecessorHalfCovers`

Region 1 holds `w`, which writes elements `[15,20)`. Region 2 holds `p`, which writes `[0,10)`, and
`node`, which reads `[5,20)` and sums it. The plan holds the edge `w -> queue`, `node` has a boundary
in-degree of one, and the sum is 40.

### `AReaderItsPredecessorCoversGainsNoEdge`

`p` writes the whole buffer here. The plan holds `w -> p` and no edge `w -> queue`: `node` has a boundary
in-degree of zero and is ordered behind `w` through `p`. The sum is 50.

### `AnEarlierReaderOrdersAWriterBehindAReadingPredecessor`

Region 1 holds `r`, which reads `[0,10)`. Region 2 holds `p`, which reads the same elements, and
`node`, which overwrites them. The plan holds the edge `r -> queue`, and both readers see the value the
buffer had before `node` ran.

## test_region_commit.cpp

The file covers what a `DynamicGraph` keeps when a commit of its regions throws.

### `expect_producer_orders_consumer`

`expect_producer_orders_consumer` expects the cross-region edge from the node named `a` to the node
named `c` and a boundary in-degree of one on `c`.

### `AThrowingRegionBuildKeepsTheEdgesOfTheOtherRegions`

Regions 0, 1 and 2 share one buffer: `a` writes it, `b` and `c` read it. After one run region 1 is
dropped and region 3 is added with a `Testing::RefusingFill`, a host task whose copy throws while
its flag is set and which writes a buffer of its own. The run returns `CompileFailed`,
`compose_count()` has not moved and the edge `a -> c` is still in the plan. The flag is then
cleared, the next run succeeds and composes once, the edge `a -> c` is there, and `c` holds what
`a` wrote in the second step.

A second mid-commit failure, an allocation that fails after the first region is un-wired, has no
test: the composer offers no seam that makes `std::set::insert` throw.
