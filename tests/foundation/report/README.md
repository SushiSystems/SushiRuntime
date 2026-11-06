# report tests

The tests of the `report` module, mirroring `modules/foundation/report/`.
`tests/foundation/report/CMakeLists.txt` builds them into `sr_report_test_objects`, linked into `sr_foundation_tests`. `sr test` runs them
with the rest; `sr run sr_foundation_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_run_report.cpp

The file checks what `RunReport::reset()` keeps and zeroes. The suite is `Small_RunReport`.

### `ResetKeepsWarningsCapacity`

`reset()` must not free the storage of the report's vectors, so a report handed to `run(RunReport&)`
every tick stops allocating after the first run.

### `ResetZeroesTheOutcomeAndTheCounts`

`reset()` returns success, cancellation, the task count, the layer and the distributed count to
their constructed values.

## compile_dispatch_width.cpp

Includes `dispatch_width.hpp` alone, so the build fails if the header misses an include. It holds no test.

## test_node_table.cpp

NodeTable, the names, operations and costs of a plan's nodes by registration index. The graph label,
the graph id, and the origin, operation, host flag and multiplicity of a row are read by the tests of
the engine and the profiling modules, so none is tested here.

| Test | What it proves |
| --- | --- |
| `Small_NodeTable.FindsARowByRegistrationIndex` | A row is found by its registration index whatever order the rows came in. |
| `Small_NodeTable.AnIndexPastTheTableHasNoRow` | An index no row holds, before, between and past the rows, has none. |
| `Small_NodeTable.KeepsAnAbsentCostApartFromAStatedZero` | A cost a caller never stated stays apart from a stated cost of zeros. |

## test_node_event_record.cpp

What a default `NodeEventRecord` states. The engine backend copies the record field for field with no
translation, so file-scope `static_assert`s pin its size, its field order and its triviality; they are
checked by the build and no test repeats them.

| Test | What it proves |
| --- | --- |
| `Small_NodeEventRecord.ADefaultRecordStatesUnknownWidthsAndTheHost` | The widths default to `K_WIDTH_UNKNOWN` and not to zero, the device to `K_HOST_DEVICE_INDEX` and the submitting thread and the times to 0. |

## compile_clock_pairing.cpp

Includes `clock_pairing.hpp` alone, so the build fails if the header misses an include. It holds no test.

## test_drop_counts.cpp

DropCounts, which says how many dispatches a run left out of its timing.

| Test | What it proves |
| --- | --- |
| `Small_DropCounts.ADefaultCountLostNothing` | A default count holds nothing, per cause and in total, and names no node. |
| `Small_DropCounts.AnyTellsWhetherADispatchWasLost` | `any()` is true once one cause holds a count. |
| `Small_DropCounts.EveryCauseHasItsOwnSlot` | Every cause has its own slot, so no two causes share a count. |

## test_run_observer.cpp

RunObservation and RunObserverHandle, the way a run's timing leaves the runtime. The observer the
observation test uses is the `RecordingObserver` of the module.

| Test | What it proves |
| --- | --- |
| `Small_RunObservation.AnObserverReadsTheObservationItIsGiven` | An observer is called through the interface with the observation it is given. |
| `Small_RunObserverHandle.ADefaultHandleStandsForNothing` | A default handle stands for nothing and a reset of it is harmless. |
| `Small_RunObserverHandle.ResetRemovesOnceAndOnlyOnce` | Reset() removes the observer once and a second call removes nothing. |
| `Small_RunObserverHandle.DestructionRemovesTheObserver` | Destroying a handle removes its observer. |
| `Small_RunObserverHandle.AMoveConstructedHandleCarriesTheRemoval` | A move-constructed handle carries the removal and the moved-from one removes nothing. |
| `Small_RunObserverHandle.AMoveAssignedHandleCarriesTheRemoval` | A move-assigned handle carries the removal. |
| `Small_RunObserverHandle.AssigningOverALiveHandleRemovesItsObserver` | Assigning over a live handle removes the observer it stood for. |

## test_recording_observer.cpp

RecordingObserver, the observer the runtime's tests keep observations with. The thread test is
`Medium_`: it starts four threads.

| Test | What it proves |
| --- | --- |
| `Small_RecordingObserver.StartsEmpty` | An observer that saw nothing holds nothing. |
| `Small_RecordingObserver.KeepsEveryObservationInOrder` | It keeps a copy of every observation in the order they came. |
| `Small_RecordingObserver.ClearForgetsEveryObservation` | Clear() forgets every observation. |
| `Medium_RecordingObserver.KeepsObservationsFromSeveralThreads` | Observations arriving from several threads are all kept. |
