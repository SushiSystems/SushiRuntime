# api tests: vocabulary

The tests of `Dynamic` and `LateCount`, the late-bound words of `api/vocabulary/`, and of the small
words the fluent calls take: `In`, `Out`, `InOut`, `Coupled`, `Reads`, `Writes` and `On`. They are part
of `sr_surface_tests`; [the page of the module's tests](../README.md) says how to run them.

Each small test takes a host runtime from `TestSupport::HostRuntimeTest`. A death test row that
names a slot takes its three buffers from `TestSupport::make_slot_buffers`, one device-resident, one
shared and one host-resident.

## test_captures.cpp, test_coupled.cpp, test_dependencies.cpp, test_placement.cpp

| Test | What it proves |
| --- | --- |
| `Small_Captures.EachCaptureNamesItsBuffer` | `In` and `Out` point at the buffer they name, `In` carries its data address as its key, and `In` observes the buffer. The `reads` and `writes` flags of `In`, `Out` and `InOut` are `static_assert` lines at the top of the file. |
| `Small_Coupled.NamesTheStatesItWasGiven` | `Coupled` refers to the states it was given, in order. The deduction of its element types is a `static_assert` at the top of the file. |
| `Small_Dependencies.ARegionIsItsOwnKeyAndHasNoObserver` | `Reads` over a `BufferRegion` keeps the region as its key and watches nothing. |
| `Small_DependenciesOfHandles.ABufferReadsAndWritesTheSameKey` | A buffer reads and writes one key, the address of its data. |
| `Small_DependenciesOfHandles.AStateReadsCurrentAndWritesNext` | A state reads its current field and writes its next field. |
| `Small_DependenciesOfHandles.ATypedHandleIsObserved` | A buffer passed to `Reads` is observed, so a graph can tell when it is destroyed. |
| `Small_DependenciesOfHandles.ReadsAndWritesCollectOneKeyEach` | `Reads` collects one key per handle, in argument order. |
| `Small_Placement.ABufferNamesItsDevice` | `On` over a buffer names the device of the buffer and observes it. |

## test_dynamic.cpp

The file covers the persistent-graph, late-binding path, work package WP-1 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN/`. A graph whose live size or per-step participation
changes between steps must be compiled exactly once and replayed, never recompiled. Each test
asserts the numerical result against a vector built on the host, and where the plan changes between
runs, `Graph::compile_count() == 1`.

### `Small_LateBindings`

| Test | What it proves |
| --- | --- |
| `DynamicSizePerElement` | `sized()` shrinks the iteration range to a live prefix on each step. Driving the live count from 1 to `CAP` over `CAP` runs increments `buf[i]` exactly on the steps where `i` is inside the live range, so `buf[i] == CAP - i`, with one compile. |
| `EnableTogglePerElement` | `when()` gates whether the node runs at all. Toggling the predicate over ten steps runs the increment only on the five enabled steps. |
| `DisabledNodeStillReleasesSuccessors` | A disabled step must release its successors so that the rest of the graph still runs. Upstream writes the gate buffer and downstream reads it, so the tracker orders downstream after upstream; downstream advances on every step. |
| `DynamicSizeState` | `sized()` works on a stepped `State`: the live prefix grows on each step while the graph is compiled once, and the first two cells accumulate one increment per step. |
| `ComposedWhenAndSized` | `when()` and `sized()` compose. Element 0 is incremented once per step that is both enabled and in range. |
| `BaseOffsetIteratesASlice` | Rows of four windows: `based_at()` with `sized()` runs exactly the window, and the callable receives absolute indices. Graph colouring has this shape: each colour is a contiguous slice of one constraint buffer. |
| `MovingTheWindowDoesNotRecompile` | Moving the window between two runs leaves `compile_count()` at one. |
| `BaseAndSizeBothFromTheDevice` | `based_at_device()` and `sized_from_device()` take the offset and the length from slots a predecessor wrote; the slot is registered as a read, so the producer is ordered first without the caller naming it. |
| `ALaterWindowThatFitsRunsAfterOnePastTheCapacityFailed` | A window that runs off the end of the capacity fails the run, and the next run with a window that fits succeeds. That the failure is `InvalidGraph` and names what the window asked for is held by `Small_LateWindow.AWindowPastTheCapacityIsAFault`. |

### `Medium_LateBindingsDeathTest`

Rows of nine calls, each of which stops the process at the call site and not in the middle of a run:
`based_at_device` on a device-resident buffer, on a shared buffer and on an index past the slot;
`sized_from_device` on the same three; and the member `Dynamic::sized_from_device` on a device-resident
and a shared buffer, and the member `Dynamic::based_at_device` on an index past the slot.

### `sized_from_device`

The file covers `sized_from_device`: a task whose iteration size is a count that a predecessor
kernel produced within the same step.

A collision pipeline has this shape. The broadphase writes a pair count, the narrowphase iterates
exactly that many pairs and writes a manifold count, and the solver iterates exactly that many
manifolds. None of those numbers exists when the step boundary polls the host-side `sized()`
providers. Without `sized_from_device` the choices are a readback in the middle of the step or a
dispatch at the worst case on every tick.

| Test | What it proves |
| --- | --- |
| `Small_DeviceCountProducer.ConsumerIteratesTheCountTheProducerJustWrote` | Rows of five multipliers (5, 200, 1, 0 and 1000, times 3). A producer kernel writes a count it computed on the device, and a consumer sized from that count iterates exactly that many elements in one `run()`, with one compile. The counter is registered as a read of the consuming task, so the consumer is ordered after the producer though the caller named neither; without that edge it would read the count before it was written. |
| `Small_DeviceCount.ChangingTheDeviceCountKeepsOneCompile` | A count the host writes between two runs leaves `compile_count()` at one. |
| `Small_DeviceCount.ChainedProducerCountsInOneComposition` | A three-stage chain: the output count of each stage is the iteration size of the next, all in one `run()` with one compilation. |
| `Small_DeviceCountWhen.AnEnabledStepRunsTheLiveCount`, `ADisabledStepRunsNoElement`, `AStepEnabledAgainRunsTheLiveCount` | `when()` still composes with a size driven by the device. A disabled step runs nothing, whatever the counter says, and the step enabled again runs the count. |
| `Small_DeviceCount.CountOverCapacityFailsTheRun`, `ACleanGraphRunsAfterACountOverCapacityFailedARun` | A count larger than the compiled capacity fails the run. Truncating would silently drop work. A clean graph on the same runtime then runs. |
| `Small_DeviceCount.ReadsTheCountAtTheGivenIndex` | A count is read from the element the index names, not from element zero. |
| `Small_DeviceCount.AcceptsABaseSlotAtTheGivenIndex` | A base is read from the element the index names, and the node runs from there to the end of the capacity. |

### Slot liveness

A graph watches the buffer a `Dynamic` reads its count or base from.

| Test | What it proves |
| --- | --- |
| `Small_DeviceSlotLiveness.ADestroyedSlotBufferRefusesTheRun` | Rows of the count buffer of a task, the base buffer of a task and the count buffer of a State rule: once the buffer is destroyed, the next run is refused with `DanglingResource`. |
| `Small_DeviceSlotRecorder.TheRecorderWatchesTheTargetAndTheCounter` | A dynamic task over a target and a counter makes the recorder watch two allocations. |

## test_late_count.cpp

The file covers `LateCount`, a run count resolved once per step from exactly one source, and what
`LateCount` and `Dynamic` let a caller write.

A set of `static_assert` lines at the top state the shape of the two types: a `LateCount` is built by
a factory only, copies, and has no assignable capacity; a `Dynamic` moves, does not copy, and has no
public control and no raw-pointer size setter.

### Where `Recorder::resolve()` takes a live value from

`record_live_count_copy` records the resolver of one late count; its fill copies the live value to
`out[0]`. The cases that a count below its capacity, and a fixed count, resolve to themselves are held
small by `HostCountsResolveOnTheHost` in `tests/surface/api/detail/test_late_count_resolution.cpp`.

| Test | What it proves |
| --- | --- |
| `Small_LateCountResolved.ChangingHostCountKeepsOneCompile` | A host count that changes over three runs leaves `compile_count()` at one. |
| `Small_LateCountResolved.HostCountAboveCapacityFailsTheRunWithInvalidGraph` | The run fails with `InvalidGraph` and a message that names the live count and the capacity. That the count then reaches the fill as zero is held by `AHostCountAboveTheCapacityResolvesToZero`. |
| `Small_LateCountResolved.ARunGoesOnOnceTheHostCountFitsAgain` | The same graph runs once the provider returns a count that fits. |
| `Small_LateCountResolved.DeviceSlotResolvesFromHostResidentBuffer` | A `Residency::Host` device slot resolves to the value written there. |
| `Small_LateCountOverCapacity.DeviceCountAboveCapacityFailsTheRun`, `ALaterGraphRunsCleanAfterAFailedRun` | A device count above its capacity fails the run, and a later graph with a count inside the capacity runs clean. A host runtime has one layer, so there are no layer rows. |
| `Small_LateCountProducer.ResolverSeesTheCountItsPredecessorJustWrote` | Rows of two counts in turn (300 then 700, 700 then 0). The resolver reads the count its own step's producer wrote, so a changed count takes effect the same run and not one run late. A device kernel cannot reach the test's frame, so the producer's next count is in a buffer. |

### What a caller can build

| Test | What it proves |
| --- | --- |
| `Medium_LateCountDeathTest.ASlotThatCannotBeReadStopsTheCallThatNamesIt` | Rows of `from_device` on a device-resident buffer, on a shared buffer and on an index one past the slot. The residency rows ask for `Residency::Host`; the index row names the buffer's size. |
| `Small_LateCountFromDevice.AcceptsTheLastIndexOfItsSlot` | The last index of the slot is accepted and the capacity is the one given. |
| `Medium_LateCountEmptyProviderDeathTest.FromHostStopsOnAnEmptyProvider` | `from_host` stops on a provider that holds no callable. It needs no device. |
| `Medium_LateCountCapacityDeathTest.AFactoryStopsOnACapacityPastAUint32` | Rows of `fixed`, `from_host` and `from_device` over a capacity a uint32 live-count word cannot hold. The fixture skips on a target whose `size_t` is as narrow as a uint32. |
| `Small_LateCount.FixedAcceptsTheLargestCapacityAUint32Holds` | The largest capacity a uint32 holds is accepted. |
| `Small_LateBoundVocabulary.ARecordedCopyKeepsTheHostProvider` | A recorded copy still polls the provider after the original is destroyed, on every step. |
| `Small_LateBoundVocabulary.FromDeviceReadsTheWordAtItsIndex` | `from_device` reads the word its index names, not the first word. |
| `Small_LateBoundVocabulary.ASizeSetAfterADeviceSizeWins`, `ADeviceSizeSetAfterASizeWins` | The size source set last decides the live size, whichever kind it is. |
| `Small_LateBoundVocabulary.MembersChainOnANamedDynamic`, `ADisabledNamedDynamicRunsNoElement` | The members chain on a named `Dynamic` and carry `when()`, `sized()` and `based_at()`; a `Dynamic` disabled through `when()` runs no element. |
