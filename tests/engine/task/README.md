# task tests

The tests of the `task` module, mirroring `modules/engine/task/`. `tests/engine/task/CMakeLists.txt`
builds them into `sr_task_test_objects`, linked into `sr_engine_tests`. `sr test` runs them with the rest;
`sr run sr_engine_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.
Test prefixes and the device fixture follow `tests/engine/scheduler/README.md`.

## compile_late_bindings.cpp, compile_node_profiling.cpp, compile_ready_sink.cpp and compile_task_work.cpp

Each file includes one header alone, so the header carries every include it needs. The headers hold a
plain struct, an alias or a function alias, which have no behaviour of their own to test. The promise of
`LateBindings` that a default count uses the baked width is held by `test_node_widths.cpp`.

## test_dispatch_events.cpp

DispatchEventScope, which collects the events a task body hands over.

| Test | What it proves |
| --- | --- |
| `Small_DispatchEventScope.CollectsOnlyWhileTheScopeIsOpen` | An event handed over inside a scope is collected, and one outside it, with no scope open, is not. |
| `Small_DispatchEventScope.NestedScopesRestoreTheOuterOne` | An inner scope collects its own events and the outer one is restored after it. |
| `Medium_DispatchEventScope.AScopeBelongsToItsThread` | A scope belongs to its thread: another thread's events do not reach it. A second thread is the smallest way to see it. |

## test_in_memory_offload_coordinator.cpp

The file tests the InMemoryOffloadCoordinator adapter against the OffloadCoordinator contract.

| Test | What it proves |
| --- | --- |
| `RefusesAndRecordsANodeByDefault` | Without acceptance set, try_offload() refuses and the node is recorded as offered only. |
| `AcceptsAndListsANodeOnceAcceptingIsSet` | With acceptance set, try_offload() accepts and the node is listed as accepted. |
| `KeepsTheSinkItIsBoundTo` | bind() keeps the address of the sink it was given. |
| `CountsEachPrepareCall` | Each prepare() call is counted. |
| `KeepsTheRulesOfTheLatestPrepareCall` | The rules of the latest prepare() call replace those held before. |
| `RecordsEachLocalCompletionInCallOrder` | Each node the pool finished itself is recorded in call order. |
| `RecordsThatStopRan` | stop() is recorded once the scheduler calls it. |

## test_in_memory_offload_sink.cpp

The file tests the InMemoryOffloadSink adapter against the OffloadSink contract.

| Test | What it proves |
| --- | --- |
| `RecordsEachResubmittedNodeInCallOrder` | Each node given back by resubmit() is recorded in call order. |
| `RecordsEachRemotelyCompletedNodeInCallOrder` | Each node finished by a worker and given to complete_remote() is recorded in call order. |
| `RecordsEachFailedNodeWithItsError` | fail() records the node with the error code and message it was given. |

## test_placement_memory.cpp, test_usm_placement_memory.cpp and compile_usm_placement_memory.cpp

`PlacementMemory` is a port with two implementations. The checks every implementation must pass are
`expect_*` helpers in `tests/support/placement_memory_contract.hpp`. `test_placement_memory.cpp` runs
them on `InMemoryPlacementMemory` as `Small_PlacementMemoryContract`. `test_usm_placement_memory.cpp` runs them on `USMPlacementMemory` over a `USMAllocator` of two
entries that share the program queue, as `Medium_USMPlacementMemoryContract`. `compile_usm_placement_memory.cpp`
includes the adapter header alone.

| Test | What it proves |
| --- | --- |
| `AnUnrecordedAddressHasNoDevice` | An address nobody recorded has no recorded and no owning device. |
| `ARecordedAddressNamesItsDevice` | A recorded (small) or adopted (medium) address names its device. |
| `TheDeviceCountIsTheNumberOfDevicesItWasBuiltWith` | The device count is the number of devices or allocator entries. |
| `TwoDevicesInOneContextShareIt` | Two devices in one context share it. |

## test_in_memory_placement_memory.cpp

The file tests what `InMemoryPlacementMemory` adds to the contract: devices numbered into two contexts do not
share one.

| Test | What it proves |
| --- | --- |
| `TwoDevicesInTwoContextsDoNotShareOne` | Two devices in two contexts do not share one. |

## test_node.cpp

The file tests Node: successors inline and spilled, readiness on completion, placement and offload
state.

| Test | What it proves |
| --- | --- |
| `ASuccessorIsReadyAfterItsLastPredecessor` | A successor becomes ready when its last predecessor completes. |
| `SuccessorsPastTheInlineCapacityAreKept` | Successors past the inline capacity are kept and each visited once; a builder adds them and the visited set is compared with the added set. |
| `ArmingAddsTheIntraAndBoundaryCounts` | `arm_in_degree()` sets the in-degree to the intra count plus the boundary count. |
| `PlacementIsUnstatedUntilPlaceOn` | Placement is unstated until `place_on()`, and then holds the index last stated. |
| `TheOffloadStateIsMadeOnceOnDemand` | The offload state is absent until `offload()` asks for it, and is made once. |
| `SetLiveIoStoresTheListsOfTheNextDispatch` | `set_live_io()` stores the read and write lists in the offload state. |

## test_node_placement.cpp

The file covers the placement a node states and the rule `Internal::Graph::placement_of()` applies to it:
a stated placement wins over the owner of the first write region, else of the first read region,
when the two share a SYCL context, and is refused when they do not; a node that states nothing runs
on the owner; a node with no owner runs on its stated placement, else device 0. `modules/engine/scheduler/README.md` states the rule under "Queue selection", and the
scheduler's `tests/engine/scheduler/test_task_scheduler.cpp` checks the queue a dispatch receives.

### `Medium_NodePlacementStateDeathTest`

`place_on()` stores an index and `stated_placement()` returns it, which `test_node.cpp` checks. An index
the 16-bit field cannot hold fails `SR_VERIFY`, which aborts the process with the index in the message;
a death test checks it, and the earlier placement stays. A death test starts a child process.

### `Small_NodePlacementRule`

The fixture holds an `InMemoryPlacementMemory` of two devices in one SYCL context, so device indices 0
and 1 both exist. No test makes a queue or allocates: `record()` names a host address as owned by one of the
two, and an address with no record stands for a region no device owns.

| Test | What it proves |
| --- | --- |
| `ANodeWithNoRegionRunsOnTheDefaultWhenNothingIsStated` | A node with no region and no placement runs on device 0. |
| `ANodeWithNoRegionRunsOnItsStatedDevice` | A node with no region runs on the device it states. |
| `ANodeWithOnlyAReadRegionRunsOnItsOwner` | A node whose only region is a read runs on the owner of that region. |
| `TheFirstWriteRegionOutranksTheFirstReadRegion` | The owner of the first write outranks the owner of the first read. |
| `AnOwnedRegionPlacesANodeThatStatesNothing` | An owned region places a node that states nothing. |
| `AStatedDeviceInTheOwnersContextOutranksTheOwner` | A stated device in the owner's context outranks the owner. |
| `AnUnownedRegionWithNothingStatedRunsOnDeviceZero` | An unowned region with nothing stated runs on device 0. |
| `AnUnownedRegionLeavesTheStatedPlacementInForce` | A stated device stands for an unowned region. |

### `Small_NodePlacementContexts`

The fixture holds an `InMemoryPlacementMemory` of two devices in two SYCL contexts, so the refusal needs no
second device.

| Test | What it proves |
| --- | --- |
| `AStatedDeviceInAnotherContextIsRefused` | A stated device in the other context is an `InvalidGraph` error naming the owner. |
| `StatingTheOwnerItselfIsAccepted` | Stating the owner itself is accepted. |
| `AStatedIndexPastTheDevicesIsLeftToTheDispatch` | A stated index past the memory's devices is returned as stated, for `place_for_dispatch()` to refuse with the count. |

## test_node_widths.cpp

The file tests snapshot_widths(): the baked width, the late count, and widths too wide for 32 bits.

| Test | What it proves |
| --- | --- |
| `ABakedNodeReportsItsWidthAsLaunched` | A node with a baked width and no bindings reports it as launched. |
| `ABakedNodeReportsItsWidthAsLive` | The same node reports it as live. |
| `ANodeWithNoBakedWidthReportsUnknown` | A node with no baked width reports an unknown width. |
| `DefaultBindingsReportTheBakedWidthAsLive` | Bindings that hold their defaults report the baked width as live. |
| `ALateCountSetsTheLiveWidth` | A late count sets the live width. |
| `ALateCountLeavesTheLaunchedWidthAlone` | A late count leaves the launched width alone. |
| `AWidthBeyondThirtyTwoBitsIsUnknown` | A width beyond 32 bits is reported as unknown. |

## test_offload_coordinator.cpp

The file tests the OffloadCoordinator contract through `InMemoryOffloadCoordinator`, the adapter beside
the port, called through a reference to the port. `test_in_memory_offload_coordinator.cpp` checks the
adapter itself in the same way, so the two files overlap.

| Test | What it proves |
| --- | --- |
| `BindReachesTheImplementationWithTheSink` | `bind()` reaches the implementation with the sink. |
| `PrepareReachesTheImplementationWithTheRules` | `prepare()` reaches it with the rules it was given. |
| `TryOffloadReachesTheImplementationWithTheNode` | `try_offload()` reaches it with the node. |
| `OnLocalCompletionReachesTheImplementationWithTheNode` | `on_local_completion()` reaches it with the node. |
| `StopReachesTheImplementation` | `stop()` reaches it. |

## test_offload_sink.cpp

The file tests the OffloadSink contract through `InMemoryOffloadSink`, called through a reference to the
port. It overlaps `test_in_memory_offload_sink.cpp` for the same reason.

| Test | What it proves |
| --- | --- |
| `ResubmitReachesTheSink` | A node given back reaches the sink. |
| `CompleteRemoteReachesTheSink` | A node finished remotely reaches the sink. |
| `FailReachesTheSinkWithTheError` | `fail()` reaches the sink with the node and the error. |

## test_offload_state.cpp

The file tests OffloadState: the state owns its payload.

| Test | What it proves |
| --- | --- |
| `ThePayloadIsOwned` | The state destroys its payload with itself, not before. The payload type counts its destruction, since the task module ships no payload to watch. |

## test_recording_task.cpp

The file tests RecordingTask, the in-memory host Task. Its host run takes a queue, so that call is
left to the medium tests that hold one.

| Test | What it proves |
| --- | --- |
| `ReportsItsNameAndPriority` | The task reports the name and priority it was built with. |
| `IsAHostTaskThatNeverSplits` | The task is a host task that submits no device work and never splits. |
| `CountsEachNativeRun` | Each native run is counted. |
| `CountsEachCompletion` | Each completion is counted. |

## test_region_link.cpp

The file tests RegionLink: clear() drops the boundary and keeps the intra count.

| Test | What it proves |
| --- | --- |
| `ClearDropsTheBoundaryOnly` | Clear() empties the boundary and leaves what the region baked. |

## test_small_event_vector.cpp

The file covers `Internal::Graph::SmallEventVector`, the event dependencies a node holds: copy, move,
assignment from another vector or from a `std::vector`, and `clear()`. The events are
default-constructed. Each one has its own state, so `==` tells two of them apart and a copy of
one compares equal to it; the tests rely on that.

| Test | What it proves |
| --- | --- |
| `ACopyHoldsTheSameEventsInlineAndOverflow` | A copy holds the same events, inline and past `K_STATIC_CAP`. |
| `AMoveHoldsTheEventsAndEmptiesTheSource` | A move holds the events and empties the source, its overflow storage included. |
| `Small_SmallEventVectorAssignment.AssignmentReplacesEveryPriorEvent` | Rows of (events held, events assigned): assigning a `SmallEventVector` and assigning a `std::vector` each replace every prior event, for lists that fit the inline buffer, spill past it, shrink or grow the vector. |
| `AMoveAssignmentReplacesEveryPriorEventAndEmptiesTheSource` | Move assignment replaces the prior events and leaves the source empty. |
| `AssignmentPastTheInlineCapacityKeepsEveryElementInOrder` | Twenty events keep 8 inline and 12 in the overflow, in order. |
| `IterationVisitsEveryEventInOrder` | Iterating the same vector visits each event in order. |
| `ClearEmptiesTheVector` | `clear()` empties the vector. |
| `AClearedVectorAcceptsNewEvents` | A cleared vector accepts new events. |
| `SelfCopyAssignmentKeepsEveryEvent` | A copy assignment to itself keeps every event. |
| `SelfMoveAssignmentKeepsEveryEvent` | A move assignment to itself keeps every event. |
| `AssignmentFromItsOwnOverflowKeepsThatList` | Assigning the vector's own `dynamic_data` keeps that list, an aliasing case. |

## test_task.cpp

The file tests Task through the concrete `Node`.

| Test | What it proves |
| --- | --- |
| `ATaskWithNoSuccessorReadiesNothing` | A task with no successor readies nothing when it completes. |

## test_task_submission.cpp

The file covers `submit_task()` and `submit_task_discarding()` on the program's queue. The tasks are
`Node`s: a host node whose body submits one kernel and hands its event over, and a kernel node.

| Test | What it proves |
| --- | --- |
| `Medium_TaskSubmission.AHostTaskGetsItsDependencies` | A host task receives every dependency as a vector. |
| `Medium_TaskSubmission.AHostTaskHandsItsEventOver` | The event a host body hands over lands in the sink. |
| `Medium_TaskSubmission.ANullSinkCollectsNothing` | A null sink opens no scope, so the enclosing scope still collects. |
| `Medium_TaskSubmission.AKernelTaskReturnsAnEvent` | A kernel task runs its command group once and returns an event. |
| `Medium_TaskSubmission.AKernelTaskHandsNoEventOver` | A kernel task hands nothing over. |
| `Medium_TaskSubmission.TheDiscardingPathRunsAHostTask` | The discarding path runs a host task once, behind its dependencies. |
| `Medium_TaskSubmission.TheDiscardingPathRunsAKernelTask` | The discarding path runs a kernel task once. |

## test_task_types.cpp

The file covers the operation hash of `OperationID` and the parameter slots of
`Internal::Graph::TaskMetadata`. The hash checks are `static_assert`s, so a wrong value stops the test
program from compiling; they cover two names the repository uses, one name with a byte above 127,
and the unsigned-byte rule the hash applies.

### `Small_TaskTypes`

One test stores a value in the first and the last parameter slot and reads each back as its type.
