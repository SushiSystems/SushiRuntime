# api tests: data

The tests of `Buffer` and `State`, the handles of `api/data/`.
They are part of `sr_api_tests`; [the page of the program](../README.md) says how to run it.

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

The test guards against a late-bound State rule that takes its read key when it is recorded and
its write key at each replay. After one step the two fields have swapped, so a plan rebuilt then
would register the rule as a reader and a writer of the same field and not as a reader of the
current one.

The graph holds a kernel that writes `K_MARK` into the State's second field, named with
`Writes(state)`, and then the late-bound rule `next = current + 1`. After one step the second field
is current. The test renames the rule, which makes the plan stale, and reads `graph.plan()`: the
rule's node must read the second field and have one predecessor, the writer of that field. It then
runs a second step and expects `K_MARK + 1` everywhere and two compiles in all.
The predecessor count is the deterministic check; the values depend on the order only when the scheduler
runs the two unordered nodes the other way round.
