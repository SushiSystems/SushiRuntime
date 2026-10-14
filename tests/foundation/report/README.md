# report tests

The tests of the `report` module, mirroring `modules/foundation/report/`.
`tests/foundation/report/CMakeLists.txt` builds them into `sr_report_tests`. `sr test` runs them
with the rest; `sr run sr_report_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_run_report.cpp

The file covers the layout of `NodeEventRecord` and checks that `RunReport::reset()` keeps the
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

The file covers the report types under their names in `SushiRuntime`.

### `ADefaultReportIsEmpty`

A default `RunReport` is successful, names the scheduler layer and holds no node event. A default
`NodeEventRecord` has a launched width of `K_WIDTH_UNKNOWN`.

### `ANodeTimingNamesItsOperation`

`NodeTiming::operation` is an `OperationID`: unnamed by default, and equal to the id it was
given.

## test_dispatch_width.cpp

The file tests K_WIDTH_UNKNOWN: the sentinel no real launch width reaches.

| Test | What it proves |
| --- | --- |
| `UnknownIsTheLargestWidth` | The unknown width is the largest 32-bit value. |
