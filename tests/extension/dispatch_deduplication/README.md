# dispatch_deduplication tests

The tests of the `dispatch_deduplication` module, mirroring
`modules/extension/dispatch_deduplication/`. `tests/extension/dispatch_deduplication/CMakeLists.txt`
builds them into `sr_dispatch_deduplication_test_objects`, linked into `sr_extension_tests`. `sr test` runs them with the rest; `sr run
sr_extension_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_dispatch_deduplicator.cpp

| Test | What it proves |
| --- | --- |
| `DuplicateDispatchRunsOnce` | An id is accepted once and refused while it is remembered. A deduplicator of capacity 2 forgets its oldest id when a third arrives. |
| `ClearForgetsEveryId` | `clear()`, which a new master session calls, empties the set, so an id seen before is accepted again. |
