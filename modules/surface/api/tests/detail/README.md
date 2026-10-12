# api tests: detail

The tests of the callable traits, the node emitters and the fault record of `api/detail/`.
They are part of `sr_api_tests`; [the page of the program](../README.md) says how to run it.

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
