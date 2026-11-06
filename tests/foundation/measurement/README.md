# measurement tests

The tests of the `measurement` module, mirroring `modules/foundation/measurement/`.
`tests/foundation/measurement/CMakeLists.txt` builds them into `sr_measurement_test_objects`, linked into `sr_foundation_tests`. `sr test`
runs them with the rest; `sr run sr_foundation_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_completion_signal.cpp

The file tests CompletionSignal: a wait times out, a raise wakes the waiter, and reset clears it. Every
test is `Medium_`: a wait reads the real clock, and the wake test hands the raise to another thread.

| Test | What it proves |
| --- | --- |
| `AWaitWithNoRaiseTimesOut` | A wait with nothing raised returns false. |
| `ARaiseWakesTheWaiter` | A raise on another thread wakes the waiter. |
| `ResetClearsARaisedSignal` | A raised signal stays raised, and reset() clears it. |

## test_measurement.cpp

The file tests summarize(), measure() and the SR_MEASUREMENT line. The figures are exact, so the
comparisons are equalities.

| Test | What it proves |
| --- | --- |
| `TheMedianOfAnOddCountIsTheMiddleSample` | The median of 5, 1 and 3 is 3. |
| `TheMedianOfAnEvenCountIsTheMeanOfTheMiddlePair` | The median of 4, 1, 2 and 3 is 2.5. |
| `SummaryTakesTheExtremesAndCountsTheRuns` | The minimum, the maximum and the run count of a summary. |
| `NoSampleGivesAnEmptySummary` | No sample gives an all-zero summary. |
| `MeasureCallsTheRunEachTime` | measure() calls the run the number of times asked. |
| `TheLineCarriesEveryKey` | The line opens with the prefix and carries every key with its value. |
| `TheLineNamesTheBuild` | The line names the build as release or debug. |
