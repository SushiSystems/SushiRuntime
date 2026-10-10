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

An emitter that records two nodes reports a region size of one and a graph size of one. `size()`
counts rules, as it does for a reduction that lowers to several nodes.

### `AnEmitterReplaysOnlyWhenItsRegionIsRebuilt`

The emitter counts its own calls. Three replayed steps call it once, rebuilding the neighbouring
region leaves the count at one, and dropping and recording its own region again raises it to two.

### `AStaticGraphTakesAnEmitter`

A static `Graph` records an emitter beside an ordinary `add()` that reads the emitter's buffer.
Four steps advance both, so the form is not specific to regions and survives a chained replay.

## test_rule_registration.cpp

`Graph::rule_registration` is the only way a caller can read the node events of a `RunReport`. The
events number nodes by registration index, and the `add()` order a caller thinks in is a different
sequence. These cases check the mapping between the two.

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

## numeric/

### test_counting_sort.cpp

The file covers `Graph::add_counting_sort()`. Its design is sections A3 and B of
`docs/archive/superpowers/specs/2026-09-16-scan-and-counting-sort-design.md`, and it was built as
task 4 of `docs/archive/superpowers/specs/2026-09-16-scan-and-counting-sort-plan.md`. The tests
check stability against `std::stable_sort`, the digit-count boundaries a live key range crosses,
late-bound replay with one compile, and that every internal pass of one call shares one `node_index`
across steps.

#### `HostReference`

`HostReference` is the host reference for every case in the file: a stable sort of (key, index)
pairs by key alone, plus the bucket offsets and counts that follow from it. It is the ground truth
`add_counting_sort()` is checked against, and it does not copy the algorithm under test.

#### `StableAgainstHostReferenceOnDuplicateKeys`

The test checks stability against `std::stable_sort` on seeded duplicate keys. A comparison against
`std::sort` proves the case is not vacuous: a plain unstable sort must be able to disagree with the
stable reference on the same input, or the test would pass whether or not `add_counting_sort()` were
stable.

#### `StableAcrossMultipleDigitsWhenTheLowDigitTies`

The test checks stability across digits. Every key has the same low byte, so the first, least
significant digit pass ties every element, and only the second digit pass tells them apart. The same
`std::sort` comparison as in `StableAgainstHostReferenceOnDuplicateKeys` proves that the tie
matters.

#### `LargeRandomCaseAgainstHostReference`

The large case is seeded and random. n = 10^6 with a 2^20 key range needs more than 256 tiles (10^6
/ 256 is about 3907) and three digit passes. It is the first size and range in the suite that
crosses both boundaries at once.

#### `ZeroElementsWritesZeroEverywhere`

With zero elements, every bucket starts and stays at the live count, which is zero, and nothing
beyond the declared window is touched.

#### `MaximumKeyOfA20BitRange`

The maximum key of a 2^20 range catches a digit-count or mask error at the top bit, because
`key_range.capacity - 1` sets exactly bit 19.

#### `DigitCountFollowsKeyRangeCapacity`

A live key range at or below 2^16 must take two digit passes, and one above it three. Each digit
block is a histogram, a column scan, a bin base and a placement, so the node count the call emits
grows by exactly four per extra digit.

#### `LateBoundChangingCountAndKeyRangeKeepsOneCompile`

A late-bound element count and a late-bound key range that both change between runs must not force a
recompile. The capacity fixes the topology; only the live values and the buffers they describe vary.

#### `LiveKeyRangeBelowCapacityLandsInIndices`

A key range capacity of 2^24 plans three digits. Live key ranges that need one, two and three digits
must each leave the stable order in the indices, whichever digit turns out to be the last one that
runs.

#### `DigitsAboveTheLiveKeyRangeDoNotRun`

The upper digits are skipped. A key that breaks the precondition of the live key range but fits the
capacity is sorted by its low byte alone, which could not happen if the digit above it still ran.
The test is white-box by design; a caller must never pass such a key.

The key 0x0101 has low byte 1, so one digit puts it between 0 and 2, and two digits would put it
last.

#### `RegionsRecordScanAndCountingSort`

A `DynamicGraph` region records the prefix scan and the counting sort exactly as a `Graph` does,
each in its own region, with live counts that change between runs.

#### `EachRunSortsItsOwnKeys`

The test covers substeps: `run(4)` with keys that change every step, so the sort of each step must
reflect the buffer as it stood when that step ran.

#### `SharesOneNodeIndexAcrossStepsAndPasses`

