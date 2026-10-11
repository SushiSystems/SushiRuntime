# observability tests

The tests of the `observability` module, built into one program, `sr_observability_tests`, which
`modules/platform/observability/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_observability_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_node_event_record.cpp

The file covers the layout of `Internal::Observability::NodeEventRecord` and checks that `RunReport::reset()` keeps the
capacity of `node_events` while it zeroes the clock pairing. The engine copies `NodeEventRecord`
field for field with no translation, so the tests pin its size and triviality.

### `LayoutIsFortyBytesAndTriviallyCopyable`

The record is 40 bytes and trivially copyable, which is the shape the engine backend copies with no
translation. A change of padding or an added field breaks that contract.

### `ResetKeepsNodeEventsCapacity`

`reset()` must not free the storage of `node_events`. Every other vector of `RunReport` already
keeps its capacity across a reset.

### `ResetZeroesClockPairing`

`reset()` must zero the clock pairing, so that a stale pairing from a previous run never survives
into a report that reads as freshly reset.

### `WidthsDefaultToUnknown`

The widths default to `K_WIDTH_UNKNOWN` and not to zero. Zero is a real dispatch width on an empty
pass and would read as a measured value.

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
