# api tests: graph regions

The tests of `DynamicGraph` and `Region`: adding, dropping and composing regions.
They are part of `sr_api_tests`; [the page of the program](../../README.md) says how to run it.

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
