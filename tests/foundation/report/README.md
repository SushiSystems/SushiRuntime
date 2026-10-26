# report tests

The tests of the `report` module, mirroring `modules/foundation/report/`.
`tests/foundation/report/CMakeLists.txt` builds them into `sr_report_tests`. `sr test` runs them
with the rest; `sr run sr_report_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_run_report.cpp

The file covers the layout of `NodeEventRecord` and checks what `RunReport::reset()` keeps and
zeroes. The engine backend copies `NodeEventRecord` field for field with no translation, so the
tests pin its size, its field order and its triviality.

### `LayoutIsFortyEightBytesAndTriviallyCopyable`

The record is 48 bytes and trivially copyable, with `device_index` and `submit_thread` after
`end_ns`. A change of padding, order or an added field breaks the contract the engine backend
copies against.

### `ResetKeepsWarningsCapacity`

`reset()` must not free the storage of the report's vectors, so a report handed to `run(RunReport&)`
every tick stops allocating after the first run.

### `ResetZeroesTheOutcomeAndTheCounts`

`reset()` returns success, cancellation, the task count, the layer and the distributed count to
their constructed values.

### `WidthsDefaultToUnknown`

The widths default to `K_WIDTH_UNKNOWN` and not to zero. Zero is a real dispatch width on an empty
pass and would read as a measured value. The device defaults to `K_HOST_DEVICE_INDEX` and the
submitting thread to 0.

The file covers the report types under their names in `SushiRuntime`.

### `ADefaultReportIsEmpty`

A default `RunReport` is successful, names the scheduler layer and holds no NUMA row. A default
`NodeEventRecord` has a launched width of `K_WIDTH_UNKNOWN`.

## test_dispatch_width.cpp

The file tests K_WIDTH_UNKNOWN: the sentinel no real launch width reaches.

| Test | What it proves |
| --- | --- |
| `UnknownIsTheLargestWidth` | The unknown width is the largest 32-bit value. |

## test_node_table.cpp

NodeTable, the names, operations and costs of a plan's nodes by registration index.

| Test | What it proves |
| --- | --- |
| `Small_NodeTable.FindsARowByRegistrationIndex` | A row is found by its registration index whatever order the rows came in. |
| `Small_NodeTable.AnIndexPastTheTableHasNoRow` | An index no row holds, before, between and past the rows, has none. |
| `Small_NodeTable.KeepsAnAbsentCostApartFromAStatedZero` | A cost a caller never stated stays apart from a stated cost of zeros. |
| `Small_NodeTable.ARowKeepsItsOperationHostFlagAndMultiplicity` | A row keeps the operation, the host flag and the multiplicity it was given. |
| `Small_NodeTable.CarriesTheLabelAndTheOriginOfItsRows` | A table keeps the label of its graph and the origin of each row. |
| `Small_NodeTable.CarriesTheGraphId` | A table keeps the id of the graph it was built for, and 0 when none was given. |

## test_node_event_record.cpp

NodeEventRecord keeps the size and the field order SushiEngine mirrors.

| Test | What it proves |
| --- | --- |
| `Small_NodeEventRecord.HasTheSizeAndTheOrderTheEngineMirrors` | The size and the order of the record, and what a default record states. |
| `Small_NodeEventRecord.ADeviceClockKeepsItsDeviceAndPairing` | A device clock holds the device and its pairing as they were given. |

## test_clock_pairing.cpp

ClockPairing and DeviceClock, which place a device's event clock on the steady clock.

| Test | What it proves |
| --- | --- |
| `Small_ClockPairing.ADefaultPairingIsTheIdentity` | A default pairing is the identity pairing: both readings 0 and no uncertainty. |
| `Small_ClockPairing.APairingKeepsItsThreeReadings` | A pairing keeps its three readings as given. |
| `Small_ClockPairing.ADeviceClockKeepsItsDeviceAndPairing` | A device clock holds the device and its pairing as they were given. |

## test_drop_counts.cpp

DropCounts, which says how many dispatches a run left out of its timing.

| Test | What it proves |
| --- | --- |
| `Small_DropCounts.ADefaultCountLostNothing` | A default count holds nothing and names no node. |
| `Small_DropCounts.CountsPerCauseAndSaysWhetherAnyWasLost` | A count is kept per cause and that the total and any() read them all. |
| `Small_DropCounts.EveryCauseHasItsOwnSlot` | Every cause has its own slot, so no two causes share a count. |

## test_run_observer.cpp

RunObservation and RunObserverHandle, the way a run's timing leaves the runtime.

| Test | What it proves |
| --- | --- |
| `Small_RunObservation.StartsEmpty` | An observation starts empty and on the scheduler layer. |
| `Small_RunObservation.AnObserverReadsTheObservationItIsGiven` | An observer is called through the interface with the observation it is given. |
| `Small_RunObserverHandle.ADefaultHandleStandsForNothing` | A default handle stands for nothing and a reset of it is harmless. |
| `Small_RunObserverHandle.ResetRemovesOnceAndOnlyOnce` | Reset() removes the observer once and a second call removes nothing. |
| `Small_RunObserverHandle.DestructionRemovesTheObserver` | Destroying a handle removes its observer. |
| `Small_RunObserverHandle.AMovedHandleCarriesTheRemoval` | A moved handle carries the removal and the moved-from one removes nothing. |
| `Small_RunObserverHandle.AssigningOverALiveHandleRemovesItsObserver` | Assigning over a live handle removes the observer it stood for. |
