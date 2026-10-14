# engine_handles tests

The tests of the `engine_handles` module, mirroring `modules/surface/engine_handles/`.
`tests/surface/engine_handles/CMakeLists.txt` builds them into `sr_engine_handles_tests`. `sr test`
runs them with the rest; `sr run sr_engine_handles_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_late_controls_access.cpp

The file tests engine_controls() and surface_controls(): one object under two names, sharing its
owner.

| Test | What it proves |
| --- | --- |
| `BothSharesNameOneBinding` | The surface share and the engine share name one binding and one owner. |
| `NullStaysNull` | Null converts to null. |

## test_plan_access.cpp

The file tests engine_plan() and surface_plan(): a round trip that names one object, and null for
null.

| Test | What it proves |
| --- | --- |
| `ARoundTripNamesTheSamePlan` | The surface name and the engine plan are one object both ways. |
| `NullStaysNull` | Null converts to null in both directions. |
