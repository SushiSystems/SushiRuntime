# api tests: graph

The tests of `Graph`, `DynamicGraph`, `Region`, `Recorder`, `PlanView` and `RunHandle`. They are part of `sr_api_tests`;
[the page of the module's tests](../README.md) says how to run them.

## benchmark_dynamic_graph.cpp

A scale probe for the dynamic region graph: 256 regions stream in, 16 of them mutate between
steps, and the world keeps running correctly. The test asserts the composition contract at
scale, one recompose per mutation and not one per region, and reports the streaming cost. It asserts no recompose speedup and guards
correctness and scale.

The final check rebuilds the expected count per cell. Every region ran the initial step plus
one step per run, and a mutated region gained one more increment on each run from the one that
streamed its second task in.

The gate benchmark for the "allocation-per-node churn on the recompose path" theme, audit
Tier B. The substrate commits work proportional to the changed region every step: a dirty
region is rebuilt, and each rebuilt node pays a `Node` allocation, two IO-region vectors, an
`AccessNode` per access, event constructors and build list nodes. The benchmark isolates that
cost in two ways.

Commit wall time is the portable headline. A commit is one `region().add()` that marks a
region dirty and one `run(1)` that recomposes and steps. Compared with a `run(1)` that has no
mutation, which is pure replay, the delta is the recompose cost.

Allocations per commit come from a gated global `operator new` counter. The count is
authoritative on a single-CRT build, Linux and CI, where it sees allocations inside
libsushiruntime too. On a Windows DLL build it counts only allocations on the executable's
side, from headers and templates, so trust the wall-time delta there and the allocation count
on the CI lane.

The counter is off by default and during every other test, so the process-wide replacement
costs one relaxed load per allocation outside the measured windows. One replacement serves the
whole benchmark binary and routes everything through `malloc` and `free`. This replacement is
also why an AddressSanitizer build leaves the benchmark suite out; see `cli/README.md`.

The test asserts correctness and the composition contract, one recompose per mutation, and
never a speed or allocation threshold. The numbers are reported for the owner to compare
across the B2 changes. If the recompose allocations or milliseconds dominate, the node,
`AccessNode` and event churn is the target. If the pure-replay step already allocates heavily,
the effort belongs in steady-state execution.

The closing loop is a smoke check. Every region ran the warm step, the replay-baseline steps
and one step per commit, and a mutated region gained an extra increment on and after the
commit that streamed its second task in. The test asserts forward progress and the composition
contract and does not rebuild the exact count per cell, which the `Medium_DynamicGraph`
tests own. A nonzero value that is uniform across a region is enough here.

## benchmark_graph.cpp

This is the baseline for the graph-replay design, `docs/design/GRAPH_REPLAY/README.md`, sections 0 and
G (P0). Node i reads buffer i and writes buffer i+1, so every node depends on the one before it
and the scheduler cannot dispatch two nodes at once. The run therefore pays the scheduler's
whole per-node dispatch path, which is dependency resolution, submit, poll and the hand-off to
the successor, with no fan-out to hide it behind. The benchmark sweeps the chain length K and
reports milliseconds per node. Layer 0, which removes the per-node allocation in
`SmallEventVector::to_vector()`, and the later layers are measured against that number.

The gate benchmark for the batched root injector, audit ds-1. Each replayed step re-injects
every root node. `submit_batch` takes a step's whole root set in one lock acquisition, one
`inflight_` increment and one wake burst, so the injection cost per step does not grow with
root fan-out.

The benchmark builds R independent roots, each writing its own buffer, replays a fixed number
of steps, and sweeps R. It reports milliseconds per step and the amortized cost per root and
step. Injection is O(1) per step whatever R is, so as R grows the per-root cost should fall
towards the cost of executing and completing one task, and should not stay pinned to a lock
and a notify per root. Correctness and forward progress are asserted; speed is not.

## test_device_sized.cpp

The file covers the launches of phase P3 of `docs/design/GRAPH_REPLAY/README.md` (sections D.2 and G): a
`Recorder::kernel(DeviceSized, ...)` and a `sized_from_device` task, both sized by a count a
predecessor wrote in the same step.

### `record_marking_rule`

`record_marking_rule` is the rule every case but two records. A resolver publishes two live words
into recorder scratch, the width and a salt, and a device-sized kernel folds its index and both
words into `marks[t]` for the work-items below the width. A word at or past the width keeps what an
earlier step left, so the bytes after a step depend on every width so far. `marked` is the host
model of one such write.

| Test | What it proves |
| --- | --- |
| `OnlyItemsBelowTheLiveWidthRun` | Widths of 10, 0, the capacity and 257 run in turn on one compiled plan. After each run every word must equal the host model, and the word one past the capacity must still hold the canary. A launch that ignored the live width, or read the width of the previous run, fails on the first comparison. |
| `ANullLiveWidthRunsEveryItemAndTheArityPicksTheWords` | A null `live_width` lets every work-item run. A callable of one argument gets the index alone and one of two arguments gets `live_words[0]` after it. `record_marking_rule` covers the callable of three arguments. |
| `ZeroCapacityLaunchRunsNothingAndReleasesItsSuccessor` | A zero-width `parallel_for` is not portable, so a launch of capacity zero submits an empty task. The node must still complete: a successor that reads the same buffer copies the untouched canary. |

### `DeviceSizedRuleChainMatchesSchedulerAsTheCountsChange`

On every step a producer kernel writes a new width, 0 and the capacity included, and the host hands
the resolver a new salt. The chain and the forced scheduler must record the same digest of the
whole buffer on each of 48 steps, bit for bit, and the digest must change on more than a quarter of
them.

| Test | What it proves |
| --- | --- |
| `SizedFromDeviceChainMatchesSchedulerAsTheCountChanges` | The same comparison for a `sized_from_device` task, whose live size comes from the same counter. |
| `DeviceCountOverCapacityFailsAChainedRun` | A producer writes a count one above the capacity. The resolver cannot throw from a kernel, so it writes a fault record and the run fails when the step ends. A second graph on the same runtime, with a count that fits, runs clean on the chain. |

## test_dynamic_graph.cpp

The file covers the dynamic, region-partitioned graph (`Runtime::dynamic_graph()`). Each test
asserts the numerical outcome and the composition behaviour: regions run together, data dependencies
across regions order correctly, dropping a region removes its work, and a run without a mutation
never recomposes.

| Test | What it proves |
| --- | --- |
| `IndependentRegionsRunAndComposeOnce` | Two independent regions each own a buffer and increment it once per step. Over several steps both buffers advance, and a run that mutates nothing composes the combined plan exactly once. |

### `CrossRegionDependencyOrders`

A producer region writes `x`, and a consumer in a different region reads `x` and writes `y`. Both
touch `x`, so the composer must wire an edge across the regions for the consumer to see the
producer's writes: `y[i] == 2*(i+1)`. Ordering across regions follows the ascending region key, so
the producer has key 10 and the consumer key 11, as the documented contract requires.

| Test | What it proves |
| --- | --- |
| `DropRegionStopsItsWork` | Dropping a region removes its work at the next step boundary. After the drop the surviving region keeps advancing, and the buffer of the dropped region freezes. |
| `AddRegionBetweenSteps` | Adding a region between steps recomposes exactly once, and the work of the new region takes effect on the next run. |
| `SingleRecomposePerMutation` | Mutating one region out of many recomposes once, and the unchanged regions are not rebuilt. The proof is indirect: the results stay correct, and `compile_count` advances by exactly one per mutation, not per region. |
| `FindRegionReadsWithoutRecomposing` | Reading a region through `find_region()` is not a mutation. The plan must not recompose on the next run, and an absent key returns `nullptr` where `region()` would create the region. |

### `ThreeRegionChainThreadsValue`

The chain has three hops across three buffers: region 0 writes `x`, region 1 reads `x` and writes
`y`, and region 2 reads `y` and writes `z`. Each hop is a separate boundary. The composer must order
the regions 0, 1, 2, so that `z` carries the value threaded through both hops: `z[i] == 2*(i+1) +
1`.

| Test | What it proves |
| --- | --- |
| `ReadReadSharingStaysConsistent` | One producer writes `base`, and two consumer regions both read `base` and write their own outputs. The two readers share no hazard, so the composer must not order them against each other; both see the producer's writes. The observable is correctness: the output of each reader reflects `base`. |

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

The file covers the per-graph profiling switch of `DynamicGraph` and its registration index, which
is dense and ordered by ascending region key and then by `add()` order at every recompose. The
design is sections D, E (item 8) and F.6 of
the g1 node event records design spec in the version history, built as task 6 of
the g1 node event records plan spec in the version history.

The file reaches the plan through `dynamic_graph_plan_access.hpp`.

| Test | What it proves |
| --- | --- |
| `registration_index_of` | `registration_index_of` returns the registration index of the one compiled node called `name`, which `find_plan_node` looks up. `plan.all_nodes` is populated only while the graph requests profiling (`publish_plan()` in `region_composer.hpp`), and every test in the file relies on that. |
| `NotRequestedRecordsNothing` | A graph that never requested profiling records nothing, whatever the default of the runtime is. |
| `RequestedGraphProducesRecordsMatchingRenumberedIndex` | A `DynamicGraph` that requested profiling must produce records; a correctly stamped flag is not enough. It needs its own call to `CompiledGraph::capture_profiling_state()`, because it has no `capture_base_state()` of its own that could make the call for it. |

### `IndexDenseAndOrderedByRegionThenAddOrder`

With two regions that each make more than one `add()` call, `node_index` is dense and ordered by
ascending region key first and by `add()` order within a region second.

Region 5 is added first but has a higher key than region 2, so its two calls must land after the one
call of region 2 once the order follows the key.

### `IndexStaysDenseAfterDropAndReAdd`

Dropping a region and adding it again must not leave a gap in the index space. The next recompose
renumbers every live region densely from zero.

The file compares whole output buffers byte for byte and uses the public API only, so it holds
across a change of internals. All data is `std::uint32_t`.

Four shapes are covered: the same work as regions of a `DynamicGraph` and as one static `Graph`,
with a region dropped and recorded again; host tasks among kernels in one dependency chain;
steps through `run_async()` against `run(n)`; and a 2-D box with an origin offset through each
launch form. Where a layer choice applies, the chain layer is compared with the forced scheduler
layer, and the offset launches also with a reference computed on the host.

`NdRange` has no origin, so its kernels add the offset to the global id themselves.

`UntrackedOffsetLaunchesMatchAcrossLayers` places its untracked kernels on the last device of the
runtime with `On`.

## test_graph.cpp

The file covers the native CPU execution path, work package WP-7 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN/`. `add_host()` runs plain C++ on a worker thread
with no SYCL submission. The task is ordered into the graph by the data it touches and shares one
completion contract with kernel tasks.

| Test | What it proves |
| --- | --- |
| `HostTaskRunsAfterKernel` | A native host task ordered after a device kernel must see the kernel's output. The task is dispatched only once the kernel's event has completed, so the shared USM the kernel wrote is visible on the host. The ordering comes from the shared buffer `a`. |
| `KernelRunsAfterHostTask` | A device kernel ordered after a native host task must see the host's writes. The kernel waits on the inline completion of the host task before it is dispatched, and shared USM written on the host is visible on the device. |
| `UntrackedHostTaskRunsEveryStep` | An untracked native task runs once per step, which proves that the form without dependencies is both a root and a leaf and replays on every step. |

### `ThrowingHostTaskKeepsRuntimeUsable`

A native task that throws fails the run, which is the error isolation of work package WP-0, and the
runtime stays usable: a fresh graph runs cleanly afterwards. Native tasks therefore share the kernel
error path and do not latch the worker pool dead.

The file covers per-dispatch node event records, the clock pairing and steady-state capture. The
design is section C and items 1, 2, 3, 4 and 6 of section E in
the g1 node event records design spec in the version history, built as task 5 of
the g1 node event records plan spec in the version history. The item numbers in the
subsections refer to section E of the design.

| Test | What it proves |
| --- | --- |
| `NodeEventRecordsTestAccess` | `NodeEventRecordsTestAccess` is a friend used only by tests to reach the compiled plan of a `Graph`. It mirrors `RegistrationIndexTestAccess` in `graph/test_recorder.cpp`. |
| `RequestedGraphRecordsOnePerDispatch` | Item 2: three independent kernel nodes and `run(5)` give fifteen records, invocations 0 to 4 per node, and `submit <= start <= end` for every record. |

### Steady-state capture

Item 3 is tested in `test_sushiruntime.cpp`, which is built into the
`sr_api_allocation_tests` executable and so runs in its own process. A replacement `operator new` in
`graph/test_graph.cpp` would see only the allocations of its own executable and never those
of the shared library. The opening of this file explains why that test needs its own binary.

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

The file covers the overloads that take a caller-owned `RunReport`: `run(RunReport&)`,
`run(RunReport&)` and `RunHandle::wait(RunReport&)`.

The tests pin down two properties that pull against each other. The overloads exist so that a frame
loop keeps one report alive and the per-run vectors stop being reallocated, so the report must not
be rebuilt from scratch. A report carried across ticks must also never let the numbers of the
previous run survive into the current one, and not rebuilding invites exactly that. The resolution
is to reset and then refill: the capacity stays and the values do not. Both halves are asserted,
also on the failure paths, where stale numbers would mislead most: a caller that ignored the
returned error would otherwise read a report that looks successful.

| Test | What it proves |
| --- | --- |
| `record_write` | `record_write` zeroes `buf` and records the one task every test in the file runs: element i gets i+1. It takes both handles by reference and does not return them, because a `Buffer` must not be moved once a graph has recorded it; the graph would keep pointing at the old address. |
| `ReuseReportsWhatTheValueFormReports` | The reuse overload must report what the value-returning one reports. If the two drivers ever diverged, every number a consumer reads from the cheap path would silently become a different measurement from the documented one. |
| `SteadyStateRunsDoNotReallocateTheReport` | The overload exists so that the per-run vectors are refilled in place. Comparing the address of the row before and after proves that no reallocation happened. A capacity check alone would pass even if the vector were rebuilt into a new block of the same size. |
| `EachRunOverwritesTheLastOne` | Reuse must not accumulate. A short run that follows a long one has to read as the short run, because the count belongs to the run, not to the report. |

### `ARejectedRunLeavesNoStaleNumbers`

A rejected run must clear the report it was handed. The dangerous case is a caller that checks the
returned error only sometimes. Reading a stale `is_successful` of true from a run that never
happened is worse than reading zeroes, so the failure path resets the report and marks it
unsuccessful.

| Test | What it proves |
| --- | --- |
| `TryRunReturnsTheCallersReport` | `run(report)` returns a pointer to the report it was given, so a caller can keep using the result of the call without holding both. |

### `AsyncWaitFillsTheCallersReport`

The asynchronous path settles into a caller-owned report too. The overlapped frame loop is the one
that most wants the overload.

A run must be cancellable from another thread. An otherwise unbounded `run_until` has to return
promptly when a second thread calls `cancel()`, and the pool must stay usable so that a later run
completes normally. Cancellation is reset per run; it does not latch.

The file covers layer 1 of `docs/design/GRAPH_REPLAY/README.md` (sections C and G, phase P1).

A chainable plan runs as an in-order chain, matches the scheduler bit for bit, and names the path it
took in `RunReport::layer`.

### `ANamedNodeRunsOnTheChainWithNoClusterAttached`

A kernel and a State rule each name an operation and one parameter in their `NodeOptions`. No
cluster is attached, so nothing can take either node. The run of three steps must report
`ExecutionLayer::Chain` and leave the values an unnamed graph leaves: a named node is a candidate
for another machine only where one can run it.

The file covers the reusable completion latch, section 5.1 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN/`. `execute()` allocates no promise and no future
per step. One latch on the engine is reset, armed and waited on each step. These tests pin the
behaviour the latch must preserve: a long replayed run drives every step and reaches the right
state, and the same plan re-runs cleanly afterwards, which requires the latch to reset and the
sentinel to re-wire.

| Test | What it proves |
| --- | --- |
| `ReRunsCleanlyOnTheSamePlan` | The test makes three separate `run()` calls on one compiled plan. The latch must reset between them, and the sentinel, which is wired once, must keep signalling. |

### `SingleStepRunsRepeatedlyWithoutLeak`

Many independent single-step runs stress the reset, arm and wait cycle hardest. Each run allocates
nothing on the completion path.

A single kernel cannot dereference USM from two device contexts that are not peers. The scheduler
detects such a node on its first dispatch and fails the run with a precise error, before the node
can make an illegal cross-context access. The guard is section 5.4 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN/`. Splitting data together with the work across
devices belongs to work package WP-5 of the same document, and until that is built the guard
enforces the co-location contract. The check needs at least two device contexts, so the test skips
on a single-device machine.

### `NodeSpanningTwoContextsFailsCleanly`

The node reads data in context 0 and writes data in context 1, and no single queue can run it
correctly.

The run fails with a clear error and not with an illegal memory access, and the runtime stays usable
afterwards, which is the error-isolation contract.

The file is the determinism conformance suite: the same graph on the same machine must produce
byte-identical output however many workers ran it.

Rollback netcode and replay files are built on this guarantee, and two independent things can break
it. The scheduler can: if a node could start before a node it reads from had finished, or a
work-item could see a neighbour's partial result, a two-worker run and a sixteen-worker run would
disagree in the low bits and nothing would report an error. The compiler can: fast-math licenses reassociation, so the same source may be
folded in a different order in two builds, and FMA contraction rounds once where the source says
twice. This suite varies the pool size, which the `worker_count` setting from item BB-2 of
`docs/design/ENGINE_BACKBONE_REFACTOR/` makes possible, and it separately asserts the compiler
contract, which no scheduling discipline can replace.

Every comparison is on the raw bytes. A check with a tolerance would pass on exactly the divergence
that ruins a replay a thousand steps later.

### `order_sensitive_value`

The input is one large value followed by many small ones. Each small value vanishes when added to
the running total but survives when the small values are summed together first. A sum over this
input gives a different number for a different combination order, so the bit comparison can fail.
Without such an input the test would pass on a runtime that reassociated freely.

| Test | What it proves |
| --- | --- |
| `alternating_value` | `alternating_value` is a second pattern that alternates in sign, so that a divergence which happened to cancel in the first total still shows in the output. |

### `record_tiled_total`

`record_tiled_total` is the rule under test, written in the test over `Recorder`. One work-item
per tile of 256 values adds its tile left to right into recorder scratch, and a second node adds
the 157 partials left to right into the total. Which values meet which is fixed by the tile size,
so the total has one correct bit pattern, and that pattern differs from the plain left-to-right
sum of the same input.

### `run_workload`

`run_workload` runs one fixed workload on a runtime built for `worker_count` workers, where 0 means
the whole machine, and copies the results out. Two independent rules compete for the pool, so the
worker that adds a given tile varies with the pool size. A map then mixes both totals into every
output element, so a divergence in either rule reaches the bytes being compared.

| Test | What it proves |
| --- | --- |
| `bytes_equal` | `bytes_equal` compares bytes, not numbers. A difference of one ulp must fail, and NaN payloads must compare as the bits they are. |
| `first_difference` | `first_difference` returns the index of the first differing element, so that a failure message can name the element. |

### `TheFloatingPointContractReachesConsumerCode`

The test asserts the compiler half of the contract where a consumer would feel it. This translation
unit is not part of the library and compiles against the installed-style interface, so a definition
that arrives in it is evidence that the floating-point contract travels with the target and does not
stop at the library edge. Restoring `/fp:fast` or `-ffast-math` fails the test at compile time,
which is the property item BB-3 of `docs/design/ENGINE_BACKBONE_REFACTOR/` asks the suite to have.

### `HostArithmeticIsNotReassociated`

The test catches reassociation at run time, without a macro, so it still works on a compiler whose
fast-math mode does not announce itself. The two expressions hold the same three values in the same
order with different grouping, and they are required to disagree. A compiler allowed to reassociate
would rewrite one into the other and they would match.

### `OutputIsByteEqualAcrossWorkerCounts`

This test is the core claim of the suite. One worker, two workers and the whole machine must agree
on every byte of the output.

The test guards against a vacuous pass: if every configuration collapsed to the same pool, the
schedule would not have varied at all.

### `RepeatedConfigurationsAgreeByteForByte`

The same configuration, run again from scratch, must land on the same bytes.
`OutputIsByteEqualAcrossWorkerCounts` varies the pool size. This test varies nothing and relies on
the run-to-run scheduling jitter of an unchanged pool (which steal wins a race, which worker parks)
to expose a dependence on order.

| Test | What it proves |
| --- | --- |
| `TheRebalancerDoesNotChangeTheBytes` | The whole machine runs the workload with the background rebalancer off and then on. Task migration reassigns work-items to workers in the middle of a run, and the output must not move by a byte. |

### `ReplayDoesNotDriftAcrossSteps`

A replayed graph must not drift. Every step of a multi-step run recomputes the same total from
the same input, so step 1 and step `STEPS` have to produce the same bytes. A drift would mean that
per-step state of the plan had leaked into the arithmetic.

The file covers the per-graph profiling switch: section B of
the g1 node event records design spec in the version history, built as task 4 of
the g1 node event records plan spec in the version history. It checks that
`set_profiling()` is independent across graphs, that the plan recompiles once, and that
every device-dispatch branch the scheduler can route to a profiling queue does so.

### `NUMAFallbackBranchUsesProfilingQueue`

A node with no declared `Reads` or `Writes` leaves the scheduler's anchor unset. That forces the
NUMA-local fallback branch and not the cached-index branch that the other two tests of the file
exercise. Nothing public exposes which branch a node took, because `Node::cached_queue_index_` is
private to the scheduler, so the test asserts the observable effect of the fallback: a profiling
queue is built and timing is collected. If the fallback still landed on the plain queue, the
poller's timestamp read on that event would throw, because the plain queue is not built with
`enable_profiling`. A regression therefore fails loudly and does not merely lose timing.

One branch is left: the NUMA fallback of `devices().queue_or_numa_local()`, taken when
`memory().owning_device()` does not recognise an anchor. A test
cannot force it safely. Reaching it needs a device kernel that dereferences a pointer the runtime
never allocated, which is an illegal access on a real GPU backend.

The file covers `RunReport` observability, section 5.5 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN/`. Profiling is chosen at construction. With
profiling off, the report carries no profiling breakdown. With profiling on, it carries timing per
node and per worker. Explicit transfers between host and device are accounted in both cases.

| Test | What it proves |
| --- | --- |
| `ProfilingOnCapturesPerNodeTiming` | Both kernels appear as distinct plan nodes, each dispatched once per step. The magnitudes of the device times depend on the backend and on noise, so the stable claim is structural: the per-node breakdown is present and correctly counted. |

### `NamingTheLastNodeReachesNodeTiming`

Any node can be given a diagnostic name that reaches `RunReport::NodeTiming`. This is requirement R8
of `docs/design/PHYSICS_SUBSTRATE_REQUIREMENTS/README.md`. Without a name every node reports as
"unnamed_task" whichever `add()` produced it, and a profile of a real graph is a list of identical
labels.

The test sets the name with `name()`, which names the rule the verb before it recorded.

| Test | What it proves |
| --- | --- |
| `EveryAddShapeCanBeNamed` | The test names a node of every launch shape: counted, captured, a State rule, host work and a dynamic kernel. |
| `UnnamedNodeStillReportsTheDefaultLabel` | A node added without a name keeps the documented default label, so a call site without a label is still visible in the report. |
| `TransferBytesAttributedToTheRunWindow` | A host node pushes data during the run. The bytes must land in the window of that run, not before it. |

## test_node_options.cpp

The file covers NodeOptions. The width a node states when it is recorded is the `launched_width`
and the `live_width` of its node event records, and its name and operation are in its row of
`report.node_timings`. Each node of a rule reads a region the node before it wrote, so
`report.node_events` lands in recording order whichever layer ran it.

| Test | What it proves |
| --- | --- |
| `EachNodeOfARuleReportsItsStatedWidth` | Two nodes of one rule state 600 and 1. The buffers they name are oversized by 9 and by 8 elements, so a stamp that read the size of either buffer and not the stated width fails. |
| `ResolverAndDeviceSizedNodeReportTheirStatedWidths` | A resolver states a width of one and a device-sized kernel states its capacity. The `LateCount` is built with `LateCount::fixed`, so the live count is the capacity and every work-item writes. |

### `ANodeReportsTheNameAndTheOperationItStated`

One kernel is recorded through `Recorder::kernel` with a width, a name, an operation and two
parameters. The string and the array the options point at are destroyed before the run, so a
recorder that kept the caller's pointers reads freed memory. The row of the node holds the name
and the value of the operation.

| Test | What it proves |
| --- | --- |
| `ANodeNameReplacesTheNameOfItsRule` | Two rules carry the same rule name, and the node of the first states a name of its own. The first row reports the node's name and no operation, the second the rule's name. |
| `MoreParametersThanANodeCarriesAreRefused` | Nine parameters are refused: `refusal()` holds an `InvalidGraph` error whose message names `NodeOptions::parameters`, rule 0 and the nine parameters, and the graph holds no rule afterwards. |

## test_plan_view.cpp

The file covers `compile()`, which builds a plan and runs nothing, and `Graph::plan()`, which
returns a copy of what the plan orders. `zeroed` allocates a buffer of `K_WIDTH` zeroes, so a
kernel that ran is visible in the first word.

| Test | What it proves |
| --- | --- |
| `CompileThenRunCompilesOnce` | `compile()` leaves `compile_count()` at one and the buffer at zero. A second `compile()` and the `run()` after it build nothing more. |
| `CompileReturnsTheRecordersRefusal`, `CompileOfAnEmptyGraphIsRefused` | `compile()` answers as `run()` does before its first step. A graph whose recorder refused a rule returns that refusal from `compile()` and from `plan()` and builds nothing. A graph with nothing recorded is `InvalidGraph`. |
| `ADynamicGraphCompiledThenRunComposesOnce` | A DynamicGraph takes `compile()` from the same driver: `compose_count()` is one after it and still one after the run. |

### `AChainOfThreeKernelsReportsItsPredecessorsAndRegions`

Three kernels write `first`, then `second` from `first`, then `third` from `second`. The view has
three nodes in recording order with predecessor counts 0, 1 and 1, and each node's `reads` and
`writes` hold the base address of the buffers it named. The second node was given a name and an
operation, and its entry carries both. `plan()` ran nothing, and the run after it does not
compile again.

| Test | What it proves |
| --- | --- |
| `AViewOutlivesItsGraph` | The view is read after the graph that returned it is destroyed. |

## test_recorder.cpp

The file covers what the recorder's words mean: a rule against a node, a name, a move, a region
access and a liveness observer. `add_increment` adds one rule that adds a step to every word of a
buffer, so a wrong plan shows as a wrong sum. `record_three_node_rule` records three such kernels
inside one `Recorder::rule()` scope.

| Test | What it proves |
| --- | --- |
| `ReadingARegionBetweenRunsDoesNotRecompose` | `world.region(k).size()` and `node_count()` between two runs leave `compose_count()` at one, and the second run still executes the rule. |
| `ARecordingThroughAHeldRegionRecomposes` | A `Region&` taken before the first run records a second rule after it. The next run composes again and executes both rules, so a recording is seen without a second `region(key)` call. |
| `ClearingARegionReplacesItsWork` | `Region::clear()` empties the region, a new rule is recorded, and the next run executes only the new one. |
| `ADropBetweenRunsRecomposes` | Dropping one of two regions composes once more, and the dropped region's rule does not run again. |

### `AThreeNodeRuleIsOneRuleWithOneName`

The acceptance case of the rule count. A graph whose only rule has three nodes reports `size() == 1`
and `node_count() == 3`. The three nodes share one registration, so the report holds one
`NodeTiming` row, which carries the name given by one `name()`, and `rule_registration(1)`
names no rule.

| Test | What it proves |
| --- | --- |
| `ARegionRuleReportsItsRegistrationAndName` | The same rule in the second of two regions. `DynamicGraph::rule_registration(key, rule)` reports no index before the first run, then index 0 for the first region's rule and 1 for the three-node rule, because a commit numbers rules by region key and then by `add()` order. |
| `NameLastOwnsItsText` | One rule is named from a string that is overwritten and destroyed before the run, another from two temporaries in a row. The report carries the first name intact and the last of the two. |
| `ANullOrEmptyNameLeavesTheDefault` | A rule named and then given a null `const char*` reports the name an unnamed rule reports. |
| `MovedFromGraphIsEmpty` | After a move the source reports no rule, no node and no compile, refuses a run with `InvalidGraph`, and then records and runs a new rule. The destination runs without compiling again. |
| `MovedFromDynamicGraphHasNoRegions` | The same for a `DynamicGraph`. The destination also composes again when its region is recorded on, which shows that the region still reports to the graph that owns it after the move. |
| `OneObserverPerAllocation` | Sixteen rules over one buffer leave `Recorder::watched_allocation_count()` at one; a second buffer raises it to two, and naming the first again leaves it there. |
| `ADroppedBufferIsStillDetected`, `ADroppedBufferIsStillDetectedInARegion` | A buffer named by the first and the last of six rules is destroyed after a good run. The next run is refused with `DanglingResource`, on a `Graph` and on a `DynamicGraph`. |
| `refusal_of` | `refusal_of` clears a recorder, runs a call and returns the `Error` the call left as the recorder's refusal, or nothing when it left none. It clears the recorder again before it returns. |

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

| Test | What it proves |
| --- | --- |
| `GraphClearLeavesAGraphThatRecordsAndRunsAgain` | A graph records one rule, runs and is cleared. It then holds no rule, `rule_registration(0)` is `K_NO_REGISTRATION`, and a run returns `InvalidGraph`. A rule recorded after the clear runs alone. |

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

The file covers `Recorder::scratch()`. Every case records `record_staged_rule`: a rule of two nodes
in which the first writes `in[i] + 1` into scratch and the second writes twice that into `out`. A
wrong or freed scratch pointer, or a second node that ran before the first, gives a wrong `out`.

| Test | What it proves |
| --- | --- |
| `AConsumerSeesWhatPassedThroughTheScratch` | A task that reads the output of the rule is ordered after both of its nodes, not only after the first: the copy it makes must hold the finished values. |
| `EachRunReadsItsInputAsItStands` | The input changes before each of four runs and the plan compiles once. Each run must produce the values of the input as it stood then, through the same scratch. |
| `ScratchSurvivesARecompile` | Scratch is allocated when the node is recorded, not when the plan compiles. A node added after the first run forces a second plan, and the kernels replayed into it still hold a valid pointer. |
| `ScratchIsFreedWithTheRecorder` | Recording the rule raises the allocator's live count by one, and destroying the graph brings it back to the baseline. |

### `RegionsRecordRulesWithScratch`

Two regions of a `DynamicGraph` each record the rule through `Region::recorder()`, the second
reading what the first wrote. Before the last round the test renames the rule of region 1, which
changes its recording, so the composer replays its nodes onto a new sub-graph with the scratch they
captured. Asking for the region would not do it: `region(key)` is not a mutation.

### `AScratchRequestNoDeviceCanSatisfyReturnsAllocationFailed`

The memory module has no seam that makes an allocation fail, so the test asks for half the
address space in bytes, which no device has. `scratch()` returns `AllocationFailed`, and a request
of 16 bytes on the same recorder is granted.

The file covers `Node::registration_index` and the grouping of the nodes of one `Recorder::rule()`
scope under one index. The design is sections D and F5 of
the g1 node event records design spec in the version history.

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

| Test | What it proves |
| --- | --- |
| `RuleScopeGroupsResolverAndDeviceSizedNodesUnderOneIndex` | A rule scope groups every node shape the same way. `record_late_bound_rule` records a late-count resolver, a device-sized kernel and a plain kernel in one scope, and the three share one index while the node after the scope takes the next one. |

### Placement scopes

`record_idle` records a host node that reads and writes nothing, with the options it is given. The
tests read each node's class and key back from `Graph::plan()`.

| Test | What it proves |
| --- | --- |
| `NestedScopesStampTheInnermostClassAndKey` | Seven nodes are recorded outside both scopes, in the outer one, in the inner one, in the inner one with its own class and key, in the outer one again after the inner scope closed, in the outer one with its own class and no key, and outside again. The plan shows none, outer, inner, the node's own class and key, outer, the node's own class with no key, and none. The inner key is 0. |
| `WithinReturnsWhatTheCallableReturns` | `within()` returns the rule position the callable returned. |
| `AThrowingCallableClosesItsScope` | A callable that throws leaves no scope open: the next node carries no class and no key. |

### Placement scopes and devices

The tests send a class to one past the last device, which no machine has, except the last, which
needs a second device and skips below two.

| Test | What it proves |
| --- | --- |
| `OnAScopeRunsAnUntrackedVerbOnTheLocalDevice` | `On(scope)` under a `Local` rule records the verb with no refusal, and the run writes every element. |
| `OnAScopeNoRuleMatchesRecordsOnDeviceZero` | With no rules, `On(scope)` records the raw verb as `On(DeviceIndex{0})` does. |
| `OnAScopeThatFailsRefusesTheVerb` | Under a `Fail` rule `refusal()` holds an `InvalidArgument` that names the rule and the device, and neither the launch nor the raw verb records a node. |
| `ARefusedScopeEndsTheRun` | The kept refusal is the error `run()` returns. |
| `AScopeOpenedBeforeTheRulesFollowsTheNewSet` | A scope made before the rules is refused once a `Fail` set is installed and accepted after `clear_placement_rules()`, since the recorder resolves it at record time. |
| `AScopeBufferAndItsTrackedNodesLandOnDeviceOne` | With a rule for device 1, the scope's buffer is on device 1, a tracked node recorded in the scope writes it (the plan's write is the buffer) and runs, and `On(scope)` on an untracked verb records. |

