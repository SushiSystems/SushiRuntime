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

## test_data_handles.cpp

The file covers what `Buffer` and `State` promise alike: what a range call does with a window, what
an allocation does with a count it cannot hold, and the queries both answer. Every handle is shared
USM on device 0, so the file needs no GPU.

### `error_of`, `expect_window_rejected`, `expect_every_range_call_rejects`

`error_of` runs a call and returns the `Core::Error` of the `SushiException` it throws, or an error
of code `ok`. `expect_window_rejected` checks one such error for `invalid_access` and for a message
that holds the call, `offset <n>`, `count <n>` and `size <n>`. `expect_every_range_call_rejects`
applies it to `read_range`, `write_range` and `region` of one handle, which is how the Buffer test
and the State test stay the same test.

### `BufferRejectsAWindowPastTheEnd`, `StateRejectsAWindowPastTheEnd`

A window `{size, 1}` starts one past the end, and `{size - 2, 5}` starts inside and runs over. All
three range calls throw `invalid_access` for both, and the message holds the offset, the count and
the size.

### `WindowWhoseEndWrapsIsRejected`

An offset or a count near the largest `std::size_t` makes `offset + count` wrap to a small number.
The window is rejected all the same.

### `RejectedWriteCopiesNothing`

A write whose window runs over the end throws and leaves every element as it was, the ones inside
the handle included.

### `EmptyWindowAtTheEndIsAccepted`

A window `{size, 0}` is valid on a Buffer and on a State: `read_range` returns an empty vector,
`write_range` accepts an empty span with a null pointer, and `region` returns a zero-length region
at the end.

### `FittingWindowRoundTrips`

The last five elements are written and read back, and the element before the window keeps its
value.

### `NullOrShortSourceWithACountThrows`

`write_range` with a count of four throws `invalid_access` for a null source, on a Buffer and on a
State, and for a source of two elements.

### `OverflowingElementCountThrowsBeforeAllocating`

The count is chosen so that `count * sizeof(double)` wraps to eight bytes, a size any allocator
grants. `Runtime::buffer` and `Runtime::state` throw `allocation_failed` with the count in the
message.

### `StateAndBufferAnswerTheSharedQueriesAlike`

`device_index`, `size`, `empty`, `residency` and `is_host_accessible` return the same on a State and
on a Buffer allocated alike, and `State::region` names a window of the current field.

## test_device_count.cpp

The file covers `API::sized_from_device`: a task whose iteration size is a count that a predecessor
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

### `RejectsACounterThatIsNotHostResident`

A counter the host cannot read at every moment of a run is rejected where it is written, not in the
middle of a run. `Device` memory is never host-addressable, and `Shared` memory is managed memory
the host may not touch while a kernel runs on some backends.

### `AcceptsAnInteriorPointerIntoAHostSlot`

A caller with one plan array per device offsets into the array and hands over the interior address.
Nothing else resolves an interior pointer to its allocation kind, so `add()` must resolve the
pointer to the allocation it lies in before judging it.

### `AcceptsAnInteriorPointerIntoAHostBaseSlot`

`based_at_device` reads its slot on the host exactly as `sized_from_device` does, through the same
check, so an offset base slot resolves the same way.

### `RefusesARawSlotThatIsNotHostResident`

The raw-pointer setters take a bare address, so the residency check at `add()` is all that stands
between a device slot and a host read of it in the middle of a run. The refusal has to say which of
the two is wrong.

### `RefusesASlotThisRuntimeNeverAllocated`

A pointer from another runtime or from the stack has no residency this context can read. That is a
different fault from a device slot, and it gets a different message.

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

A device-resident `State` steps with the fluent `add(state, fn)` rule and is seeded and read back
through its range API. The field stays resident across all steps, and only the windowed readback
touches the host.

### `SharedDefaultUnchanged`

Shared residency stays the default and is host-addressable. Its range API works alongside direct
indexing, so existing code is unaffected.

### `HostSpanIndexesSharedBuffer`

`host()` is the branch-free bulk access to shared USM: one residency check, then unchecked indexing
over the whole buffer.

### `HostAccessOnDeviceThrows`

A device-resident buffer is not host-addressable. Host indexing throws a deterministic error in
every build; it never dereferences into undefined behaviour.

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

### `DynamicGraphNodeEventsTestAccess`

`DynamicGraphNodeEventsTestAccess` is a friend used only by tests to reach `DynamicGraph::plan_`,
which has no public accessor. It mirrors `NodeEventRecordsTestAccess` on `Graph`.

### `registration_index_of`

