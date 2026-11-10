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
| `Medium_EventTimestamps.AProfiledEventReadsOrderedTimes` | A profiled event reads start before end, and submit no later than start. It runs on a profiling queue. |
| `Small_EventTimestamps.MergesEventsIntoOneSpan` | Two events fold into the earliest submit and start and the latest end. |
| `Small_EventTimestamps.TheMergeDoesNotDependOnTheOrder` | The merge covers both events whichever one is merged into the other. |
| `Small_EventTimestamps.TheSubmitIsTheBackendsOnlyWhenEveryEventHadOne` | The submit reading counts as the backend's own only when every event had one. |
| `Small_EventTimestamps.MergingAnEventIntoItselfChangesNothing` | Merging one event into itself changes nothing. |

### `FailsTheReadAndLeavesTheOutputAlone`

Rows: a default event read on its own, and a dispatch read with no events handed over and with two
handed over. A default event comes from no queue built with profiling, so each read fails and the
output is as it was. The test needs no device.

## test_pairing_claim.cpp

How one dispatch claims its device's clock pairing, keeps it or gives it back.

| Test | What it proves |
| --- | --- |
| `Small_PairingClaim.ASubmittedClaimKeepsThePairing` | A claim that submitted keeps the device's pairing, and collect returns the event reading stored. |
| `Small_PairingClaim.TheWindowOfAClaimStartsAtASteadyReading` | The pairing of a claim carries a steady reading above zero. |
| `Small_PairingClaim.AClaimThatSubmittedNothingIsGivenUp` | A claim that submitted nothing is given up, so the next dispatch can claim its candidate. |
| `Small_PairingClaim.ALosingClaimChangesNothing` | A claim with no ledger, or on a device whose candidates are all held, leaves the ledger as it was. |
| `Small_PairingClaim.TheFirstDispatchesOfADeviceEachKeepACandidate` | The first dispatches of a device each keep a candidate, so a cold first submit is not the only one. |
| `Small_PairingClaim.SteadyNsCountsNanosecondsSinceTheEpoch` | steady_ns() counts the nanoseconds of a time point since the steady clock's epoch. |

## test_tighter_pairing.cpp

`tighter_pairing()`, the pure choice between two pairings of one device: the shorter window wins, the kept one on a tie.

| Test | What it proves |
| --- | --- |
| `Small_TighterPairing.AShorterWindowReplacesALongerOne` | A warm pairing replaces a cold one with a longer window. |
| `Small_TighterPairing.ALongerWindowKeepsTheKeptPairing` | A cold candidate arriving after a warm one does not replace it. |
| `Small_TighterPairing.EqualWindowsKeepTheKeptPairing` | Two equal windows keep the pairing already kept. |
| `Small_TighterPairing.APairingIsChosenWholeNotFieldByField` | A window of zero beats every other, and the pairing travels whole. |
| `Small_TighterPairing.TheTightestOfSeveralIsChosenInAnyOrder` | The tightest of a run's candidates is chosen whatever order they come in. |
| `Small_TighterPairing.TheChosenWindowIsNoLongerThanAnyCandidates` | The chosen window is never longer than any candidate's, so a cold first submit does not inflate it. |

## test_profiling_ledger.cpp

The file covers the record writer, the device pairings, the drop counts and the move operations of
`ProfilingLedger`.

### `run_dispatches`

`run_dispatches` records one dispatch for each attempt of each node through `record()`, with the node
index and the invocation number. An attempt that finds no slot is counted and skipped. It returns the
node index and the invocation of each record that `collect` appends, in order, so two runs are
compared with one equality.

### `pair_device_zero` and `pairings_of`

`pair_device_zero` claims device 0's pairing, with an event reading of 900 ns beside a steady reading of
5000 ns. `pairings_of` returns what `collect_pairings` reports.

### The move tests

Each runs twice, once for a move construction and once for a move assignment onto a ledger that holds
other rows. The moved-to ledger keeps the pairing and the rows of the source, the moved-from ledger
holds no pairing and claims nothing before a reserve, and after a reserve it records the same run as a
fresh ledger.

### `ARowKeepsItsCapacityAndCountsTheRest`

Two rows: one dispatch past a row of one slot, and three past a row of two. A dispatch past the row is
counted as `FullRow` and not stored, the first drop names its node, and nothing aborts.

### The record, pairing and drop tests

| Test | What it proves |
| --- | --- |
| `RecordFillsEveryField` | record() fills every field of the record it stores. |
| `ARecordOnTheHostDeviceKeepsTheHostIndexAndUnknownWidths` | A record made on the host device with unknown widths keeps them. |
| `ADeviceKeepsAFewPairingCandidatesPerRun` | A device keeps K_PAIRING_CANDIDATES candidates per run and refuses the next claim. |
| `EachDeviceClaimsItsOwnPairing` | Three devices are claimed one after another. |
| `ADevicePastTheLedgerClaimsNothing` | A device past the ledger claims nothing and owns nothing. |
| `AReleasedCandidateIsClaimedAgain` | A released candidate is free again, so a full device claims once one is given back. |
| `TheSameTaskInTwoStepsHoldsTwoCandidates` | One task claiming in two steps holds two candidates, and each step's event reaches its own. |
| `TheFirstEventReadingOfACandidateStays` | The first event reading of a candidate stays when a second dispatch of its key is reaped. |
| `ADeviceThatNeverReadItsEventHasNoPairing` | A device whose owner never read its event reports no pairing. |
| `TheStoredPairingsAreReportedPerDevice` | The pairings of the devices that read their events are reported with their values. |
| `TheTightestWindowIsTheDevicesPairing` | A cold first submit with a long window loses to a later, shorter one. |
| `ACandidateWithNoEventIsNotChosen` | A candidate with no event reading never wins, however short its window. |
| `AReserveClearsThePairings` | The next reserve() clears the claims, so a run starts with no pairing. |
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