### Placed tasks

| Test | What it proves |
| --- | --- |
| `ATaskOverBorrowedMemoryRunsOnTheDeviceItStates` | A task that writes a buffer borrowed over host memory and states the runtime's last device is handed that device's queue. |
| `AStatedDeviceInTheOwnersContextWinsOverTheOwner` | A task over a buffer the runtime allocated on device 0 that states another device of device 0's context is handed the stated device's queue; skipped when no such device exists. |
| `AStatedDeviceInAnotherContextIsRefused` | The same task stating a device outside device 0's context fails the run with an `InvalidGraph` error naming the other context, and its body never runs; skipped when every device shares device 0's context. |
| `ATaskThatStatesNoDeviceRunsOnTheOwner` | A task that states no device, over a buffer allocated on the last device, is handed that device's queue. |
| `ATaskPlacedPastTheDeviceCountStops` | A task placed on one past the last device stops the process with a message that says the index names no device. |

## test_region.cpp

The file covers `extend()` as a library above the runtime uses it: the extension receives the
Recorder and records device tasks with `Recorder::task`.

### `record_device_task`

`record_device_task` records one `Recorder::task` node whose body is a `parallel_for` on the queue
it is given, named and given its width through NodeOptions. The tasks are device kernels, not host
tasks, because a host access to a `Residency::Shared` buffer while a kernel runs on the same GPU
faults on Windows with CUDA; see `docs/reference/KNOWN_ISSUES.md`.

