# observability tests

The tests of the `observability` module, mirroring `modules/platform/observability/`.
`tests/platform/observability/CMakeLists.txt` builds them into `sr_observability_tests`. `sr test`
runs them with the rest; `sr run sr_observability_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_profiling_ledger.cpp

The file covers the move operations of `ProfilingLedger`. A moved-to ledger and a moved-from ledger are
compared with a fresh ledger reserved with the same multiplicities and the same device count.

### `run_dispatches`

`run_dispatches` records one dispatch for each attempt of each node through `record()`, with the node
index and the invocation number. An attempt that finds no slot is counted and skipped. It returns the
records that `collect` appends.

### `expect_same_run`

`expect_same_run` asserts that two runs hold the same number of records, and that the node index
and the invocation of each record match in order.

### `pair_device_zero` and `pairings_of`

`pair_device_zero` claims device 0's pairing, with an event reading of 900 ns beside a steady reading of
5000 ns. `pairings_of` returns what `collect_pairings` reports.

### `MovedFromLedgerClaimsNothingBeforeReserve`

The source reserves rows for two nodes and is move-constructed into a target. The moved-from ledger returns
`npos` from `claim_slot(0)`, and the target returns slot 0.

### `MoveConstructedSourceReservesLikeFresh`

The move leaves device 0's pairing in the target, and the moved-from ledger reports none. The source is then
reserved with rows for three nodes. Its records for the same dispatches match those of a fresh ledger, as
`expect_same_run` checks.

### `MoveAssignedSourceReservesLikeFresh`

The target starts with one row for one node and then takes the source by move assignment. The target holds
the pairing, and the source reports none and returns `npos` from `claim_slot(0)`. The source is then reserved
with rows for three nodes, and its records match those of a fresh ledger.

## test_event_timestamps.cpp

The file tests read_event_timestamps() on an event of a profiling queue and on a default event.

| Test | What it proves |
| --- | --- |
| `AProfiledEventReadsOrderedTimes` | A profiled event reads start before end, and submit no later than start. |
| `AnUnprofiledEventReadsNothing` | An event with no profiling reads nothing and leaves the output alone. |

## test_transfer_counters.cpp

The file tests TransferCounters' starting values, per-counter adds and concurrent totals.

## test_event_timestamps.cpp: merging the events of a body

| Test | What it proves |
| --- | --- |
| `Small_EventTimestamps.MergesEventsIntoOneSpan` | Two events fold into the earliest submit and start and the latest end. |
| `Small_EventTimestamps.TheMergeDoesNotDependOnTheOrder` | The merge covers both events whichever one is merged into the other. |
| `Small_EventTimestamps.TheSubmitIsTheBackendsOnlyWhenEveryEventHadOne` | The submit reading counts as the backend's own only when every event had one. |
| `Small_EventTimestamps.MergingAnEventIntoItselfChangesNothing` | Merging one event into itself changes nothing. |
| `Small_EventTimestamps.AnUnreadableEventFailsTheDispatch` | An event that cannot be read fails the dispatch whether it is returned or handed over. |

## test_profiling_ledger.cpp: the record writer, the pairings and the drops

| Test | What it proves |
| --- | --- |
| `Small_ProfilingLedger.RecordFillsEveryField` | Record() fills every field of the record it stores. |
| `Small_ProfilingLedger.ARecordWithNoDeviceNamesTheHost` | A host record keeps the host device and unknown widths by default. |
| `Small_ProfilingLedger.KeepsOnePairingPerDevice` | Each device claims its own pairing and only a device that read its event reports one. |
| `Small_ProfilingLedger.AReserveClearsThePairings` | The next reserve() clears the claims, so a run starts with no pairing. |
| `Small_ProfilingLedger.CountsADispatchThatFoundItsRowFull` | A dispatch that finds its row full is counted, not stored. |
| `Small_ProfilingLedger.CountsARecordForANodePastTheRows` | A record for a node past the ledger is counted under its own cause. |
| `Small_ProfilingLedger.OverflowBehavesTheSameInDebugAndRelease` | An overflow never aborts, so debug and release builds behave the same. |
| `Small_ProfilingLedger.NoteDropCountsEachCauseApart` | The other drop causes are counted by note_drop(), each under its own count. |
| `Small_ProfilingLedger.ACleanRunLosesNothing` | A ledger that recorded nothing reports no drop. |
| `Small_ProfilingLedger.AMovedLedgerCarriesItsPairings` | A moved ledger keeps its pairings and a moved-from one starts with none. |

## test_event_timestamps.cpp: merging the events of a body

| Test | What it proves |
| --- | --- |
| `Small_EventTimestamps.MergesEventsIntoOneSpan` | Two events fold into the earliest submit and start and the latest end. |
| `Small_EventTimestamps.TheMergeDoesNotDependOnTheOrder` | The merge covers both events whichever one is merged into the other. |
| `Small_EventTimestamps.TheSubmitIsTheBackendsOnlyWhenEveryEventHadOne` | The submit reading counts as the backend's own only when every event had one. |
| `Small_EventTimestamps.MergingAnEventIntoItselfChangesNothing` | Merging one event into itself changes nothing. |
| `Small_EventTimestamps.AnUnreadableEventFailsTheDispatch` | An event that cannot be read fails the dispatch whether it is returned or handed over. |

## test_profiling_ledger.cpp: the record writer, the pairings and the drops

| Test | What it proves |
| --- | --- |
| `Small_ProfilingLedger.RecordFillsEveryField` | Record() fills every field of the record it stores. |
| `Small_ProfilingLedger.ARecordWithNoDeviceNamesTheHost` | A host record keeps the host device and unknown widths by default. |
| `Small_ProfilingLedger.KeepsOnePairingPerDevice` | Each device claims its own pairing and only a device that read its event reports one. |
| `Small_ProfilingLedger.AReserveClearsThePairings` | The next reserve() clears the claims, so a run starts with no pairing. |
| `Small_ProfilingLedger.CountsADispatchThatFoundItsRowFull` | A dispatch that finds its row full is counted, not stored. |
| `Small_ProfilingLedger.CountsARecordForANodePastTheRows` | A record for a node past the ledger is counted under its own cause. |
| `Small_ProfilingLedger.OverflowBehavesTheSameInDebugAndRelease` | An overflow never aborts, so debug and release builds behave the same. |
| `Small_ProfilingLedger.NoteDropCountsEachCauseApart` | The other drop causes are counted by note_drop(), each under its own count. |
| `Small_ProfilingLedger.ACleanRunLosesNothing` | A ledger that recorded nothing reports no drop. |
| `Small_ProfilingLedger.AMovedLedgerCarriesItsPairings` | A moved ledger keeps its pairings and a moved-from one starts with none. |

## test_event_timestamps.cpp: merging the events of a body

| Test | What it proves |
| --- | --- |
| `Small_EventTimestamps.MergesEventsIntoOneSpan` | Two events fold into the earliest submit and start and the latest end. |
| `Small_EventTimestamps.TheMergeDoesNotDependOnTheOrder` | The merge covers both events whichever one is merged into the other. |
| `Small_EventTimestamps.TheSubmitIsTheBackendsOnlyWhenEveryEventHadOne` | The submit reading counts as the backend's own only when every event had one. |
| `Small_EventTimestamps.MergingAnEventIntoItselfChangesNothing` | Merging one event into itself changes nothing. |
| `Small_EventTimestamps.AnUnreadableEventFailsTheDispatch` | An event that cannot be read fails the dispatch whether it is returned or handed over. |

## test_profiling_ledger.cpp: the record writer, the pairings and the drops

| Test | What it proves |
| --- | --- |
| `Small_ProfilingLedger.RecordFillsEveryField` | Record() fills every field of the record it stores. |
| `Small_ProfilingLedger.ARecordWithNoDeviceNamesTheHost` | A host record keeps the host device and unknown widths by default. |
| `Small_ProfilingLedger.KeepsOnePairingPerDevice` | Each device claims its own pairing and only a device that read its event reports one. |
| `Small_ProfilingLedger.AReserveClearsThePairings` | The next reserve() clears the claims, so a run starts with no pairing. |
| `Small_ProfilingLedger.CountsADispatchThatFoundItsRowFull` | A dispatch that finds its row full is counted, not stored. |
| `Small_ProfilingLedger.CountsARecordForANodePastTheRows` | A record for a node past the ledger is counted under its own cause. |
| `Small_ProfilingLedger.OverflowBehavesTheSameInDebugAndRelease` | An overflow never aborts, so debug and release builds behave the same. |
| `Small_ProfilingLedger.NoteDropCountsEachCauseApart` | The other drop causes are counted by note_drop(), each under its own count. |
| `Small_ProfilingLedger.ACleanRunLosesNothing` | A ledger that recorded nothing reports no drop. |
| `Small_ProfilingLedger.AMovedLedgerCarriesItsPairings` | A moved ledger keeps its pairings and a moved-from one starts with none. |

## test_event_timestamps.cpp: merging the events of a body

| Test | What it proves |
| --- | --- |
| `Small_EventTimestamps.MergesEventsIntoOneSpan` | Two events fold into the earliest submit and start and the latest end. |
| `Small_EventTimestamps.TheMergeDoesNotDependOnTheOrder` | The merge covers both events whichever one is merged into the other. |
| `Small_EventTimestamps.TheSubmitIsTheBackendsOnlyWhenEveryEventHadOne` | The submit reading counts as the backend's own only when every event had one. |
| `Small_EventTimestamps.MergingAnEventIntoItselfChangesNothing` | Merging one event into itself changes nothing. |
| `Small_EventTimestamps.AnUnreadableEventFailsTheDispatch` | An event that cannot be read fails the dispatch whether it is returned or handed over. |

## test_profiling_ledger.cpp: the record writer, the pairings and the drops

| Test | What it proves |
| --- | --- |
| `Small_ProfilingLedger.RecordFillsEveryField` | Record() fills every field of the record it stores. |
| `Small_ProfilingLedger.ARecordWithNoDeviceNamesTheHost` | A host record keeps the host device and unknown widths by default. |
| `Small_ProfilingLedger.KeepsOnePairingPerDevice` | Each device claims its own pairing and only a device that read its event reports one. |
| `Small_ProfilingLedger.AReserveClearsThePairings` | The next reserve() clears the claims, so a run starts with no pairing. |
| `Small_ProfilingLedger.CountsADispatchThatFoundItsRowFull` | A dispatch that finds its row full is counted, not stored. |
| `Small_ProfilingLedger.CountsARecordForANodePastTheRows` | A record for a node past the ledger is counted under its own cause. |
| `Small_ProfilingLedger.OverflowBehavesTheSameInDebugAndRelease` | An overflow never aborts, so debug and release builds behave the same. |
| `Small_ProfilingLedger.NoteDropCountsEachCauseApart` | The other drop causes are counted by note_drop(), each under its own count. |
| `Small_ProfilingLedger.ACleanRunLosesNothing` | A ledger that recorded nothing reports no drop. |
| `Small_ProfilingLedger.AMovedLedgerCarriesItsPairings` | A moved ledger keeps its pairings and a moved-from one starts with none. |