`registration_index_of` returns the registration index of the one compiled node called `name`.
`plan.all_nodes` is populated only while the graph requests profiling (`commit()` in
`dynamic_graph.hpp`), and every test in the file relies on that.

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
- the success path of `try_run()` and the fields of `RunReport`
- the `add(fn)` overload that takes a raw `sycl::handler`
- the `add(n, fn)` range kernel overload
- the pointer-tracked `add(reads, writes, n, fn)` overload
- `Reads` and `Writes` over `Buffer::region()`, which track a sub-region
- a standalone `Buffer<T>` allocation used in a graph
- `Graph::size()`
- the `Runtime::advanced()` and `unsafe_context()` accessors
- `run(0)`

### `TryRunSuccessPath`

`try_run()` on a non-empty graph must return a successful result with `total_tasks_executed` filled
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

`add_untracked(fn)` takes a function that receives `sycl::handler&` directly, the raw kernel
overload. The kernel must run and produce the expected values.

### `RangeKernelAdd`

`add_untracked(n, fn)` takes a function that receives `(begin, end, sycl::handler&)`, the range
kernel overload. The function must see `begin = 0` and `end = N` and launch its own work.

### `PointerTrackedAddOrdering`

`add(reads, writes, n, fn)` is the overload with explicit dependency vectors. Two tasks share one
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
`dependency_key()` and read back. The test verifies the lifecycle of owning USM.

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

- `add_untracked(Extent<2>, fn)`, a multidimensional per-element map
- `add_untracked(NdRange<2>, fn)`, a tiled work-group kernel
- `add(State, Extent<2>, fn)`, a multidimensional stepped stencil
- `add(out, in0, in1, fn)`, a variadic coupled update that reads three fields
- `add(Reads, Writes, n, fn)`, the handle-based dependency wrappers
- `add(Extent, In/Out, fn)`, buffer arguments captured automatically
- `add(State, Extent with offset, fn)`, an interior-only (halo) update

### `Extent2DPerElementMap`

With `add_untracked(Extent<2>, fn)` the kernel sees a `sycl::id<2>` that covers the whole box and
writes a row-major linear index, which proves 2-D iteration and indexing.

### `NdRange2DTiledKernel`

`add_untracked(NdRange<2>, fn)` runs a tiled kernel in the work-group model. Each item writes its
global linear index, and a correct result proves that `nd_item` indexing works.

### `State2DSteppedUpdate`

`add(State, Extent<2>, fn)` is a multidimensional stepped update. Each step adds the row index to
every cell, so after S steps a cell in row y holds `S*y`.

### `VariadicCoupledThreeInputs`

`add(out, in0, in1, fn)` is a coupled update that reads three fields, `out` and two others: `next =
cur + a + b`. All reads see data of the previous step (Jacobi).

### `ReadsWritesWrappersOrdering`

With `add(Reads, Writes, n, fn)` the dependency wrappers must order a writer before a dependent
reader exactly as the raw `void*` vectors do.

### `InOutCapturedArguments`

With `add(Extent, In/Out, fn)` the captured buffer arguments give the kernel raw pointers and
register the dependency in one step. The kernel computes `dst = src * 2`.

### `InteriorOnlyHaloUpdate`

With `add(Reads, Writes, Extent-with-offset, fn)` an interior-only update must leave the one-cell
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

### `BasedAtDeviceRejectsAnUnreadableSlot`

`based_at_device` rejects a slot it could not read at dispatch, and it does so at the call site, not
in the middle of a run.

### `RawSharedSlotIsRejectedAtAdd`

The raw-pointer setter takes no buffer, so `add()` checks the pointer: a slot inside `Shared` memory
is rejected before the graph records the task.

## test_late_count.cpp

The file covers where `Recorder::resolve()` takes the live value of a `LateCount` from and when it
reads it. Every case records `record_live_count_copy`, a resolver whose fill copies the live count
into a buffer the test reads back.

### `DefaultResolvesToCapacity`

With neither a host provider nor a device slot the live count is the capacity, as in a fixed call.

### `ChangingHostCountKeepsOneCompile`

A host count that changes between runs, zero included, must not force a recompile. The capacity
fixes the plan and only the live value varies.

### `HostCountAboveCapacityFailsTheRun`

A host count above the capacity fails the run when the resolver is submitted, as an out-of-window
`based_at()` does. The fill never runs, so the output keeps its canary.

### `DeviceSlotResolvesFromHostResidentBuffer`

A `Residency::Host` device slot resolves to the value written into it, exactly as
`sized_from_device()` reads its own slot.

### `DeviceCountAboveCapacityFailsTheRunOnBothLayers`

A device count above the capacity fails the run on a default runtime and on one that forces the
scheduler. A second graph with a count that fits then runs clean on the same runtime.

### `NonHostDeviceSlotIsRejectedAtRegistration`