| Test | What it proves |
| --- | --- |
| `ExtensionTasksOrderAgainstANeighbourRegion` | One extension records two tasks on a buffer, a fill and a tripling, and a higher-keyed region reads that buffer through an ordinary `add()`. The consumer sees the tripled values, so the two tasks ran in their recorded order and the boundary edge ordered the consumer behind them. |
| `AnExtensionIsOneRule` | An extension that records two tasks reports a region size of one, a node count of two and a graph size of one: `size()` counts an extension once, however many nodes it records. |
| `AnExtensionRunsOnceWhenItIsCalled` | The extension counts its own calls. The count is one when `extend()` returns and stays one over three replayed steps and over a rebuild of the neighbouring region. Dropping its own region and extending it again raises the count to two. |
| `AStaticGraphTakesAnExtension` | A static `Graph` records an extension beside an ordinary `add()` that reads the extension's buffer. Four steps advance both, so the form is not specific to regions and survives a chained replay. |

## test_run_handle.cpp

The file covers `Graph::run_async()`, `DynamicGraph::run_async()` and the `RunHandle` they return.

The claim under test is that a step can start without blocking the caller, so that a simulation tick
overlaps with work that needs no workers, such as a renderer or an audio extract of the previous
tick. A return value cannot show that a call did not block, so the overlap tests use a rendezvous:
the step's own task waits for a flag that the caller sets after `run_async()` has returned. If
`run_async()` blocked, nobody could set the flag in time and the task would time out. Every wait is
bounded, so a regression reports a failure and does not hang.

