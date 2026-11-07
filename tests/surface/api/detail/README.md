# api tests: detail

The tests of the callable traits, the node emitters and the fault record of `api/detail/`. They are part of `sr_surface_tests`;
[the page of the module's tests](../README.md) says how to run them.

A test here that runs a described node allocates its data through `Runtime::buffer`. When the
description has a host form (`NodeDescription::host_form`), the test calls it on a host runtime from
`TestSupport::HostRuntimeTest` and runs small. A description with no host form (a raw command group,
a queue body, a device-sized launch, a late-count resolver) takes the program's runtime from
`TestSupport::RuntimeTest` and runs medium. A test that builds a description and reads its fields
touches no device and runs small. Headers whose tests were removed are compiled alone by the
`compile_*.cpp` files below.

## description_runner.hpp

`run_description()` runs the work of a described node once on the runtime's first queue and waits:
a command group is submitted, queue work is called with no dependency, and host work is called on
the test's thread. `queue_of()` returns that queue through `Runtime::advanced()`.
`bound_controls(dynamic)` builds the late controls of a `Dynamic` with `make_late_bindings()`,
registers its providers on a `StepMachinery` and runs the machinery's `before_step()`, so the
controls hold the count and the base the providers returned. The emitter tests use them, so they
need no graph and no plan.

## compile_late_controls.cpp, compile_node_description.cpp, compile_node_spec.cpp, compile_plan.cpp, compile_step_machinery.cpp

Each file includes one header of `api/detail/` and nothing else, so the header carries every include
it needs. The headers declare an opaque name held by pointer, defaults, a value holder or a class
whose promises are shown by the graph tests (`graph/test_plan_view.cpp` for the node table,
`vocabulary/test_dynamic.cpp` for a window past the capacity, `data/test_state.cpp` for a State that
moves).

## test_assertion_logging.cpp

The file tests `install_fatal_logging()`. Each test runs a failing `SR_VERIFY` in a child process
after the installation and reads the child's standard error, so all three are medium death tests.

| Test | What it proves |
| --- | --- |
| `LogsTheFailedCheckAtFatal` | A `[FATAL]` record carries the condition and the message. |
| `TheDefaultHandlerStillWritesItsLineAfterTheLog` | The handler that was installed before still writes its line. |
| `ASecondInstallationLogsTheFailedCheckOnce` | After two installations the child's standard error holds `[FATAL]` once; a second handler chained to the first would write it twice. |

## test_before_step_hooks.cpp

The file tests `BeforeStepHooks`. It touches no device, so both tests are small.

| Test | What it proves |
| --- | --- |
| `HooksRunInOrderWithTheCount` | Every hook runs once per call, in the order added, with the count; the second hook captures 256 bytes. |
| `AMovedObjectKeepsItsHooks` | A moved `BeforeStepHooks` runs the hooks it was handed. |

## test_callable_checks.cpp

The file tests the kernel traits: which callable each launch form takes.

| Test | What it proves |
| --- | --- |
| `EachLaunchTakesItsIndex` | `is_launch_kernel_v` is true for a kernel that takes the index its launch passes (a count, an `Extent<2>` and an `NdRange<1>`) and false for a count launch given an `id` kernel; `accepts_kernel` agrees. |

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
`saturated_fault_word()` at compile time, and a third refuses to compile the file where
`std::size_t` is not wider than a uint32.

| Test | What it proves |
| --- | --- |
| `AFieldPastAUint32IsStoredSaturated` | A record written with a base and a capacity of `UINT32_MAX + 5` holds `UINT32_MAX` in both words and the count as given. |
| `TakingASaturatedRecordNamesItsStartAndClearsIt` | `take_device_fault()` returns an `InvalidGraph` error that names `4294967295` as the start of the window, clears the record, and returns nothing when asked again. |

## test_device_pass.cpp

The file covers `Detail::compiled_for_device_v`.

### `IsFalseInAUnitBuiltWithoutFsycl`

The expected value is `K_UNIT_COMPILED_WITH_FSYCL`, which the test states from the build: the units
in `DEVICE_SOURCES` of `tests/surface/api/CMakeLists.txt` get `-fsycl`, and this file is not one of
them. Adding it to that list makes the test fail until the constant changes.

## test_dropped_outcome.cpp

The file tests `log_dropped_outcome`: the WARN a step nobody waited for leaves when it failed.

| Test | What it proves |
| --- | --- |
| `LogsTheErrorAndWhereItWasDropped` | The line names the error, its message and the call that dropped it. |

## test_emit_captured.cpp

The file tests `captured_node()`: the kernel receives one pointer per capture.

| Test | What it proves |
| --- | --- |
| `TheKernelReceivesEachPointer` | The kernel reads one capture and writes the other at each index, and the description states the width. |

## test_emit_device_sized.cpp

The file tests `device_sized_node()`: the launch covers the capacity and stops at the live width.

| Test | What it proves |
| --- | --- |
| `OnlyTheLiveIndicesRun` | Only the indices below the device-written live width run; the rest keep their value. |

## test_emit_dynamic.cpp

The file tests `dynamic_node()`: a late-bound description runs over the live window it is given. The
controls come from `bound_controls(sized(...))`.

| Test | What it proves |
| --- | --- |
| `TheKernelRunsOverTheLiveCount` | The kernel runs over the live count only, and the description keeps the controls it was given. |

## test_emit_host.cpp

The file tests `host_node()`: a host description that runs its function on the calling thread. It
touches no device, so both tests are small.

| Test | What it proves |
| --- | --- |
| `AHostNodeDescribesItsKindWidthAndWrites` | The description is a host node with the width and the written region it was given and holds worker work. |
| `AHostNodeRunsItsFunctionOnce` | Its work calls the function once. |

## test_emit_late_count_resolver.cpp

The file tests `late_count_resolver_node()`: the fill receives each count's live value.

| Test | What it proves |
| --- | --- |
| `TheFillReceivesTheLiveCounts` | A fixed and a host count reach the fill as their live values. |

## test_emit_launch.cpp

The file covers the reach `detail/emit_launch.hpp` computes for a launch whose origin plus size may
not fit a `std::size_t`, and the refusal made from it.

| Test | What it proves |
| --- | --- |
| `Small_LaunchReach.ReachIsOriginPlusSizeWhenItFits` | An `Extent<1>` of 8 elements at origin 16 reaches 24. |
| `Small_LaunchReach.ReachSaturatesAtTheLargestSizeWhenTheSumWraps` | The same extent and an `NdRange<1>` at origin `SIZE_MAX - 3` reach `SIZE_MAX`, and `saturated_reach(SIZE_MAX - 7, 8)` is `SIZE_MAX`. |
| `Small_LaunchReach.ALaunchPastItsHandleIsRefused` | `launch_refusal()` returns an `InvalidGraph` error for a wrapping extent over a handle of 8 elements. |
| `Small_LaunchReach.ALaunchInsideItsHandleIsNotRefused` | It returns nothing for an extent that fits. |
| `Small_LaunchReachRecord.ARefusedLaunchRecordsNoRule` | `add_rule(extent, state, kernel)` with a wrapping extent keeps a refusal and records no rule. A wrapped sum would be 4, which is inside the State. |

## test_emit_state.cpp

The file covers the nodes `detail/emit_state.hpp` and `detail/emit_dynamic.hpp` build for State
and coupled rules. `seeded_state` fills the current field with the index plus `K_SEED` and the next
field with `K_CANARY`, so an element a kernel did not write is recognisable. The cases that build a
description outside a recorder call its `host_form` and read the next field, since no
advance is registered.

| Test | What it proves |
| --- | --- |
| `ACoupledRuleReadsItsInputsThroughConstPointers` | `add_coupled(Coupled(out, left, right), kernel)` with a kernel that takes both inputs as `const Word*` writes `current + a * b` into `out` and leaves both inputs as they were. |
| `StateCoupledAndUntrackedNodesReportTheirLaunchWidth` | A profiled graph holds a State rule, a coupled rule with a count and an untracked launch. Their node events report the State's size and the two counts as `launched_width`, compared sorted because the nodes share no region. |
| `AStateNodeBuiltFromItsViewRunsTheRule` | `state_launch_node()` over the State's view writes `current + 1` into the first `K_PREFIX` words of the next field and leaves the rest at `K_CANARY`. |
| `ACoupledNodeBuiltFromItsViewsRunsTheRule` | `coupled_launch_node()` writes `current + 2 * other` into the next field. |
| `ADynamicStateNodeBuiltFromItsViewRunsTheLiveWindow` | `dynamic_state_node()` with controls that bind a count of `K_PREFIX` increments those words and keeps `K_CANARY` past them. |

## test_emit_task.cpp

The file tests `task_node()`: a task description whose work receives the queue.

| Test | What it proves |
| --- | --- |
| `Small_EmitTask.ATaskNodeDescribesItsKindWriteAndWork` | The description is a task node with the written region it was given and holds queue work. |
| `Medium_EmitTask.ATaskNodeRunsOnTheQueue` | Its work runs on the queue it is handed and stores its value. |

## test_emit_tracked.cpp

The file tests `launch_node()`, `range_node()` and `raw_node()`: the width each states and the work
each runs. The description tests and the launch run test are small; the range and raw run tests use the program's runtime.

| Test | What it proves |
| --- | --- |
| `Small_EmitTracked.ALaunchNodeStatesItsKindAndWidth` | A launch is a kernel node of the width it was given. |
| `Small_EmitTrackedRun.ALaunchRunsOncePerIndex` | A launch over a count runs the kernel once per index. |
| `Small_EmitTracked.ARangeNodeStatesItsWidth` | A range description states the width it was given. |
| `Medium_EmitTracked.ARangeKernelGetsTheWholeRange` | A range kernel is handed the whole range and the handler. |
| `Small_EmitTracked.ARawNodeKeepsTheStatedWidth` | A raw command group keeps the width the caller states. |
| `Medium_EmitTracked.ARawNodeRunsItsCommandGroup` | The command group of a raw description runs. |

## test_emit_untracked.cpp

The file tests the untracked emitters: no regions, the device stated, and the work run.

| Test | What it proves |
| --- | --- |
| `Small_EmitUntrackedRun.AnUntrackedLaunchNamesItsDevice` | An untracked launch names its device, touches no region and runs its kernel. |
| `Small_EmitUntracked.ARangeFormStatesItsWidthAndDevice` | The range form states the width and the device it was given. |
| `Small_EmitUntracked.ARawFormIsUntrackedOnItsDevice` | The raw form is untracked and states the device it was given. |

## test_graph_id.cpp

`GraphId`, the process-wide identity of one owner of plans.

| Test | What it proves |
| --- | --- |
| `Small_GraphId.IdsAreNonZeroAndDistinct` | Two identities are non-zero and differ. |
| `Small_GraphId.AMovedFromIdReportsZero` | A moved-from identity reports 0 and the moved-to one keeps the value. |
| `Small_GraphId.MoveAssignmentHandsTheIdentityOver` | Move assignment hands the identity over and leaves the source with none. |
| `Medium_GraphId.SeveralThreadsNeverTakeTheSameId` | The counter never repeats an id when four threads take 500 each at once. |

## test_handle_access.cpp

`HandleAccess`: the keys and the liveness of a Buffer and a State. The handles adopt made-up
addresses through `tests/support/adopted_handles.hpp`, so no case touches a device.

| Test | What it proves |
| --- | --- |
| `ABufferKeyIsItsData` | A buffer's key is its data address. |
| `ABufferLivenessEndsWithIt` | A buffer's liveness observer expires when the buffer is destroyed. |
| `AStateReadsCurrentAndWritesNext` | A state's read key is its current field and its write key its next one. |

## test_late_count_resolution.cpp

The file tests `late_count_source()` and `read_live_count()`: the host path and its capacity bound.

| Test | What it proves |
| --- | --- |
| `HostCountsResolveOnTheHost` | A fixed count is its own live value, and a host count calls its provider. |
| `AHostCountAboveTheCapacityResolvesToZero` | A host count above the capacity is recorded as a fault and resolves to zero. |

## test_late_window.cpp

The file tests `resolve_live_range()` and `read_late_window()`: windows inside and past the capacity.

| Test | What it proves |
| --- | --- |
| `AWindowInsideTheCapacityResolves` | A window inside the capacity resolves to itself. |
| `NoHostCountTakesTheRestOfTheCapacity` | No host count covers the rest of the capacity from the base. |
| `AWindowPastTheCapacityIsAFault` | A window past the capacity is refused and recorded. |
| `AnUnboundWindowReadsAsTheRestOfTheCapacity` | Controls built from a `Dynamic` that states no count read as the rest of the capacity. |
| `ABoundWindowCarriesTheCountAndTheBaseItWasGiven` | Controls built from `sized(4).based_at(1)` read a count of 4 and a base of 1. |

## test_node_stamp.cpp

The file tests the parameter limit of a node, shown through `Graph::add`: `Small_NodeParameterLimit`
runs one row at `NodeOptions::K_MAX_PARAMETERS` parameters and one row past it.

| Row | What it proves |
| --- | --- |
| `AtTheLimit` | The add is accepted and the graph holds one node. |
| `OnePastTheLimit` | The add is refused and the graph holds no node. |

The copy of the operation, the placement, the determinism and the name into a node is read from the
plan in `graph/test_plan_view.cpp`, and the width in `graph/test_node_options.cpp`. The copy of the
parameters has no public reader and is in the backlog.

## test_step_run.cpp

The file covers the run entries the calls of `step_run.hpp` serve, through `Graph`.

| Test | What it proves |
| --- | --- |
| `RunUntilStepsOncePerFalseAnswerOfItsStopTest` | `run_until` with a stop test that answers false twice runs a host node twice. |
| `AGraphRunsAfterAnotherGraphsRunHasReturned` | A second graph's run is accepted once the first graph's blocking run has returned. |
| `CancellingAGraphWithNoStepPendingChangesNothing` | `cancel()` on a graph with no step pending leaves its next run to run its node. |