A device slot that is not `Residency::Host` is rejected by `Recorder::resolve()`, the way
`require_host_device_slots()` rejects a non-host slot of `sized_from_device()`. Nothing is
recorded, and the failure does not wait for the first run.

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

The file covers `NodeOptions::width`: the width a node states when it is recorded is the
`launched_width` and the `live_width` of its node event records. Each node of a rule reads a region
the node before it wrote, so `report.node_events` lands in recording order whichever layer ran it.

### `EachNodeOfARuleReportsItsStatedWidth`

Two nodes of one rule state 600 and 1. The buffers they name are oversized by 9 and by 8 elements,
so a stamp that read the size of either buffer and not the stated width fails.

### `ResolverAndDeviceSizedNodeReportTheirStatedWidths`

A resolver states a width of one and a device-sized kernel states its capacity. The `LateCount` is
left at its default, so the live count is the capacity and every work-item writes.

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
`NodeTiming` row, which carries the name given by one `name_last()`, and `rule_registration(1)`
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
`invalid_graph`, and then records and runs a new rule. The destination runs without compiling again.

### `MovedFromDynamicGraphHasNoRegions`

The same for a `DynamicGraph`. The destination also composes again when its region is recorded on,
which shows that the region still reports to the graph that owns it after the move.

### `OneObserverPerAllocation`

Sixteen rules over one buffer leave `Recorder::watched_allocation_count()` at one; a second buffer
raises it to two, and naming the first again leaves it there.

### `ADroppedBufferIsStillDetected`, `ADroppedBufferIsStillDetectedInARegion`

A buffer named by the first and the last of six rules is destroyed after a good run. The next run
is refused with `dangling_resource`, on a `Graph` and on a `DynamicGraph`.

## test_region_emitter.cpp

The file covers `add_emitter()`, the recording form that hands the caller the backing TaskGraph.
The emitters record device kernels, the way a library above the runtime does, and each kernel waits
on the dependency events it is handed.

### `record_device_task`

`record_device_task` records one `add_task` node whose work is a `parallel_for` on the queue it is
given. The tasks are device kernels, not host tasks, because a host access to a
`Residency::Shared` buffer while a kernel runs on the same GPU faults on Windows with CUDA; see
`docs/reference/KNOWN_ISSUES.md`.

### `EmittedNodesOrderAgainstANeighbourRegion`

One emitter records two kernels on a buffer, a fill and a tripling, and a higher-keyed region reads
that buffer through an ordinary `add()`. The consumer sees the tripled values, so the two emitted
nodes ran in their recorded order and the boundary edge ordered the consumer behind them.

### `AnEmitterIsOneRule`

An emitter that records two nodes reports a region size of one and a graph size of one: `size()`
counts an emitter once, however many nodes it adds.

### `AnEmitterReplaysOnlyWhenItsRegionIsRebuilt`

The emitter counts its own calls. Three replayed steps call it once, rebuilding the neighbouring
region leaves the count at one, and dropping and recording its own region again raises it to two.

### `AStaticGraphTakesAnEmitter`

A static `Graph` records an emitter beside an ordinary `add()` that reads the emitter's buffer.
Four steps advance both, so the form is not specific to regions and survives a chained replay.

## test_rule_registration.cpp

`Graph::rule_registration` is the only way a caller can read the node events of a `RunReport`. The
events number nodes by registration index, and the rule order a caller thinks in is a different
sequence. These cases check the mapping between the two.

`record` adds three plain kernels and then `record_grouped_rule`, three kernels inside one
`Recorder::rule()` scope, each reading what the one before it wrote. The graph therefore holds four
rules and six nodes, and `last_rule()` gives the position of the scope.

### `ARuleCarriesNoIndexUntilTheGraphCompiles`

`size()` is four, `node_count()` is six, and every rule reports `no_registration` before the first
run.

### `ARuleScopeTakesOneIndexOfItsOwn`

Each plain `add()` takes an index of its own. The scope takes one more, which no plain kernel has,
and exactly three node events carry it. A position past the last rule reports `no_registration`.

### `ARulesNodesReportOneIndexOnEveryStep`

Over `run(4)` the report holds one event per node per step, and on every step exactly three events
carry the index of the rule.

## test_run_report_reuse.cpp

The file covers the overloads that take a caller-owned `RunReport`: `run(RunReport&)`,
`try_run(RunReport&)` and `RunHandle::wait(RunReport&)`.

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

The non-throwing form returns the report it was given, so a caller can keep using the result of the
call without holding both.

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

### `TheDeprecatedTwoArgumentCreateStillWorks`

The two-argument `create(devices, config)` must build what the one-struct form builds. The call sits
between pragmas that silence the deprecation warning, because the tree compiles with `-Werror`.

### `AMovedRuntimeKeepsItsContext`

A move hands the context over and does not build a second one.
