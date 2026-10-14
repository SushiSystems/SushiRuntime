# observability tests

The tests of the `observability` module, mirroring `modules/platform/observability/`.
`tests/platform/observability/CMakeLists.txt` builds them into `sr_observability_tests`. `sr test`
runs them with the rest; `sr run sr_observability_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_profiling_ledger.cpp

The file covers the move operations of `ProfilingLedger`. A moved-to ledger and a moved-from ledger are
compared with a fresh ledger reserved with the same multiplicities.

### `run_dispatches`

`run_dispatches` claims one slot for each attempt of each node and writes a record that holds the node
index and the invocation number. An attempt that claims no slot is skipped. It returns the `RunReport`
that `collect` fills.

### `expect_same_run`

`expect_same_run` asserts that two reports hold the same number of node events, and that the node index
and the invocation of each event match in order.

### `MovedFromLedgerClaimsNothingBeforeReserve`

The source reserves rows for two nodes and is move-constructed into a target. The moved-from ledger returns
`npos` from `claim_slot(0)`, and the target returns slot 0.

### `MoveConstructedSourceReservesLikeFresh`

The move leaves the clock start of 900 ns in the target's pairing `event_ns`, and the moved-from ledger
reports `event_ns` of 0. The source is then reserved with rows for three nodes. Its report for the same
dispatches matches the report of a fresh ledger, as `expect_same_run` checks.

### `MoveAssignedSourceReservesLikeFresh`

The target starts with one row for one node and then takes the source by move assignment. The target holds
the clock start of 900 ns, and the source reports `event_ns` of 0 and returns `npos` from `claim_slot(0)`.
The source is then reserved with rows for three nodes, and its report matches that of a fresh ledger.

## test_event_timestamps.cpp

The file tests read_event_timestamps() on an event of a profiling queue and on a default event.

| Test | What it proves |
| --- | --- |
| `AProfiledEventReadsOrderedTimes` | A profiled event reads start before end, and submit no later than start. |
| `AnUnprofiledEventReadsNothing` | An event with no profiling reads nothing and leaves the output alone. |

## test_transfer_counters.cpp

The file tests TransferCounters' starting values, per-counter adds and concurrent totals.

