# api tests: vocabulary

The tests of `Dynamic` and `LateCount`, the late-bound words of `api/vocabulary/`. They are part of `sr_api_tests`;
[the page of the module's tests](../README.md) says how to run them.

## test_dynamic.cpp

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
where the member is called, and each message names the call. The members take no bare pointer,
so no test hands them an interior pointer, a device address or a stack address.

The file covers the liveness of the buffer a `Dynamic` reads a device value from. `host_slot`
allocates the one-word `Residency::Host` buffer.

### `ADestroyedCountBufferRefusesTheRun`, `ADestroyedBaseBufferRefusesTheRun`

A graph records a rule with `sized_from_device(counter)` or `based_at_device(base)` and runs once.
The buffer is then destroyed and the next `run()` returns `DanglingResource`. The count case
also checks that the recorder watches two allocations, the target and the counter.

### `ADestroyedCountBufferRefusesAStateRule`

The same refusal for `add(sized_from_device(counter), state, kernel)`, whose observers the recorder
gathers in a different member.

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
numbers in the message, as an out-of-window `based_at()` does. The fill runs with the count
resolved to zero, so the output holds 0. The provider then returns 100, and the same graph runs and writes 100: a fault
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

## test_captures.cpp

The file tests In, Out and InOut: the pointer, the key and the observer each capture holds.

| Test | What it proves |
| --- | --- |
| `EachCaptureNamesItsBuffer` | In reads, Out writes, and both point at the buffer they name. |
| `InOutReadsAndWrites` | InOut both reads and writes. |

## test_coupled.cpp

The file tests Coupled: the state written and the states read, in argument order.

| Test | What it proves |
| --- | --- |
| `NamesTheStatesItWasGiven` | Coupled refers to the states it was given, in order. |

## test_dependencies.cpp

The file tests the dependency keys: a buffer's data, a state's two fields, and a region as given.

| Test | What it proves |
| --- | --- |
| `KeysFollowTheHandle` | A buffer reads and writes one key, and a state reads current and writes next. |
| `ReadsAndWritesCollectOneKeyEach` | Reads and Writes collect one key per handle, in order. |

## test_placement.cpp

The file tests On: a device by index, or the device of the handle it names.

| Test | What it proves |
| --- | --- |
| `AnIndexNamesItsDevice` | An index names that device and observes nothing. |
| `ABufferNamesItsDevice` | A buffer names its own device and is observed. |
