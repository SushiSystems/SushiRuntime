# observability tests

The tests of the `observability` module, built into one program, `sr_observability_tests`, which
`modules/platform/observability/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_observability_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_node_event_record.cpp

The file covers the layout of `Observability::NodeEventRecord` and checks that `RunReport::reset()` keeps the
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

The widths default to `WIDTH_UNKNOWN` and not to zero. Zero is a real dispatch width on an empty
pass and would read as a measured value.
