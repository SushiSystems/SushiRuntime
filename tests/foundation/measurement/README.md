# measurement tests

The tests of the `measurement` module, mirroring `modules/foundation/measurement/`.
`tests/foundation/measurement/CMakeLists.txt` builds them into `sr_measurement_tests`. `sr test`
runs them with the rest; `sr run sr_measurement_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_completion_signal.cpp

The file tests CompletionSignal: a wait times out, a raise wakes it, and reset clears it.

| Test | What it proves |
| --- | --- |
| `AWaitWithNoRaiseTimesOut` | A wait with nothing raised returns false. |
| `ARaiseWakesTheWaiterAndResetClearsIt` | A raise on another thread wakes the waiter, and reset() clears it. |

## test_measurement.cpp

The file tests summarize(), measure() and the SR_MEASUREMENT line.

| Test | What it proves |
| --- | --- |
| `SummaryTakesTheMiddleAndTheExtremes` | Holds the tolerance of every comparison of a summary; the figures are exact. */ constexpr double K_TOLERANCE = 0.0; /** @brief Tests the median of an odd and an even count, and the extremes. |
| `NoSampleGivesAnEmptySummary` | No sample gives an empty summary. |
| `MeasureCallsTheRunEachTime` | Measure() calls the run the number of times asked. |
| `TheLineCarriesEveryKey` | The line starts with the prefix and carries every key. |
