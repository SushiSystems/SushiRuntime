# api tests: detail

The tests of the callable traits, the node emitters and the fault record of `api/detail/`. They are part of `sr_surface_tests`;
[the page of the module's tests](../README.md) says how to run them.

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

## test_device_fault.cpp

The file covers `Detail::record_device_fault()` for values past a uint32. Two `static_assert`s pin
`saturated_fault_word()` at compile time.

### `AFieldPastAUint32IsStoredSaturated`

A record written with a base and a capacity of `UINT32_MAX + 5` holds `UINT32_MAX` in both words
and the count as given. `take_device_fault()` then returns an `InvalidGraph` error that names
`4294967295` as the start of the window, clears the record, and returns nothing when asked again.
The test is skipped where `std::size_t` is 32 bits wide.

## test_device_pass.cpp

The file covers `Detail::compiled_for_device_v`.

### `AnswersFalseOnlyForDpcppWithoutFsycl`

The value is false when the DPC++ headers are compiled without `-fsycl`, and true otherwise.

## test_emit_launch.cpp

The file covers `Detail::launch_reach()` for a rank-1 launch whose origin plus size does not fit
a `std::size_t`.

### `ReachSaturatesAtTheLargestSize`

An `Extent<1>` of 8 elements at origin 16 reaches 24. The same extent and an `NdRange<1>` at
origin `SIZE_MAX - 3` reach `SIZE_MAX`.

### `AnExtentWhoseEndWrapsIsRefusedAtAdd`

`add(extent, state, kernel)` with that extent over a State of 8 elements is refused with
`InvalidGraph` and records nothing. A wrapped sum would be 4, which is inside the State.

## test_emit_state.cpp

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

## test_before_step_hooks.cpp

The file tests BeforeStepHooks: hooks run in order with the step count, small and large alike.

| Test | What it proves |
| --- | --- |
| `HooksRunInOrderWithTheCount` | Every hook runs once per call, in the order added, with the count. |

## test_callable_checks.cpp

The file tests the kernel traits: which callable each launch form takes.

| Test | What it proves |
| --- | --- |
| `EachLaunchTakesItsIndex` | Checks the index type each launch form passes its kernel. |

## test_emit_captured.cpp

The file tests captured_node(): the kernel receives one pointer per capture.

| Test | What it proves |
| --- | --- |
| `TheKernelReceivesEachPointer` | The kernel reads one capture and writes the other at each index. |

## test_emit_device_sized.cpp

The file tests device_sized_node(): the launch covers the capacity and stops at the live width.

| Test | What it proves |
| --- | --- |
| `OnlyTheLiveIndicesRun` | Only the indices below the device-written live width run. |

## test_emit_dynamic.cpp

The file tests dynamic_node(): a late-bound description runs over the live window it is given.

| Test | What it proves |
| --- | --- |
| `TheKernelRunsOverTheLiveCount` | The kernel runs over the live count and keeps its bindings. |

## test_emit_host.cpp

The file tests host_node(): a host description that runs its function on the calling thread.

| Test | What it proves |
| --- | --- |
| `AHostNodeRunsItsFunction` | Checks the fields of the description and that its work runs once. |

## test_emit_late_count_resolver.cpp

The file tests late_count_resolver_node(): the fill receives each count's live value.

| Test | What it proves |
| --- | --- |
| `TheFillReceivesTheLiveCounts` | A fixed and a host count reach the fill as their live values. |

## test_emit_task.cpp

The file tests task_node(): a task description whose work receives the queue.

| Test | What it proves |
| --- | --- |
| `ATaskNodeRunsOnTheQueue` | Checks the fields of the description and that its work runs on the queue. |

## test_emit_tracked.cpp

The file tests launch_node(), range_node() and raw_node(): the width each states and the work each
runs.

| Test | What it proves |
| --- | --- |
| `ALaunchRunsOncePerIndex` | Holds the elements every launch here covers. */ constexpr std::size_t K_COUNT = 16; /** @brief Tests that a launch over a count runs the kernel once per index. |
| `ARangeKernelGetsTheWholeRange` | A range kernel is handed the whole range and the handler. |
| `ARawNodeKeepsTheStatedWidth` | A raw command group keeps the width the caller states. |

## test_emit_untracked.cpp

The file tests the untracked emitters: no regions, the device stated, and the work run.

| Test | What it proves |
| --- | --- |
| `AnUntrackedLaunchNamesItsDevice` | An untracked launch names its device and touches no region. |
| `RangeAndRawForms` | Checks the range and the raw forms: the width of the range, none for the raw. |

## test_handle_access.cpp

The file tests HandleAccess: the keys and the liveness of a Buffer and a State.

| Test | What it proves |
| --- | --- |
| `ABufferKeyIsItsData` | A buffer's key is its data and its liveness ends with it. |
| `AStateReadsCurrentAndWritesNext` | A state reads its current field and writes its next one. |

## test_late_controls.cpp

The file tests Detail::LateControls: an opaque name the surface holds only by pointer.

| Test | What it proves |
| --- | --- |
| `IsHeldByPointerOnly` | The name is a class type held by shared pointer. |

## test_late_count_resolution.cpp

The file tests late_count_source() and read_live_count(): the host path and its capacity bound.

| Test | What it proves |
| --- | --- |
| `HostCountsResolveOnTheHost` | A fixed count is its own live value, and a host count calls its provider. |
| `AHostCountAboveTheCapacityResolvesToZero` | A host count above the capacity is recorded as a fault and resolves to zero. |

## test_late_window.cpp

The file tests resolve_live_range() and read_late_window(): windows inside and past the capacity.

| Test | What it proves |
| --- | --- |
| `AWindowInsideTheCapacityResolves` | A window inside the capacity resolves to itself. |
| `NoHostCountTakesTheRestOfTheCapacity` | No host count covers the rest of the capacity from the base. |
| `AWindowPastTheCapacityIsAFault` | A window past the capacity is refused and recorded. |
| `TheWindowIsReadFromTheBindings` | The window read from bindings carries the count and the base set on them. |

## test_node_description.cpp

The file tests NodeDescription: a kernel of unknown width on device 0, with no regions or bindings.

| Test | What it proves |
| --- | --- |
| `DefaultsDescribeAnEmptyKernel` | Checks the defaults of a description. |

## test_node_spec.cpp

The file tests NodeSpec: it holds the description it was built from and an empty stamp.

| Test | What it proves |
| --- | --- |
| `KeepsItsDescription` | The spec keeps the description and starts with a default stamp. |

## test_node_stamp.cpp

The file tests NodeStamp and parameters_refusal(): what a stamp copies and the parameter limit.

| Test | What it proves |
| --- | --- |
| `CopiesWhatTheOptionsState` | A stamp copies the operation, the width and the parameter count. |
| `OneParameterPastTheLimitIsRefused` | Parameters within the limit pass and one past it is refused. |

## test_plan.cpp

The file tests Detail::Plan: an opaque name the surface holds only by pointer.

| Test | What it proves |
| --- | --- |
| `IsHeldByPointerOnly` | The name is a class type held by pointer. |

## test_step_machinery.cpp

The file tests StepMachinery: one view per state, and the fault records it reads.

| Test | What it proves |
| --- | --- |
| `OneViewPerState` | The same state gives the same view twice. |
| `AFaultRecordBecomesAnError` | A clean record reports nothing and a window fault reports an error. |

## test_step_run.cpp

The file tests StepPredicate and the context queries of step_run.hpp.

| Test | What it proves |
| --- | --- |
| `ThePredicateForwardsTheCount` | The predicate forwards the completed count to the wrapped callable. |
| `AnIdleRuntimeHasNoRunInProgress` | An idle runtime has no run in progress, and cancelling it is harmless. |

## test_assertion_logging.cpp

The file tests `install_fatal_logging()`. The two death tests run a failing `SR_VERIFY` in a child
process after the installation and read the child's standard error.

| Test | What it proves |
| --- | --- |
| `Medium_AssertDeathTest.LogsTheFailedCheckAtFatal` | A `[FATAL]` record carries the condition and the message. |
| `Medium_AssertDeathTest.TheDefaultHandlerStillWritesItsLineAfterTheLog` | The handler that was installed before still writes its line. |
| `Small_AssertHandler.InstallingTwiceIsHarmless` | A second installation returns and changes nothing. |

## description_runner.hpp

`run_description()` runs the work of a described node once on the runtime's first queue and waits:
a command group is submitted, queue work is called with no dependency, and host work is called on
the test's thread. `shared_integers()` allocates shared integers on that queue, so the memory and
the work share one context. The emitter tests use both, so they need no graph and no plan.

## test_graph_id.cpp

GraphId, the process-wide identity of one owner of plans.

| Test | What it proves |
| --- | --- |
| `Small_GraphId.IdsAreNonZeroDistinctAndMovedFromReportsZero` | Identities are non-zero and distinct, and a moved-from one reports 0. |
| `Small_GraphId.MoveAssignmentHandsTheIdentityOver` | Move assignment hands the identity over and leaves the source with none. |
| `Small_GraphId.SeveralThreadsNeverTakeTheSameId` | The counter never repeats an id, even when several threads take them at once. |