Every internal pass that one `add_counting_sort()` call emits shares one `node_index`, and
`run(steps)` records one invocation per step for each pass. The node event records are specified in
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`.

`key_range.capacity = 50` needs one digit, because 50 - 1 fits in 6 bits. The call therefore emits 1
node for the resolver, total and digit plan, 4 for the one digit block, 1 for the offsets and 1 for
the counts: 7 nodes.

#### `InternalNodeWidthsMatchTheDispatchTable`

The `launched_width` of each internal pass must be its own row of the width table; a test that only
checked that some width was stamped would prove less. Every pass reads a region the pass before it
wrote: `plan_region`, then `hist_region`, then `col_total_region`, then `bin_base_region`, then the
shared indices buffer, then the offsets. `report.node_events` therefore lands in that dependency
order whichever execution path ran the passes.

The shape is that of `SharesOneNodeIndexAcrossStepsAndPasses`: `key_range.capacity = 50` needs one
digit, `T_cap = next_live_tile_count(300) = 2` and `DIGIT_BINS = 256`. The node order is resolver,
histogram, column scan, bin base, placement, offsets, counts.

#### `LateBoundCountAboveCapacityFailsTheRun`

A live element count above its capacity fails the run. The alternative is a read or a write past the
buffers the sort was planned for.

#### `LateBoundKeyRangeAboveCapacityFailsTheRun`

A live bucket count above its capacity fails the run too. The offsets pass never reads the live key
range (section A3 of the design document), but the `key_range` resolver still validates it on every
run.

### test_device_sized_primitives.cpp

The file covers phase P3 of `docs/design/GRAPH_REPLAY.md` (sections D.2 and G).

On every step a producer kernel writes a new live count, 0 and the capacity included. The scan, the
counting sort, the reductions and a `sized_from_device` task size themselves from that count on the
device. The chain and the forced scheduler must record the same per-step digests bit for bit.

### test_fixed_order_reduce.cpp

The file covers `Graph::add_reduce`, `Graph::add_segmented_reduce` and the fixed-tile plan under
them. A parallel reduce also gets the sum right, so a correct sum is not the claim under test. The
claim is that the combination order is a function of the element layout alone, which makes the bit
pattern reproducible.

The strongest available check is a host reference that performs the identical fixed-tile fold,
compared bit for bit. If the device result matched some other order, the two would differ in the low
bits of a float sum. The inputs are built so that the naive left-to-right sum differs from the tiled
one, and that difference is what lets the comparison fail.

#### `fold_level`

`fold_level` is the host mirror of `detail::fold_tile` over a whole level. It folds each tile of at
most `TILE` values left to right, seeded with the first element of the tile.

#### `fixed_order_sum`

`fixed_order_sum` returns the value the runtime is required to produce for a sum: repeated levels of
`fold_level` until one value remains. It mirrors `detail::plan_reduction`.

#### `order_sensitive_input`

`order_sensitive_input` builds a sum whose result depends on the order in which the values are
combined: one large magnitude, followed by many values small enough to vanish when added to the
running total but not when added to each other first.

#### `bit_equal`

The `double` overload of `bit_equal` does for `double` what `bit_equal(float, float)` does for
`float`. The byte-identity checks need it, because `EXPECT_DOUBLE_EQ` tolerates a difference of 4
ULP and the late-bound contract is byte identity.

#### `PlanLayoutIsPureAndDisjoint`

The plan is a pure function of the element count. Level counts divide by the tile size until one
value remains, and consecutive levels never share scratch.

#### `SumMatchesTheFixedOrderReferenceExactly`

The sum matches, bit for bit, a host fold with the identical tile structure. It also differs from
the naive left-to-right sum, which proves that the comparison discriminates between orders.

#### `ResultIsStableAcrossSchedules`

Repeated runs of the same graph, and a fresh graph on a runtime that runs the background rebalancer,
must all agree bit for bit. Work stealing and the rebalancer change which worker folds which tile,
and neither may change the result. The test varies the schedule at a fixed pool size. Varying the
pool size is the job of the conformance suite `Integration_Determinism`.

The second runtime takes the same input with the rebalancer on. That is a different scheduling
regime, because task migration reassigns tiles to workers in the middle of a run.

#### `EdgeCounts`

The test uses element counts around the tile boundaries and the degenerate ones: a single value, an
empty reduction (which yields the identity), and counts that land exactly on a level boundary.

#### `MinimumAndMaximum`

`Minimum` and `Maximum` reduce with the same machinery. Their identities must let any element win.

#### `ConsumerSeesTheFinishedResult`

A reduction is an ordinary tracked task. A consumer that reads the output is ordered after the whole
reduction chain, not only after its first level.

#### `SegmentedReduceOverRaggedSegments`

The segmented reduce runs over ragged segments, one of them empty. The empty segment must yield the
identity and must not read past its zero-length window.

#### `RejectsMisSizedArguments`

Arguments of the wrong size are rejected when the task is recorded, at the call site. They do not
fault on the device in the middle of a run.

#### `LateBoundMatchesFixedCountExactly`

A `LateCount` reduce planned for a capacity must match the fixed-count overload bit for bit at every
live count tested. The design promises the same fold order for a live n as the fixed-count path
would produce for that n.

#### `LateBoundChangingCountKeepsOneCompile`

A late-bound reduce whose live count changes between runs must not recompile. The topology (levels,
scratch and node count) is fixed at the capacity, and only the live count varies per step.

#### `LateBoundAboveCapacityFailsTheRun`

A `LateCount` above its capacity fails the run. The alternative is a read past the buffers the
reduce was planned for.

### test_late_count.cpp

The file covers the tiled level plan (`detail::plan_tiled_levels` and `live_tile_levels` in
`api/detail/fixed_order_scan.hpp`) and the one resolution point of `LateCount`
(`detail::resolve_late_count` in `api/graph/graph.hpp`). Both are pure host functions, so the suite
needs no device and no `Runtime`.

#### `LevelCountsAtTileBoundaries`

The level count for a capacity is the tree that `add_reduce` already plans. The test exercises it at
the boundaries the tile size creates.

#### `LiveCountBelowCapacityMatchesThatCountAtCapacity`

A live count below the capacity must fold into the tile boundaries that the same count would get as
a capacity. The levels below the point where the live chain reaches one value line up, index for
index, with `plan_reduction(n)`.

Every level past what `plan_reduction(n)` needs is a pass-through of a single element, and folding
it changes nothing.

#### `DefaultResolvesToCapacity`

`resolve_late_count` is the one resolution point. With neither a host provider nor a device slot,
the live count is the capacity, as in a fixed call.

#### `AboveCapacityIsRejected`

A live count above the capacity fails the run, as an out-of-window `based_at()` does. It is neither
clamped silently nor allowed to read past the end.

#### `DeviceSlotResolvesFromHostResidentBuffer`

A `Residency::Host` device slot resolves to the value a predecessor wrote into it, exactly as
`sized_from_device()` reads its own `device_count` slot.

#### `NonHostDeviceSlotIsRejectedAtRegistration`

A device slot that is not `Residency::Host` is rejected at registration, the way
`require_host_device_slots()` rejects a non-host slot of `sized_from_device()`. It does not wait to
fail on the first run of the graph.

#### `RadixDigitCountAtByteBoundaries`

The test checks digit counts at the 8-bit boundaries. A key range of 256 still fits one digit and
257 needs two. Zero and one take one digit.

#### `RadixPlanSkipsDigitsAboveTheLiveKeyRange`

A plan for three digits whose live key range needs fewer runs only the low digits, and the last
active digit always places into the indices.

### test_late_count_ordering.cpp

The file covers the ordering claim behind the device slot of `LateCount`, which came out of the
second fix round of `docs/archive/superpowers/specs/2026-09-16-scan-and-counting-sort-plan.md`. A
count sourced from the device must be resolved when the consuming node is dispatched, after its
producer. It must not be polled at the step boundary, before the producer has run. A graph whose
resolver dropped the device slot from its `Reads` would still compile and run, but it would fold the
count of the previous step and not the one just written. The test is built to catch that regression.

#### `ReduceSeesTheCountItsPredecessorJustWrote`

A producer node writes a fresh live count into a `Residency::Host` slot on every run, and a
late-bound `add_reduce` reads the same slot through `LateCount::device` in the same run. The reduce
must see the count the producer wrote in this run.

The producer reads its next count from a buffer and not from a host stack variable, because the
kernel may run on the device, which cannot reach the stack frame of the test.

### test_prefix_scan.cpp

The file covers `Graph::add_prefix_scan()`. Its design is sections A1 and B of
`docs/archive/superpowers/specs/2026-09-16-scan-and-counting-sort-design.md`, and it was built as
task 3 of `docs/archive/superpowers/specs/2026-09-16-scan-and-counting-sort-plan.md`. The tests
check exact agreement with a host reference at the tile edges, the exclusive contract (`offsets[n]
== total`), late-bound replay with one compile, and that every internal pass of one call shares one
`node_index` across steps.

#### `host_exclusive_scan`

`host_exclusive_scan` is an exclusive prefix sum over `uint32`, modulo 2^32, which is the contract
of the scan: `offsets[i]` is the sum of `counts[0..i)`, and both `offsets[n]` and the return value
carry the grand sum.

#### `CANARY`

`CANARY` is a sentinel that a real scan would never produce. A slot that keeps it was not written,
and a slot that loses it was written when it must not have been.

#### `ExactAgreementWithHostReferenceAtTileEdges`

The tile edges are where an off-by-one in the level plan or in the base of the down-sweep would
show: 0, 1, one below, at and above a tile, one below, at and above the second level, and 10^6.
`offsets` and `total` are filled with the canary before the run, and `offsets` carries one extra
slot past `capacity + 1` that must survive untouched. Neither a stale zero nor a write past the
declared window can then pass by accident.

#### `OffsetsAtLiveCountEqualsTotal`

The second half of the exclusive contract: `offsets[n]` is exactly the sum that `add_prefix_scan()`
also publishes through `total`, not an approximation of it.

#### `LateBoundChangingCountKeepsOneCompile`

A live count that changes between runs must not force a recompile. The topology (levels and scratch)
is fixed at the capacity; only the live count and the counts it scans vary per run.

#### `EachRunScansItsOwnCounts`

When the counts change between calls, and not only the live count, a run must scan what is in the
buffer at the time it runs, not a stale copy.

#### `SharesOneNodeIndexAcrossStepsAndPasses`

Every internal pass that one `add_prefix_scan()` call emits (the late-count resolver, the up-sweep
levels, the down-sweep levels and the closing write) shares one `node_index`, and `run(steps)`
records one invocation per step for each of them. The node event records are specified in
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`.

