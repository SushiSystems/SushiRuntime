# api tests: data

The tests of `Buffer` and `State`, the handles of `api/data/`. They are part of `sr_surface_tests`;
[the page of the module's tests](../README.md) says how to run them.

Every test that allocates a handle takes a host runtime from `TestSupport::HostRuntimeTest` and runs
small, except the death tests, the aligned-allocation rows and the device-resident tests that run a
kernel on the device, which take the program's runtime from `TestSupport::RuntimeTest`. The tests of
an adopting `Buffer` build it over a made-up address with `tests/support/adopted_handles.hpp`
and run small. The tests that stop the process are death tests and run medium.

## test_request_checks.cpp

The three predicates a buffer or State request is checked with, run small and without a stop. The
death tests of `test_buffer.cpp` keep the wiring: that each predicate, when it fails, stops the call.

| Test | What it proves |
| --- | --- |
| `AnAlignmentOfZeroOrAPowerOfTwoIsValid`, `AnAlignmentThatIsNotAPowerOfTwoIsInvalid` | `is_valid_alignment()` accepts 0, 1 and 64 and refuses 3 and 48. |
| `TheLargestCountWhoseByteSizeFitsFits`, `ACountWhoseByteSizeWrapsDoesNotFit` | `fits_byte_count()` accepts the largest count of 8-byte elements and refuses one past it, the case whose size wraps to 8. |
| `AnIndexBelowTheCountNamesADevice`, `AnIndexAtOrPastTheCountNamesNoDevice` | `names_a_device()` holds below the count and fails at it. |

## test_residency.cpp

The rule `operator[]` of a Buffer and a State checks, run small and without a stop:
`Detail::is_host_addressable()` holds for Shared and Host and fails for Device. The death test
`IndexingADeviceResidentHandleStops` of `test_buffer.cpp` keeps the wiring: that a device-resident handle stops.

| Test | What it proves |
| --- | --- |
| `SharedAndHostMemoryAreHostAddressable`, `DeviceMemoryIsNotHostAddressable` | The rule holds for Shared and Host and fails for Device. |

## test_buffer.cpp

The file covers what `Buffer` and `State` promise alike: what a range call does with a window, what
an allocation does with a count it cannot hold, the queries both answer, which device indices and
residencies are refused, and which element types are accepted. Most handles are shared USM on
device 0. The device-resident handles allocate on device 0 as the `Medium_DeviceResident` and
`Small_DeviceResident` tests do, so
the file needs no cluster and no more devices than that file.

### `window_pattern`, `expect_every_range_call_stops`

`window_pattern` returns the pattern of the line a range call writes when it stops: the call,
`offset <n>, count <n>` and `size <n>`. `expect_every_range_call_stops` runs `read_range`,
`write_range` and `region` of one handle as death tests against it. `expect_buffer_range_calls_stop`
and `expect_state_range_calls_stop` allocate the handle, so one row table serves the Buffer and the
State.

### `Medium_DataHandleWindowDeathTest.StopsOnAWindowThatDoesNotFit`

One row per handle and window. A window `{size, 1}` starts one past the end, and `{size - 2, 5}`
starts inside and runs over, for a Buffer and for a State. An offset or a count near the largest
`std::size_t` makes `offset + count` wrap to a small number; the Buffer rows with those windows stop
all the same. Each row stops all three range calls, and the line holds the offset, the count and the
size.

### `EmptyWindowAtTheEndOfABufferIsAccepted`, `EmptyWindowAtTheEndOfAStateIsAccepted`

A window `{size, 0}` is valid: `read_range` returns an empty vector, `write_range` accepts an empty
span with a null pointer, and `region` returns a zero-length region at the end.

### `FittingWindowRoundTrips`

The last five elements are written and read back, and the element before the window keeps its
value.

### `ANullSourceWithACountStopsABuffer`, `ANullSourceWithACountStopsAState`, `AShortSourceStops`

`write_range` with a count of four stops the process for a null source, on a Buffer and on a
State, and for a Buffer source of two elements.

### `OverflowingElementCountStopsBeforeAllocating`

The count is chosen so that `count * sizeof(double)` wraps to eight bytes, a size any allocator
grants. `Runtime::buffer` and `Runtime::state` stop the process with the count in the message.

### `StateAndBufferReportTheSameMetadata`, `StateRegionNamesAWindowOfTheCurrentField`

`device_index`, `size`, `empty`, `residency` and `is_host_accessible` return the same on a State and
on a Buffer allocated alike. `State::region` names a window of the current field: its base, its byte
offset and its byte length.

### `TriviallyCopyableStructRoundTripsThroughABuffer`, `TriviallyCopyableStructRoundTripsThroughAState`

A struct of two floats instantiates `Buffer` and `State`, and a window written with `write_range`
reads back equal through `read_range`. The values are dyadic and the copy moves bits, so the
comparison is exact. The negative case, a `T` that is not trivially copyable, is a compile error
from the `static_assert` in `Buffer`, so no test can run it.

### `Medium_DataHandleResidencyDeathTest.HostOnADeviceResidentHandleStops`

One row for a Buffer and one for a State: `host()` on a device-resident handle stops the process,
and the line names `Buffer::host` or `State::host`.

### `IndexingADeviceResidentHandleStops`

`operator[]` on a device-resident Buffer or State stops the process. It is checked with `SR_ASSERT`,
so the test is compiled only where `SR_ENABLE_ASSERTIONS` is 1.

### `DeviceIndexThatNamesNoDeviceStops`

`buffer()` and `state()` with a device index equal to the device count stop the process. The
line names `Runtime::buffer` or `Runtime::state`, the index, the device count and
`advanced().devices()`.

### `Medium_DataHandlesAlignment.AlignedAllocationStartsOnAMultipleOfTheAlignment`

One row per residency. `buffer()` and `state()` are asked for 37 floats aligned to 128 bytes. The
buffer's address and both fields of the state must be multiples of 128. The fixture skips a row
whose residency the device does not offer, and the `Shared` row where the toolchain's aligned shared
allocation ignores the alignment; the run reports those rows as skipped.

### `AlignmentThatIsNotAPowerOfTwoStops`

An alignment of 48 stops the process in `buffer()` and in `state()`; the line names the call and
the value.

The file covers device-resident state, work package WP-6 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN/`. A buffer or a state allocated with
`Residency::Device` stays on its device and is not host-addressable. The host seeds it and reads it
back through `write_range()` and `read_range()`, while kernels run against it exactly as they do
against shared USM.

### `BufferRoundTrip`

A host seed, a kernel that doubles each element and a host readback round-trip correctly through a
device-resident buffer without the host ever indexing the allocation.

### `ABufferAllocatedOnTheDeviceIsNotHostAccessible`

A buffer allocated with `Residency::Device` reports that residency and is not host-accessible.

### `ReadRangeOfADeviceBufferReturnsOnlyTheWindow`

`read_range` pulls a window of a device-resident buffer without copying the whole buffer.

### `StateStepsResident`

A device-resident `State` steps with the fluent `add_rule(state, kernel)` rule and is seeded and read back
through its range API. The field stays resident across all steps, and only the windowed readback
touches the host.

### `DirectHostIndexingOfASharedBufferIsSeenByReadRange`

The host writes a shared buffer by direct indexing and `read_range` returns what it wrote.

### `HostSpanIndexesSharedBuffer`

`host()` is the branch-free bulk access to shared USM: one residency check, then unchecked indexing
over the whole buffer.

### `HostBufferIsWrittenByAKernelAndReadOnTheHost`

A host-resident buffer is host-addressable, a kernel on its device writes it, and the host reads the
result by direct indexing.

### `Small_Buffer.AdoptingConstructorExposesTheAdoptedAddress`, `DestructionReleasesTheAdoptedAllocationOnce`, `MovingAnAdoptedBufferReleasesTheAllocationOnce`

A `Buffer` adopts a block at a made-up address whose owner is a `RecordingAllocationOwner`. It
exposes that address, releases it once when destroyed, and a moved-to `Buffer` takes the address and
releases it once while the moved-from one is empty.

### `Medium_BufferDeathTest.AdoptingATooSmallAllocationStops`, `AdoptingACountThatOverflowsItsByteSizeStops`

Adopting a block of 4 bytes for 4 elements of 4 bytes stops the process, and so does a count whose
byte size overflows `std::size_t`. The lines name the adoption and the count.

## test_state.cpp

The file covers a State that moves after `add()` recorded a rule on it. `seeded_state` fills the
current field with the index plus `K_SEED` and the next field with `K_CANARY`, `add_increment`
records `next = current + 1` over the whole field, and `current_words` and `seeded_words` compare
the current field with the seed plus a number of steps. Three `static_assert` lines state that a
State moves without throwing and does not copy.

### `ARuleFollowsAStateMovedAfterAdd`

A rule is recorded, the State is moved into a new variable, and three steps run. The moved-to State
holds the seed plus three. A recorder that kept the address of the State read a moved-from object
here.

### `ARuleFollowsAStateItsVectorReallocated`

The rule is recorded on element 0 of a `std::vector<State>`, and `push_until_reallocated` pushes
States until the address of element 0 changes. Three steps then advance element 0 and leave element 1 as seeded.

### `ACoupledRuleFollowsItsMovedStates`

`add_coupled(Coupled(out, input), kernel)` is recorded and both States are moved. Two steps leave three times
the seed in the State written and the seed in the State read.

### `ADynamicStateRuleFollowsAMovedState`

`add_dynamic(sized(...), state, kernel)` is recorded and the State moved. Three steps advance the live prefix.

### `AStateAssignedOverRefusesTheRun`

A State with a recorded rule is move-assigned a new State. The next `run()` returns
`DanglingResource`.

### `ADynamicStateRuleReplayedAfterAStepReadsTheCurrentField`

The test guards against a late-bound State rule that takes its read key when it is recorded and
its write key at each replay. After one step the two fields have swapped, so a plan rebuilt then
would register the rule as a reader and a writer of the same field and not as a reader of the
current one.

The graph holds a kernel that writes `K_MARK` into the State's second field, named with
`Writes(state)`, and then the late-bound rule `next = current + 1`. After one step the second field
is current. The test renames the graph, which makes the plan stale, and runs a second step: every
element must hold `K_MARK + 1`. The values depend on the order only when the scheduler runs the two
unordered nodes the other way round, which the rebuilt plan must not allow.
