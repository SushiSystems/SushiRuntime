# api tests: graph

The tests of `Graph`, `DynamicGraph`, `Region`, `Recorder`, `PlanView` and `RunHandle`. They are part of `sr_surface_tests`;
[the page of the module's tests](../README.md) says how to run them.

## benchmark_dynamic_graph.cpp

The file holds two `Benchmark_` suites, run by `sr bench`. They print figures and pass or fail
nothing.

`Benchmark_DynamicGraph.ManyRegionsStreamingMutations` times the first composing run of a world of
256 regions, then streams 16 mutations into it between runs, and records the first run's time as a
property. That each mutation recomposes once, and that every region keeps the count its runs give
it, are held by `EachOfSeveralStreamedMutationsRecomposesOnce` and
`EveryRegionKeepsItsRunCountAfterStreamedMutations` in `test_dynamic_graph.cpp`, on 32 regions.

`Benchmark_RecomposeChurn.AllocationsAndTimePerCommit` is the gate benchmark for the
"allocation-per-node churn on the recompose path" theme, audit Tier B. The substrate commits work
proportional to the changed region every step: a dirty region is rebuilt, and each rebuilt node pays
a `Node` allocation, two IO-region vectors, an `AccessNode` per access, event constructors and build
list nodes. The suite isolates that cost in two ways.

Commit wall time is the portable headline. A commit is one `region().add()` that marks a
region dirty and one `run(1)` that recomposes and steps. Compared with a `run(1)` that has no
mutation, which is pure replay, the delta is the recompose cost.

Allocations per commit come from a gated global `operator new` counter. The count is
authoritative on a single-CRT build, Linux and CI, where it sees allocations inside
libsushiruntime too. On a Windows DLL build it counts only allocations on the executable's
side, from headers and templates, so trust the wall-time delta there and the allocation count
on the CI lane.

The counter is off by default and outside the measured windows, so the process-wide replacement
costs one relaxed load per allocation. One replacement serves the whole benchmark binary and
routes everything through `malloc` and `free`. This replacement is also why an AddressSanitizer
build leaves the benchmark suite out; see `cli/README.md`.

The numbers are reported for the owner to compare across the B2 changes. If the recompose
allocations or milliseconds dominate, the node, `AccessNode` and event churn is the target. If the
pure-replay step already allocates heavily, the effort belongs in steady-state execution.

## benchmark_graph.cpp

`Benchmark_ChainDispatch` is the baseline for the graph-replay design, `docs/design/GRAPH_REPLAY/README.md`, sections 0 and
G (P0). Node i reads buffer i and writes buffer i+1, so every node depends on the one before it
and the scheduler cannot dispatch two nodes at once. The run therefore pays the scheduler's
whole per-node dispatch path, which is dependency resolution, submit, poll and the hand-off to
the successor, with no fan-out to hide it behind. The benchmark sweeps the chain length K and
reports milliseconds per node. Layer 0, which removes the per-node allocation in
`SmallEventVector::to_vector()`, and the later layers are measured against that number.

`Benchmark_RootFanout` is the gate benchmark for the batched root injector, audit ds-1. Each replayed step re-injects
every root node. `submit_batch` takes a step's whole root set in one lock acquisition, one
`inflight_` increment and one wake burst, so the injection cost per step does not grow with
root fan-out.

The benchmark builds R independent roots, each writing its own buffer, replays a fixed number
of steps, and sweeps R. It reports milliseconds per step and the amortized cost per root and
step. Injection is O(1) per step whatever R is, so as R grows the per-root cost should fall
towards the cost of executing and completing one task, and should not stay pinned to a lock
and a notify per root. Neither suite asserts anything. That a dependent chain settles to its
length and that every independent root runs on every step are held by the `Medium_GraphReplay`
tests at the end of `test_graph.cpp`.

## test_device_sized.cpp

The file covers the launches of phase P3 of `docs/design/GRAPH_REPLAY/README.md` (sections D.2 and G): a
`Recorder::kernel(DeviceSized, ...)` and a `sized_from_device` task, both sized by a count a
predecessor wrote in the same step. The tests run on the program's runtime; the two rows of
`Medium_DeviceSizedLayers` run one on the in-order chain and one on a runtime forced onto the scheduler.

### `record_marking_rule`

`record_marking_rule` is the rule every case but a few records. A resolver publishes two live words
into recorder scratch, the width and a salt, and a device-sized kernel folds its index and both
words into `marks[t]` for the work-items below the width. A word at or past the width keeps what an
earlier step left, so the bytes after a step depend on every width so far. `marked` is the host
model of one such write.

| Test | What it proves |
| --- | --- |
| `OnlyItemsBelowTheLiveWidthRun` | Widths of 10, 0, the capacity and 257 run in turn on one compiled plan. After each run every word must equal the host model, and the word one past the capacity must still hold the canary. A launch that ignored the live width, or read the width of the previous run, fails on the first comparison. |
| `ALiveWidthChangeDoesNotRecompile` | The same four widths leave `compile_count()` at one. |
| `ANullLiveWidthRunsEveryItem` | A null `live_width` lets every work-item run up to the capacity. |
| `TheKernelArityPicksTheLiveWords` | A callable of two arguments gets `live_words[0]` after the index. `record_marking_rule` covers the callable of three arguments. |
| `ZeroCapacityLaunchRunsNothing` | A zero-width `parallel_for` is not portable, so a launch of capacity zero submits an empty task. The buffer keeps its canary. |
| `ZeroCapacityLaunchReleasesItsSuccessor` | The empty launch still completes: a successor that reads the same buffer copies the untouched canary. |

### `DeviceSizedRuleMatchesTheHostModelAsTheCountsChange`

On every step a producer kernel writes a new width, 0 and the capacity included. The test computes
the digest of the whole buffer for each of 48 steps on the host, with `marked` and `digest_of`, and
the run must record the same digests bit for bit. The digests must also change on more than a
quarter of the steps, so a match shows the width at work. Both rows of `Medium_DeviceSizedLayers`
hold the promise: the chain and the forced scheduler each answer to the host model, not to each other.
The salt is fixed, since a host provider of the salt would make the model depend on how often the
runtime calls it.

| Test | What it proves |
| --- | --- |
| `SizedFromDeviceMatchesTheHostModelAsTheCountChanges` | The same comparison for a `sized_from_device` task, whose live size comes from the same counter. |
| `DeviceCountOverCapacityFailsTheRun` | A producer writes a count one above the capacity. The resolver cannot throw from a kernel, so it writes a fault record and the run fails when the step ends. |
| `AGraphRunsCleanAfterARunFailedForACountOverTheCapacity` | A second graph on the same runtime, with a count that fits, runs clean after the failed run. |

## test_dynamic_graph.cpp

The file covers the dynamic, region-partitioned graph (`Runtime::dynamic_graph()`). Each test
asserts the numerical outcome and the composition behaviour: regions run together, data dependencies
across regions order correctly, dropping a region removes its work, and a run without a mutation
never recomposes. The fixtures turn the background rebalancer off, so no thermal move reorders a region.

| Test | What it proves |
| --- | --- |
| `IndependentRegionsAdvanceTheirOwnBuffers` | Two independent regions each own a buffer and increment it once per step. Over several steps both buffers advance. |
| `AnUnmutatedMultiStepRunComposesOnce` | A run that mutates nothing composes the combined plan exactly once. |
| `EachOfSeveralStreamedMutationsRecomposesOnce` | A world of 32 regions composes once, then eight runs each follow one more task streamed into a region; the compose count rises by exactly one per run. |
| `EveryRegionKeepsItsRunCountAfterStreamedMutations` | After the same eight streamed runs, every region holds one count per run plus one per run after its own second task joined. |
| `ARuleChangeReplacesARegionsNodeWithoutARecord` | A task recorded once in region 0, inside the scope a rule matches: a `Fail` rule for an absent device fails the run with `InvalidArgument` naming `"ruled"`, and a `Local` set given before the next run lets it run on device 0 with no re-record. |
| `ARuleToAnAbsentDeviceRefusesTheRun`, `AnUnchangedRuleSetComposesNothing`, `ARuleChangeToDeviceOneRemapsTheNode` | The three other promises of that rule change: the refusal alone, an unchanged set composes nothing more, and on a machine with two devices a rule for device 1 moves the task there and composes once. The last skips below two devices. The fixture clears the rules after each test. |

### `ARegionObservesTheWritesOfTheRegionBeforeIt`

A chain of regions with ascending keys 10, 11, 12 threads a value: the first writes `x`, each next
reads the buffer before it and writes its own, doubling the value. The composer must order the
regions by key, so the last buffer holds `(i + 1) * 2^(regions - 1)`. The test runs as two rows,
two regions and three, since the promise is the same and only the number of hops changes.

| Test | What it proves |
| --- | --- |
| `DropRegionStopsItsWork` | Dropping a region removes its work at the next step boundary. After the drop the surviving region keeps advancing, and the buffer of the dropped region freezes. |
| `AddRegionBetweenSteps` | Adding a region between steps recomposes exactly once, and the work of the new region takes effect on the next run. |
| `BuildingAllRegionsComposesOnce` | The fixture records 32 regions and runs three steps; the compose count is one. |
| `OneMutationRecomposesExactlyOnce` | Mutating one region out of the 32 recomposes once, and `compose_count` advances by exactly one per mutation, not per region. |
| `EveryRegionHoldsItsRunCountAfterAMutation` | After that mutation the results stay correct: the unchanged regions hold four, the mutated one fourteen. |
| `FindRegionRecomposesNothing` | Reading a region through `find_region()` is not a mutation. The plan must not recompose on the next run. |
| `FindRegionDoesNotCreateAnAbsentKey` | An absent key returns `nullptr` where `region()` would create the region. |
| `ReadReadSharingStaysConsistent` | One producer writes `base`, and two consumer regions both read `base` and write their own outputs. The two readers share no hazard; both see the producer's writes. The observable is correctness: the output of each reader reflects `base`. |
| `AStreamedInConsumerSeesTheProducersWrites` | Region 0, which writes `x`, runs alone first. Region 1, which reads `x`, then streams in and observes `x` on the next run. |
| `StreamingAConsumerInRecomposesOnce` | Streaming that consumer in raises the compose count from one to two. |
| `DropProducerLeavesConsumerRunnable` | Dropping the producer must cleanly un-wire the boundary and leave the consumer runnable, now without an upstream edge. The consumer keeps adding the producer's last values, so `y` ends at `2i`, and the run neither deadlocks nor faults. |

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

The file covers how an added observer reaches a `DynamicGraph` and its registration index, which
is dense and ordered by ascending region key and then by `add()` order at every recompose. The
design is sections D, E (item 8) and F.6 of
the g1 node event records design spec in the version history, built as task 6 of
the g1 node event records plan spec in the version history. The tests read the index through the
node table of an observed run, `RunObservation::table`, whose rows carry `index` and `name`.

A run with no observer records nothing; that promise is held once, by `ARunWithNoObserverRecordsNothing`
in `test_graph.cpp`. That an observer added to a `DynamicGraph` composes it no more than once is held
by `AnObserverRebuildsNothing` in [plumbing/test_step_driver.cpp](plumbing/README.md).

| Test | What it proves |
| --- | --- |
| `RequestedGraphProducesRecordsMatchingRenumberedIndex` | A `DynamicGraph` of an observed runtime must produce records, and the `node_index` of each names a row of the table, either `A` or `B`. |

### `IndexDenseAndOrderedByRegionThenAddOrder`

With two regions that each make more than one `add()` call, the table indices are dense and ordered by
ascending region key first and by `add()` order within a region second.

Region 5 is added first but has a higher key than region 2, so its two calls must land after the one
call of region 2 once the order follows the key.

### `IndexStaysDenseAfterDropAndReAdd`

Dropping a region and adding it again must not leave a gap in the index space. The next recompose
renumbers every live region densely from zero.

The file compares whole output buffers byte for byte and uses the public API only, so it holds
across a change of internals. All data is `std::uint32_t`.

Every comparison is against a host model of the work, computed in the same order the kernels are
recorded, and the rows of `Medium_LayerEquivalence` run it on the in-order chain and on a runtime
forced onto the scheduler. The layer a run took is not asserted: it is an inside detail, and the
bytes must be right on both.

Four shapes are covered: the same work as regions of a `DynamicGraph` and as one static `Graph`,
with a region dropped and recorded again; host tasks among kernels in one dependency chain, with the two whole-buffer passes on the host and on
the device (`Medium_LayerEquivalencePasses`); steps through `run_async()` against the host model of
`run(n)`; and a 2-D box with an origin offset through each launch form.

`NdRange` has no origin, so its kernels add the offset to the global id themselves.

`PlacedUntrackedLaunchesMatchTheHostReference` places its untracked kernels on the last device of the
runtime with `On`.

## test_graph.cpp

The tests of `Graph` are described in [test_graph.md](test_graph.md).

## test_node_options.cpp

The file covers NodeOptions. The width a node states when it is recorded is the `launched_width`
and the `live_width` of its node event records, and its name and operation are in its row of
the observation's node table. Each node of a rule reads a region the node before it wrote, so
the observation's events land in recording order whichever layer ran it. The observations come from
`Testing::RunRecorder`, which wraps the `RecordingObserver` adapter of the report module.

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
| `ARefusedAddRecordsNoNode` | An add with nine parameters leaves a kept refusal with the code `InvalidGraph` and the graph holds no rule afterwards. The limit as a unit is held by `ParametersUpToTheLimitAreAcceptedAndOnePastIsRefused` in `detail/test_node_stamp.cpp`. |

## test_plan_view.cpp

The file covers `compile()`, which builds a plan and runs nothing, and `Graph::plan()`, which
returns a copy of what the plan orders. `zeroed` allocates a buffer of `K_WIDTH` zeroes, so a
kernel that ran is visible in the first word.

| Test | What it proves |
| --- | --- |
| `CompileRunsNothing` | `compile()` leaves `compile_count()` at one and the buffer at zero. |
| `ACurrentPlanIsNotBuiltAgain` | A second `compile()` builds nothing more. |
| `RunUsesThePlanCompileBuilt` | The `run()` after `compile()` builds nothing more and executes the kernel. |
| `CompileReturnsTheRecordersRefusal` | A graph whose recorder refused a rule returns that refusal from `compile()` and from `plan()` and builds nothing. The code is `InvalidAccess`, the one an empty `State` gets, and the message equals the one `run()` returns. |
| `CompileOfAnEmptyGraphIsRefused` | A graph with nothing recorded is `InvalidGraph`, as `run()` answers. |
| `ACompiledDynamicGraphRunsNothing` | A DynamicGraph takes `compile()` from the same driver: `compose_count()` is one after it and the buffer is still zero. |
| `ADynamicGraphCompiledThenRunComposesOnce` | `compose_count()` is still one after the run that follows. |

### `AChainOfThreeKernelsReportsItsPredecessorsAndRegions`

Three kernels write `first`, then `second` from `first`, then `third` from `second`. The view has
three nodes in recording order with predecessor counts 0, 1 and 1, and each node's `reads` and
`writes` hold the base address of the buffers it named. `plan()` ran nothing.

| Test | What it proves |
| --- | --- |
| `ThePlanCarriesTheOptionsANodeStated` | The second node of the same chain was given a name, an operation, a placement class and key, and a determinism; its entry carries each, and the first node's entry carries none. |
| `AViewOutlivesItsGraph` | The view is read after the graph that returned it is destroyed. |

## test_recorder.cpp

The file covers what the recorder's words mean: a rule against a node, a name, a move, a region
access and a liveness observer. `add_increment` adds one rule that adds a step to every word of a
buffer, so a wrong plan shows as a wrong sum. `record_three_node_rule` records three such kernels
inside one `Recorder::rule()` scope. Every fixture derives from `RecorderTest`, which clears the
placement rules after each test, since the tests share the program's runtime.

| Test | What it proves |
| --- | --- |
| `ReadingARegionBetweenRunsDoesNotRecompose` | `world.region(k).size()` and `node_count()` between two runs leave `compose_count()` at one, and the second run still executes the rule. |
| `ARecordingThroughAHeldRegionRecomposes` | A `Region&` taken before the first run records a second rule after it. The next run composes again and executes both rules, so a recording is seen without a second `region(key)` call. |
| `ClearingARegionReplacesItsWork` | `Region::clear()` empties the region, a new rule is recorded, and the next run executes only the new one. |
| `ADropBetweenRunsRecomposes` | Dropping one of two regions composes once more, and the dropped region's rule does not run again. |

### `AThreeNodeRuleIsOneRuleWithOneName`

The acceptance case of the rule count. A graph whose only rule has three nodes reports `size() == 1`
and `node_count() == 3`. The three nodes share one registration, so the table holds one
row, which carries the name given by one `name()`, and `rule_registration(1)`
names no rule.

| Test | What it proves |
| --- | --- |
| `ARegionRuleReportsItsRegistrationAndName` | The same rule in the second of two regions. `DynamicGraph::rule_registration(key, rule)` reports no index before the first run, then index 0 for the first region's rule and 1 for the three-node rule, because a commit numbers rules by region key and then by `add()` order. |
| `NameLastOwnsItsText` | One rule is named from a string that is overwritten and destroyed before the run, another from two temporaries in a row. The report carries the first name intact and the last of the two. |
| `ANullOrEmptyNameLeavesTheDefault` | A rule named and then given a null `const char*` reports the name an unnamed rule reports. |
| `MovedFromGraphIsEmpty` | After a move the source reports no rule, no node and no compile, and refuses a run with `InvalidGraph`. |
| `ThePlanMovesWithTheTakenGraph` | The destination runs without compiling again. |
| `AMovedFromGraphRecordsAndRunsAgain` | The source of a move records a new rule and runs it. |
| `MovedFromDynamicGraphHasNoRegions`, `TheComposedPlanMovesWithTheTakenDynamicGraph`, `AMovedFromDynamicGraphTakesRegionsAndRunsAgain` | The same three promises for a `DynamicGraph`. The destination also composes again when its region is recorded on, which shows that the region still reports to the graph that owns it after the move. |
| `ManyNodesOverOneBufferAddUp` | Sixteen rules over one buffer, one over another and one more over the first run in order, and the first buffer holds seventeen. |
| `ADroppedBufferIsStillDetected`, `ADroppedBufferIsStillDetectedInARegion` | A buffer named by the first and the last of six rules is destroyed after a good run. The next run is refused with `DanglingResource`, on a `Graph` and on a `DynamicGraph`. |
| `refusal_of` | `refusal_of` clears a recorder, runs a call and returns the `Error` the call left as the recorder's refusal, or nothing when it left none. It clears the recorder again before it returns. |

### `ATaskOfTwoCommandGroupsIsOrderedByItsAccess`

A kernel writes `i + 1` into a buffer. A task recorded with `add_task` reads it and submits two
command groups: the first doubles the values into a second buffer, the second adds one into a
third and waits on the first one's event. The task returns the second event. A kernel that reads
the third buffer multiplies by three. Every element of its output is `((i + 1) * 2 + 1) * 3` only
when the task ran after the first kernel and the last kernel waited for the event the task
returned.

### Generations after a move

A graph records one rule and runs, so its plan is built at the recorder's generation. The graph is
then moved into another, which moves its recorder.

| Test | What it proves |
| --- | --- |
| `TheMovedWorkKeepsItsGeneration` | The recorder moved to keeps the generation of the work it took. |
| `AGenerationThePlanWasBuiltAtNeverNamesANewRule` | A second rule recorded on the graph moved from gets a generation that differs from the one the plan was built at, and the next run executes the second rule alone: a recorder that returned to the generation of the plan would let the old plan run. |
| `AMoveAssignmentDoesNotStepTheGenerationBack` | A move assignment from a graph whose recorder has a generation no larger must still leave a larger one. |

Only a Graph or a Region moves a Recorder, which the static_asserts at the top of the file hold.

| Test | What it proves |
| --- | --- |
| `GraphClearForgetsTheRulesAndTheirRegistration` | A graph records one rule, runs and is cleared. It then holds no rule and `rule_registration(0)` is `K_NO_REGISTRATION`. |
| `AClearedGraphRefusesAnEmptyRun` | A run of the cleared graph returns `InvalidGraph`. |
| `AClearedRuleDoesNotRunAgainAndANewOneDoes` | A rule recorded after the clear runs alone. |
| `Regression_GraphClearFreesWhatItsCompiledPlanCaptured` | A host task captures the only other owner of a `shared_ptr`. After a run the plan keeps it alive; after `clear()` the `weak_ptr` watching it has expired, so the plan and its captures were freed, not marked stale. |

### `AClearInsideAnExtensionStartsANewRule`

An extension records a host node, calls `clear()` and records a second host node. The graph then
holds one rule and one node, and the run executes the second node only. `extend()` holds a rule
scope open around the extension, so a `clear()` that leaves the scope's state as it is records the
second node into a rule the recorder has already dropped.

### The parameter limit of a named rule

`Medium_RecorderParameterLimit` has two rows, `add(count, Reads, Writes, kernel, options)` and
`add_rule(state, kernel, options)`. In each, a rule with an operation and nine parameters in its
`NodeOptions` is refused with `InvalidGraph` (`NineParametersAreRefusedAndRecordNothing`): the
message holds `9 parameters` and `at most 8`, and the rule is not recorded. The same entry records
with eight (`EightParametersAreKept`). The graph is not run. The limit as a unit is held by
`ParametersUpToTheLimitAreAcceptedAndOnePastIsRefused` in `detail/test_node_stamp.cpp`.

### `AnExtensionThatRecordsNothingAddsNoRule`

A rule named `integrate` is followed by an extension that records nothing. The graph still holds
one rule and one node, and the observed run reports the node under `integrate`.

The file covers `Recorder::scratch()`. Every case records `record_staged_rule`: a rule of two nodes
in which the first writes `in[i] + 1` into scratch and the second writes twice that into `out`. A
wrong or freed scratch pointer, or a second node that ran before the first, gives a wrong `out`.

| Test | What it proves |
| --- | --- |
| `AConsumerSeesWhatPassedThroughTheScratch` | A task that reads the output of the rule is ordered after both of its nodes, not only after the first: the copy it makes must hold the finished values. |
| `EachRunReadsItsInputAsItStands` | The input changes before each of four runs. Each run must produce the values of the input as it stood then, through the same scratch. |
| `AReplayedRuleCompilesOnce` | Four runs of that rule leave `compile_count()` at one. |
| `ScratchSurvivesARecompile` | Scratch is allocated when the node is recorded, not when the plan compiles. A node added after the first run forces a second plan, and the kernels replayed into it still hold a valid pointer. |

Whether scratch returns to the runtime when the graph dies cannot be read through the public
surface, so that promise has no test here.

### `RegionsRecordRulesWithScratch`

Two regions of a `DynamicGraph` each record the rule through `Region::recorder()`, the second
reading what the first wrote. Before the last round the test renames the rule of region 1, which
changes its recording, so the composer replays its nodes onto a new sub-graph with the scratch they
captured. Asking for the region would not do it: `region(key)` is not a mutation.
`RenamingARegionsRuleRebuildsThatRegionOnce` holds the other half: the rename composes once and the
region still has two nodes.

### `AScratchRequestNoDeviceCanSatisfyReturnsAllocationFailed`

The memory module has no seam that makes an allocation fail, so the test asks for half the
address space in bytes, which no device has. `scratch()` returns `AllocationFailed`.
`ASmallScratchRequestIsGrantedAfterALargeOneFailed` shows that a request of 16 bytes on the same
recorder is granted.

The file covers the grouping of the nodes of one `Recorder::rule()` scope under one registration
index, read through the node table of an observed run: a rule takes one row, whose `multiplicity` is
the number of its nodes. The design is sections D and F5 of
the g1 node event records design spec in the version history.

`record_grouped_rule` records three kernels in one scope, each reading what the one before it
wrote. `record_late_bound_rule` records a late-count resolver, a device-sized kernel and a plain kernel
in one scope.

| Test | What it proves |
| --- | --- |
| `RuleScopeGroupsItsNodesUnderOneIndex` | Two rows, one per kind of rule. A node named `before`, the rule and a node named `after` give a table of three rows: indices 0, 1 and 2, with the rule's row holding its three nodes. |

### `RuleScopeGroupSurvivesRebuild`

The group of a rule scope must survive a rebuild onto a fresh plan, whose registration counter
restarts at 0.

The test adds a plain node after the rule, which marks the graph dirty, and then runs twice, so
that the plan cache replays every recorded node into a second plan. The table of the second run has
three rows with indices dense from 0 to 2, and the rule's row still holds its three nodes.

The index that follows call order, not compiled order, is held by `NodeIndexIsRegistrationOrder` in
`test_graph.cpp`.

### Placement scopes

`record_idle` records a host node that reads and writes nothing, with the options it is given. The
tests read each node's class and key back from `Graph::plan()`. `record_nested_scopes` records the seven
nodes the first three tests read.

| Test | What it proves |
| --- | --- |
| `NestedScopesStampTheInnermostClassAndKey` | Seven nodes are recorded outside both scopes, in the outer one, in the inner one, in the inner one with its own class and key, in the outer one again after the inner scope closed, in the outer one with its own class and no key, and outside again. The first, second, third and last show none, outer, inner and none. The inner key is 0. |
| `ANodesOwnClassAndKeyBeatTheScopes` | The fourth node shows its own class and key, and the sixth its own class with no key. |
| `ClosingAScopeRestoresTheOuterOne` | The fifth node shows outer again. |
| `WithinReturnsWhatTheCallableReturns` | `within()` returns the rule position the callable returned. |
| `AThrowingCallableClosesItsScope` | A callable that throws leaves no scope open: the next node carries no class and no key. |

### Placement scopes and devices

The tests send a class to one past the last device, which no machine has, except the last, which
needs a second device and skips below two.

| Test | What it proves |
| --- | --- |
| `OnAScopeRunsAnUntrackedVerbOnTheLocalDevice` | `On(scope)` under a `Local` rule records the verb with no refusal, and the run writes every element. |
| `OnAScopeNoRuleMatchesRecordsTheVerb` | With no rules, `On(scope)` records the raw verb with no refusal. |
| `AScopeNoRuleMatchesPlacesItsBufferOnDeviceZero` | With no rules, a buffer of the scope is on device 0, as `On(DeviceIndex{0})` would place it. |
| `OnAScopeThatFailsRefusesTheVerb` | Two rows, the counted verb and the raw verb. Under a `Fail` rule `refusal()` holds an `InvalidArgument` that names the rule and the device, and neither verb records a node. |
| `ARefusedScopeEndsTheRun` | The kept refusal is the error `run()` returns. |
| `AScopeOpenedBeforeTheRulesFollowsTheNewSet` | A scope made before the rules is refused once a `Fail` set is installed, since the recorder resolves it at record time. |
| `AClearedSetLeavesNothingToRefuse` | The same scope is accepted after `clear_placement_rules()`. |
| `AScopeBufferAndItsTrackedNodesLandOnDeviceOne` | With a rule for device 1, the scope's buffer is on device 1, a tracked node recorded in the scope writes it and runs, and `On(scope)` on an untracked verb records. |

### Placed tasks

The tests read the device a task ran on back through `Runtime::advanced().queue(device)`.

| Test | What it proves |
| --- | --- |
| `ATaskOverBorrowedMemoryRunsOnTheDeviceItStates` | A task that writes a buffer borrowed over host memory and states the runtime's last device is handed that device's queue. |
| `AStatedDeviceInTheOwnersContextWinsOverTheOwner` | A task over a buffer the runtime allocated on device 0 that states another device of device 0's context is handed the stated device's queue; skipped when no such device exists. |
| `AStatedDeviceInAnotherContextIsRefused` | The same task stating a device outside device 0's context fails the run with an `InvalidGraph` error naming the other context, and its body never runs; skipped when every device shares device 0's context. |
| `ATaskThatStatesNoDeviceRunsOnTheOwner` | A task that states no device, over a buffer allocated on the last device, is handed that device's queue. |
| `Regression_ARuleNamingDeviceOneRunsTheNodeThere` | A task that states no device and names no region, recorded in the scope a rule for device 1 matches, is handed device 1's queue; skipped below two devices. |
| `Regression_ARuleNamingAnAbsentDeviceRunsLocalWhenItSaysLocal` | The same task under a `Local` rule for a device the machine lacks runs on device 0. |
| `Regression_ARuleNamingAnAbsentDeviceRefusesWhenItSaysFail` | Under a `Fail` rule for that device the run ends with `InvalidArgument` naming `"ruled"` and the body never runs. |
| `ATaskPlacedPastTheDeviceCountStops` | A task placed on one past the last device stops the process with a message that says the index names no device. It is a death test, so it runs medium. |

## test_region.cpp

The file covers `extend()` as a library above the runtime uses it: the extension receives the
Recorder and records device tasks with `Recorder::task`. The fixture turns the rebalancer off.

### `record_device_task`

`record_device_task` records one `Recorder::task` node whose body is a `parallel_for` on the queue
it is given, named and given its width through NodeOptions. The tasks are device kernels, not host
tasks, because a host access to a `Residency::Shared` buffer while a kernel runs on the same GPU
faults on Windows with CUDA; see `docs/backlog/README.md`.

| Test | What it proves |
| --- | --- |
| `ExtensionTasksOrderAgainstANeighbourRegion` | One extension records two tasks on a buffer, a fill and a tripling, and a higher-keyed region reads that buffer through an ordinary `add()`. The consumer sees the tripled values, so the two tasks ran in their recorded order and the boundary edge ordered the consumer behind them. |
| `AnExtensionIsOneRule` | An extension that records two tasks reports a region size of one, a node count of two and a graph size of one: `size()` counts an extension once, however many nodes it records. |
| `AnExtensionsTasksRunInRecordedOrder` | The two tasks add one each to a zeroed word; after the run it holds two. |
| `AnExtensionRunsOnceWhenItIsCalled` | The extension counts its own calls. The count is one when `extend()` returns and stays one over three replayed steps and over a rebuild of the neighbouring region. |
| `ARegionRecordedAgainAfterADropIsExtendedAgain` | Dropping the region and extending it again raises the count to two. |
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
| `WaitConsumesTheHandleAndTheWriteIsVisible` | `wait()` consumes the handle, the step's write is visible afterwards, and the report counts at least one task. |
| `MatchesTheSynchronousResult` | An asynchronous step must be observationally identical to a synchronous one: the same stepped `State`, driven the same number of times, ends in the same place. |
| `ReplayingAsynchronouslyDoesNotRecompile` | Six asynchronous steps of one rule leave `compile_count()` at one. |
| `StateAdvancesOnWaitNotOnSubmit` | The `State` advance belongs to `wait()`, not to the submit. Until the step has been settled, the field it just wrote is not yet current. A runtime that advanced on submit would let a reader see the next field one step early. |
| `DestroyingAnUnwaitedHandleStillFinishesTheStep` | A handle that goes out of scope unwaited must still finish the step, because the compiled plan cannot be destroyed while workers hold pointers into it. |
| `ADestroyedUnwaitedHandleLeavesTheGraphRunnable` | The graph must also be runnable again afterwards. |
| `ASecondWaitIsRefusedAsInvalidState` | A second wait on the same handle has no step to wait for. It returns `InvalidState`, because the alternative is to block on a latch nobody will signal again. |
| `ASecondRunIsRejectedWhileAStepIsInFlight` | A second step started while one is in flight, synchronously or asynchronously, must be rejected before anything touches the plan. A synchronous run re-arms the in-degree of every node, so letting it proceed would rewrite the scheduling state of the DAG the workers are still walking. |
| `TheGraphRunsAgainOnceTheStepHasSettled` | Rejection left nothing broken behind: after the step settles the graph runs again. |
| `TryWaitReportsFailureAsAValue` | A task exception surfaces from `wait()` as an error value, and the handle then owns no step. |
| `AFailedStepLeavesTheGraphUsable` | One failing step must not turn into a graph that fails forever: the graph accepts another step, and another graph runs after it. |
| `ReadyTracksTheStep` | `ready()` reports whether a wait would return immediately, and it does so without blocking. It must be false while the step is held open and true once the step has finished. |
| `DynamicGraphRunsAsynchronously` | The region-partitioned graph behaves the same way: a step runs its regions. |
| `ARegionAddedBetweenStepsIsCommittedByTheAsynchronousPath` | A pending structural change is committed before the step is submitted, and the compose count is one per mutation, not per step. |

### `expect_refused_as_invalid_state`, `AnotherGraphIsRefusedWhileAStepIsPending`

A runtime has one engine, so one graph's pending step occupies it. While the step of the first graph
is held open at a gate, `run()` and `run_async()` on a second graph of the same
runtime must each report `InvalidState` and must not run the second graph's task. The first
graph's handle must then complete with a successful report. `expect_refused_as_invalid_state` checks
one `Result` for that code. `TheRuntimeIsFreeOnceTheStepHasSettled` holds the other half: the
second graph runs once the step is settled.

| Test | What it proves |
| --- | --- |
| `CancelOnAnIdleGraphSparesAnotherGraphsStep` | `cancel()` on a graph with no run in flight must leave another graph's pending step alone: the step's report says not cancelled. |
| `CancelStopsItsOwnPendingStep` | `cancel()` on the graph that owns the pending step must mark its report cancelled. |
| `CancelWithNoRunInFlightDoesNotMarkTheNextRun` | A `cancel()` sent while nothing runs must not mark the next run. |

### `expect_step_ended_by_its_graph`, `AGraphDestroyedWithAPendingStepEndsTheStep`, `ADynamicGraphDestroyedWithAPendingStepEndsTheStep`

A handle may outlive its graph. The graph is destroyed in an inner scope while its step is pending,
with the gate released just before, and the handle is destroyed at the end of the test. The buffer
must hold the task's write when the scope closes, which shows that the destructor waited for the
step before the plan died. `expect_step_ended_by_its_graph` then expects `pending()` false,
`ready()` true and `InvalidState` from `wait()`, none of which may read the dead graph.
`AGraphDestroyedWithAPendingStepLeavesTheRuntimeFree` runs a new graph on the runtime, which a step
left behind would refuse.

### The dropped regions

`Medium_AsyncStepDrop` records two regions that each hold one host task that waits at the gate and
then adds one to its own buffer, and starts a step. A test drops region 1 while both tasks wait. The
tests guard against a `drop()` that destroys the region at once: the settle then reads a freed
recorder, which AddressSanitizer reports.

| Test | What it proves |
| --- | --- |
| `DropRemovesTheRegionFromTheCount` | After `drop(1)`, `has_region(1)` is false and `region_count()` is one. |
| `ThePendingStepFinishesTheDroppedTask` | Releasing the gate and settling the handle succeeds with both buffers at one: the dropped region's task finished its write, and settling walked that region's per-step hooks. |
| `TheNextRunDoesNotRunTheDroppedRegionAndComposesOnce` | A following `run()` leaves the dropped region's buffer at one, raises the other to two and brings `compose_count()` to two. |

`Medium_AsyncStepRerecord` drops region 7 while its step waits at the gate, and `region(7)` then
records a task on a second buffer.

| Test | What it proves |
| --- | --- |
| `ThePendingStepRunsTheRecordingItStartedWith` | The pending step runs the first recording only. |
| `TheNextRunRunsTheRecordingMadeWhileTheStepWasPending` | The next run runs the second recording only, which shows that the map holds the new region while the old one is kept for the step. |

## test_access.cpp

The file tests Access: the regions it keeps in order and the observers of both sets.

| Test | What it proves |
| --- | --- |
| `RegionsKeepTheirOrder` | Regions given directly keep their order. |
| `BareRegionsCarryNoObserver` | Regions given directly carry no liveness observer. |
| `HandlesBringTheirObservers` | Handles given through Reads and Writes bring the observers of both. It needs a buffer, so it runs medium. |

## test_dispatch_events.cpp

A task body which submits several kernels is timed across every event it hands over. The three tests run
as two rows of `Medium_DispatchEvents`: the worker pool, on a runtime forced onto the scheduler, and the
in-order chain, on the program's runtime.

| Test | What it proves |
| --- | --- |
| `ABodyHandingOverTwoEventsIsTimedAcrossBoth` | A body handing over two events is timed from the earliest start to the latest end. |
| `ABodyHandingOverNoneIsTimedAsBefore` | A body handing over nothing is timed by the event it returns, as before. |
| `HandingAnEventOverOutsideABodyLeavesNoTraceInALaterRun` | Handing an event over outside any body does not throw, and the record of a later run is the returned event's alone. |