`plan_tiled_levels(600)` gives two levels: level 0 folds 600 into 3 and level 1 folds 3 into 1. The
call therefore emits 1 node for the resolver, 1 for the up-sweep (every level but the top), 2 for
the down-sweep (every level) and 1 for the closing write: 5 nodes.

#### `InternalNodeWidthsMatchTheDispatchTable`

The `launched_width` of each internal pass must be its own row of the width table; a test that only
checked that some width was stamped would prove less. Every pass reads a region the pass before it
wrote: `state_region`, then `up_scratch`, then `down_scratch`, then the whole-buffer
write-after-write on `offsets`. `report.node_events` therefore lands in that dependency order
whichever execution path ran the passes.

The shape is that of `SharesOneNodeIndexAcrossStepsAndPasses`: `plan_tiled_levels(600)` gives level
0 from 600 to 3 and level 1 from 3 to 1. The node order is resolver, up0, down-top, down0, closing.

#### `ZeroCapacityWritesZeroTotal`

With capacity zero the live count is always zero, so `total` and `offsets[0]` are always zero. The
buffers start from the canary, so a zero that was already there cannot pass for a written one.

#### `LateBoundAboveCapacityFailsTheRun`

A live count above its capacity fails the run. The alternative is a read or a write past the buffers
the scan was planned for.

