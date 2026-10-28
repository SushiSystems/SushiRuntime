# task tests

The tests of the `task` module, mirroring `modules/engine/task/`. `tests/engine/task/CMakeLists.txt`
builds them into `sr_task_tests`. `sr test` runs them with the rest;
`sr run sr_task_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_node_placement.cpp

The file covers the placement a node states and the rule `Internal::Graph::placement_of()` applies to it:
a stated placement wins over the owner of the first write region, else of the first read region,
when the two share a SYCL context, and is refused when they do not; a node that states nothing runs
on the owner; a node with no owner runs on its stated placement, else device 0. `modules/engine/scheduler/README.md` states the rule under "Queue selection", and the
scheduler's `tests/engine/scheduler/test_task_scheduler.cpp` checks the queue a dispatch receives.

### `Medium_NodePlacementState`

The two tests need no allocator. `place_on()` stores an index and `stated_placement()` returns it;
an index the 16-bit field cannot hold throws `ErrorCode::InvalidGraph` with the index in the message
and leaves the earlier placement in force.

### `Medium_NodePlacementRule`

The fixture holds a `USMAllocator` over two queues on the device SYCL selects by default, so device
indices 0 and 1 both exist. No test allocates device memory: `adopt()` records a host address as
owned by one of the two, and an address with no record stands for a region no device owns. Both
queues are built on one explicit context, so a stated device in the owner's context wins.

### `Medium_NodePlacementContexts`

Each test builds its own allocator over two queues on the default device, each queue in a context of
its own, so the refusal needs no second device. A stated device in the other context is an
`InvalidGraph` error naming the owner; stating the owner itself is accepted. A stated index past
the allocator's devices is returned as stated, for `place_for_dispatch()` to refuse with the count.

## test_small_event_vector.cpp

The file covers `Internal::Graph::SmallEventVector`, the event dependencies a node holds: copy, move,
assignment from another vector or from a `std::vector`, and `clear()`. The events are
default-constructed. Each one has its own state, so `==` tells two of them apart and a copy of
one compares equal to it; the tests rely on that.

### `Small_SmallEventVector`

Each test compares `to_vector()` with the list it built. The assignment tests cover a list that
fits the inline buffer, one that spills past it, one that shrinks the vector, and the vector's own
overflow storage as the source. A moved-from vector has size zero, and self-assignment leaves the
content unchanged.

## test_task_types.cpp

The file covers the operation hash of `OperationID` and the parameter slots of
`Internal::Graph::TaskMetadata`. The hash checks are `static_assert`s, so a wrong value stops the test
program from compiling; they cover two names the repository uses, one name with a byte above 127,
and the unsigned-byte rule the hash applies.

### `Small_TaskTypes`

One test stores a value in the first and the last parameter slot and reads each back as its type.

## test_late_bindings.cpp

The file tests LateBindings: an enabled binding that uses the baked width and base 0.

| Test | What it proves |
| --- | --- |
| `DefaultsUseTheBakedWidth` | Checks the defaults of the bindings. |

## test_node.cpp

The file tests Node: successors inline and spilled, readiness on completion, placement and offload
state.

| Test | What it proves |
| --- | --- |
| `ASuccessorIsReadyAfterItsLastPredecessor` | A successor becomes ready when its last predecessor completes. |
| `SuccessorsPastTheInlineCapacityAreKept` | Successors past the inline capacity are kept and each visited once. |
| `ArmingAddsTheIntraAndBoundaryCounts` | Checks arm_in_degree(): the intra and the boundary counts add up. |
| `PlacementAndOffloadStateStartEmpty` | Placement is unstated until place_on(), and the offload state is made once. |

## test_node_profiling.cpp

The file tests NodeProfiling: three counters that start at zero and add up.

| Test | What it proves |
| --- | --- |
| `CountersStartAtZeroAndAccumulate` | The counters start at zero and accumulate. |

## test_node_widths.cpp

The file tests snapshot_widths(): the baked width, the late count, and widths too wide for 32 bits.

| Test | What it proves |
| --- | --- |
| `ABakedNodeReportsItsWidthTwice` | A node without bindings reports its baked width twice. |
| `ALateCountSetsTheLiveWidth` | A late count sets the live width and leaves the launch alone. |
| `AWidthBeyondThirtyTwoBitsIsUnknown` | A width beyond 32 bits is reported as unknown. |

## test_offload_coordinator.cpp

The file tests the OffloadCoordinator contract through a recording implementation.

| Test | What it proves |
| --- | --- |
| `EveryMemberReachesTheImplementation` | Each member of the seam reaches the implementation with its argument. |

## test_offload_sink.cpp

The file tests the OffloadSink contract through a recording implementation.

| Test | What it proves |
| --- | --- |
| `BothCallsReachTheSink` | A node given back and a node finished remotely reach the sink. |
| `FailReachesTheSinkWithTheError` | `fail()` reaches the implementation with the node and the error. |

## test_offload_state.cpp

The file tests OffloadState: not offloadable, no attempt, no lists and no payload until set.

| Test | What it proves |
| --- | --- |
| `DefaultsHoldNothingAndThePayloadIsOwned` | Counts its own destruction. */ class CountingPayload final : public OffloadPayload { public: explicit CountingPayload(int& destroyed) : destroyed_(destroyed) { } ~CountingPayload() override { ++destroyed_; } private: int& destroyed_; }; } /** @brief Tests the defaults, and that the state owns its payload. |

## test_ready_sink.cpp

The file tests ReadySink: the list a completed node writes its ready successors into.

| Test | What it proves |
| --- | --- |
| `IsAnOrderedListOfTasks` | The sink is a vector of task pointers that keeps their order. |

## test_region_link.cpp

The file tests RegionLink: clear() drops the boundary and keeps the intra count.

| Test | What it proves |
| --- | --- |
| `ClearDropsTheBoundaryOnly` | Clear() empties the boundary and leaves what the region baked. |

## test_task.cpp

The file tests Task: the defaults a subclass inherits and the two members it must write.

| Test | What it proves |
| --- | --- |
| `ASubclassInheritsTheDefaults` | Writes the two pure members and nothing else. */ class PlainTask final : public Task { public: std::optional<sushi_ptr<Task>> split() override { return std::nullopt; } bool is_splittable() const override { return false; } }; } /** @brief Tests the defaults: a kernel task, unnamed, priority 0, device work, not a graph node. |

## test_task_submission.cpp

The file covers `submit_task()` and `submit_task_discarding()` on a queue of the default device.

| Test | What it proves |
| --- | --- |
| `AHostTaskGetsItsDependenciesAndHandsItsEventOver` | A host task receives every dependency as a vector, and the event its body hands over lands in the sink. |
| `ANullSinkCollectsNothing` | A null sink opens no scope, so the enclosing scope still collects. |
| `AKernelTaskReturnsAnEvent` | A kernel task runs its command group once, returns an event and hands nothing over. |
| `TheDiscardingPathRunsEitherKind` | The discarding path runs a host task and a kernel task once each. |

## test_task_work.cpp

The file tests TaskWork and HostWork: the two callables a node runs.

| Test | What it proves |
| --- | --- |
| `HostWorkRunsOnAQueue` | Host work runs on a queue and returns its event. |
| `TaskWorkRunsInsideASubmit` | A command group runs inside a submit. |

## test_dispatch_events.cpp

DispatchEventScope, which collects the events a task body hands over.

| Test | What it proves |
| --- | --- |
| `Small_DispatchEventScope.CollectsOnlyWhileTheScopeIsOpen` | An event handed over inside a scope is collected, and one outside it is not. |
| `Small_DispatchEventScope.NestedScopesRestoreTheOuterOne` | An inner scope collects its own events and the outer one is restored after it. |
| `Small_DispatchEventScope.AScopeBelongsToItsThread` | A scope belongs to its thread: another thread's events do not reach it. |
| `Small_DispatchEventScope.HandingOverWithNoScopeDoesNothing` | Handing over with no scope open does nothing and does not throw. |
