# observability tests

The tests of the `observability` module, mirroring `modules/platform/observability/`.
`tests/platform/observability/CMakeLists.txt` builds them into `sr_observability_test_objects`, linked into `sr_platform_tests`. `sr test`
runs them with the rest; `sr run sr_platform_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.
Sizes are described in [the tests README](../../README.md#sizes).

## test_event_timestamps.cpp

The file tests the event timestamps: the read of a profiled event, the merge of the spans of several
events, and the read of an event that cannot be read.

| Test | What it proves |
| --- | --- |
| `Medium_EventTimestamps.AProfiledEventReadsOrderedTimes` | A profiled event reads a start no later than its end. It runs on a profiling queue. |
| `Small_EventTimestamps.MergesEventsIntoOneSpan` | Two events fold into the earliest start and the latest end. |
| `Small_EventTimestamps.TheMergeDoesNotDependOnTheOrder` | The merge covers both events whichever one is merged into the other. |
| `Small_EventTimestamps.MergingAnEventIntoItselfChangesNothing` | Merging one event into itself changes nothing. |

### `FailsTheReadAndLeavesTheOutputAlone`

Rows: a default event read on its own, and a dispatch read with no events handed over and with two
handed over. A default event comes from no queue built with profiling, so each read fails and the
output is as it was. The test needs no device.

## test_offset_bounds.cpp

The bounds one dispatch places on its device's clock offset, how two bounds narrow, and the pairing
`pairing_from()` makes of them. Every expected value is worked out in a comment beside it.

| Test | What it proves |
| --- | --- |
| `Small_OffsetBounds.ADispatchBoundsTheOffsetBySubmitMinusStartAndReapMinusEnd` | A dispatch bounds the offset from submit minus start up to reap minus end. |
| `Small_OffsetBounds.ADeviceClockAheadOfTheSteadyClockGivesNegativeBounds` | A device clock ahead of the steady clock gives negative bounds. |
| `Small_OffsetBounds.NarrowingKeepsTheLargerLowAndTheSmallerHigh` | Narrowing keeps the larger low and the smaller high, whichever comes first. |
| `Small_OffsetBounds.TwoDispatchesEachTightOnOneSideGiveBothSides` | Two dispatches that each bound one side tightly give the range both allow. |
| `Small_OffsetBounds.ThePairingIsTheMidpointAndHalfTheWidth` | The pairing converts with the midpoint of the bounds and states half their width. |
| `Small_OffsetBounds.AnOddWidthRoundsTheUncertaintyUp` | An odd width rounds the uncertainty up, so the far bound is still inside it. |
| `Small_OffsetBounds.NegativeBoundsPlaceTheEventBelowItself` | Negative bounds place the reference event reading below it on the steady clock. |
| `Small_OffsetBounds.CrossedBoundsKeepTheMidpointAndHalfTheGap` | Crossed bounds keep the midpoint of the two and half the gap between them. |
| `Small_OffsetBounds.TheDriftWidensTheUncertaintyAndKeepsTheMidpoint` | The drift the offset may make is added to the uncertainty and leaves the midpoint where it is. |
| `Small_OffsetBounds.TheDriftOverASpanIsAHundredPpmRoundedUp` | drift_over() allows 100 ppm of a span, rounded up to the next nanosecond. |

## test_steady_time.cpp

| Test | What it proves |
| --- | --- |
| `Small_SteadyTime.SteadyNsCountsNanosecondsSinceTheEpoch` | steady_ns() counts the nanoseconds of a time point since the steady clock's epoch. |

## test_profiling_ledger.cpp

The file covers the record writer, the device pairings, the drop counts and the move operations of
`ProfilingLedger`.

### `run_dispatches`

`run_dispatches` records one dispatch for each attempt of each node through `record()`, with the node
index and the invocation number. An attempt that finds no slot is counted and skipped. It returns the
node index and the invocation of each record that `collect` appends, in order, so two runs are
compared with one equality.

### `pair_device_zero` and `pairings_of`

`pair_device_zero` narrows device 0 by one dispatch submitted at steady 5000, run from event 900 to 950
and reaped at steady 5100, so its pairing places event 900 at steady 5025. `pairings_of` returns what
`collect_pairings` reports.

### The fake device

`seen_on_device` reads a `TrueDispatch`, whose times are all on the steady clock, the way the host sees
it: the submit and reap as they are, the start and end on a `FakeEventClock` 5 ms ahead.
`K_STEADY_DEVICE` keeps the steady clock's pace; `K_DRIFTING_DEVICE` gains 33.5 ppm, the rate measured
on an RTX 3080 Ti. `K_COLD` starts
200 us after its submit began, as a first submit after the device idled does; `K_WARM` and
`K_REAPED_QUICKLY` start and are reaped within tens of nanoseconds. `expect_converted_within_uncertainty`
converts a dispatch's start and end through a pairing, as `ClockPairing` documents, and expects each
within the pairing's uncertainty of its true steady time, and the span inside the host's submit-to-reap
window give or take that uncertainty. Every expected uncertainty adds 100 ppm of the device's span,
rounded up.

### The move tests

Each runs twice, once for a move construction and once for a move assignment onto a ledger that holds
other rows. The moved-to ledger keeps the pairing and the rows of the source, the moved-from ledger
holds no pairing and takes no record or bounds before a reserve, and after a reserve it records the same run as a
fresh ledger.

### `ARowKeepsItsCapacityAndCountsTheRest`

Two rows: one dispatch past a row of one slot, and three past a row of two. A dispatch past the row is
counted as `FullRow` and not stored, the first drop names its node, and nothing aborts.

### The record, pairing and drop tests

| Test | What it proves |
| --- | --- |
| `RecordFillsEveryField` | record() fills every field of the record it stores. |
| `ARecordOnTheHostDeviceKeepsTheHostIndexAndUnknownWidths` | A record made on the host device with unknown widths keeps them. |
| `OnlyANarrowedDeviceHasAPairing` | A device no dispatch narrowed has no pairing, and one that was narrowed has its own. |
| `ADevicePastTheLedgerIsIgnored` | A dispatch of a device past the ledger narrows nothing. |
| `EachDeviceKeepsItsOwnBounds` | Each device keeps the bounds of its own dispatches. |
| `SeveralDispatchesGiveTheTightestPairingAroundTheTrueOffset` | A cold and two warm dispatches give the tightest pairing, and every one of their spans converts within its uncertainty of the truth. |
| `AColdFirstDispatchAloneStaysInsideItsStatedUncertainty` | A cold first dispatch alone gives a wide pairing, and its span still converts within it. |
| `ThePairingDoesNotDependOnTheReapOrder` | The pairing is the same whichever order the dispatches are reaped in. |
| `AReserveClearsThePairings` | The next reserve() widens the bounds again, so a run keeps none of the last run's. |
| `AClockDriftingAcrossALongRunStaysInsideTheStatedUncertainty` | A clock drifting 33.5 ppm over a run of one minute crosses the dispatches' bounds, and the drift allowance keeps every span inside the uncertainty. |
| `Small_ProfilingLedgerDrift.ALaterRunOfADriftingClockStaysInsideItsWindow` | Rows 1, 5 and 10 minutes after a first run on a clock drifting 33.5 ppm: every span of the later run stays inside its uncertainty and its submit-to-reap window. |
| `TheNextReserveSizesARowToTheDemand` | The run after an overflow sizes the row to the demand and loses nothing. |
| `CountsARecordForANodePastTheRows` | A record for a node past the ledger is counted under its own cause. |
| `NoteDropCountsEachCauseApart` | The other drop causes are counted by note_drop(), each under its own count, and the total is their sum. |
| `TheFirstDropNamesItsNode` | The first drop names its node. |
| `ACleanRunLosesNothing` | A ledger that recorded nothing reports no drop. |

## test_transfer_counters.cpp

The file tests TransferCounters' per-counter adds, accumulation and concurrent totals.

| Test | What it proves |
| --- | --- |
| `Small_TransferCountersAdd.RaisesOnlyItsCounter` | One add raises only its own counter; one row per direction. |
| `Small_TransferCounters.RepeatedAddsAccumulate` | Repeated adds accumulate in each counter. |
| `Medium_TransferCounters.ConcurrentAddsGiveExactTotals` | No add is lost when eight threads add to both counters at once. |