### test_reduce_dispatch_width.cpp

The file covers the dispatch widths of `Graph::add_reduce()` and `Graph::add_segmented_reduce()`, in
the style of `InternalNodeWidthsMatchTheDispatchTable` in `test_prefix_scan.cpp`.

#### `EagerReduceLevelWidthsMatchTheDispatchTable`

`plan_reduction(600)` at `REDUCE_TILE = 256` has two levels: level 0 folds 600 values into ceil(600
/ 256) = 3 partials, and level 1 folds those 3 into ceil(3 / 256) = 1. Each level is its own node,
so the launch width per node is the `out_count` of that level, in level order.

#### `EagerReduceZeroCountStampsWidthOne`

`n == 0` skips every level and emits one node that writes the identity. That node still has a real
launch width of one, not `WIDTH_UNKNOWN`.

#### `LateBoundSegmentedReduceWidthsMatchResolverAndCapacity`

A `LateCount` left at its default, with no host provider and no device slot, behaves like a fixed
count equal to its capacity, which is the documented fallback of `detail::read_live_count()`. The
late-bound overload of `add_segmented_reduce()` therefore emits its resolver, a `single_task` of
width one, followed by its per-segment node, whose width is the capacity.

`segment_offsets` and `output` are each oversized past `segment_count` by a different amount. A
width stamp that read the size of either buffer, and not the launch range, would then fail and could
not pass by accident.

#### `EagerSegmentedReduceWidthMatchesSegmentCount`

The fixed-count overload emits one node over exactly `segment_count` elements, so its launch width
is `segment_count`.

The buffers are oversized as in `LateBoundSegmentedReduceWidthsMatchResolverAndCapacity`:
`segment_offsets` and `output` each carry a different amount of padding past `segment_count`, which
pins the stamp to the launch range and not to either size.