| Test | What it proves |
| --- | --- |
| `wait_for_gate` | `wait_for_gate` blocks until the flag is released or the deadline passes, and reports which happened. It is the body of a host task, so a step can be held open while the test does something on the calling thread. |
| `SubmitReturnsBeforeTheStepFinishes` | `run_async()` must return before the step finishes. The step's host task waits for a flag the caller sets only afterwards, so the task can see the flag only if the caller kept running. |
| `MatchesTheSynchronousResult` | An asynchronous step must be observationally identical to a synchronous one: the same stepped `State`, driven the same number of times, ends in the same place. |
| `StateAdvancesOnWaitNotOnSubmit` | The `State` advance belongs to `wait()`, not to the submit. Until the step has been settled, the field it just wrote is not yet current. A runtime that advanced on submit would let a reader see the next field one step early. |
| `DestroyingAnUnwaitedHandleStillFinishesTheStep` | A handle that goes out of scope unwaited must still finish the step, because the compiled plan cannot be destroyed while workers hold pointers into it. The graph must also be runnable again afterwards. |
| `WaitingTwiceThrows` | A second wait on the same handle has no step to wait for. It must throw, because the alternative is to block on a latch nobody will signal again. |
| `ASecondRunIsRejectedWhileAStepIsInFlight` | A second step started while one is in flight, synchronously or asynchronously, must be rejected before anything touches the plan. A synchronous run re-arms the in-degree of every node, so letting it proceed would rewrite the scheduling state of the DAG the workers are still walking. |
| `TaskExceptionSurfacesOnWaitAndLeavesTheGraphUsable` | A task exception surfaces from `wait()`, and the graph stays usable. One failing step must not turn into a graph that fails forever. |
| `ReadyTracksTheStep` | `ready()` reports whether a wait would return immediately, and it does so without blocking. It must be false while the step is held open and true once the step has finished. |
| `DynamicGraphRunsAsynchronously` | The region-partitioned graph behaves the same way: a step overlaps the caller, and a pending structural change is committed before the step is submitted. |

### `expect_refused_as_invalid_state`, `AnotherGraphIsRefusedWhileAStepIsPending`

A runtime has one engine, so one graph's pending step occupies it. While the step of the first graph
is held open at a gate, `run()` and `run_async()` on a second graph of the same
runtime must each report `InvalidState` and must not run the second graph's task. The first
graph's handle must then complete with a successful report, and the second graph must run once the
step is settled. `expect_refused_as_invalid_state` checks one `Result` for that code.

| Test | What it proves |
| --- | --- |
| `CancelReachesOnlyTheGraphItIsCalledOn` | `cancel()` on a graph with no run in flight must leave another graph's pending step alone: the step's report says not cancelled. `cancel()` on the graph that owns the pending step must mark its report cancelled. A `cancel()` sent while nothing runs must not mark the next run. |

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

## test_access.cpp

The file tests Access: the regions it keeps in order and the observers of both sets.

| Test | What it proves |
| --- | --- |
| `RegionsKeepTheirOrder` | Regions given directly keep their order and carry no observer. |
| `HandlesBringTheirObservers` | Handles given through Reads and Writes bring the observers of both. |
