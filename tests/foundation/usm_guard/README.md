# usm_guard tests

The tests of the `usm_guard` module, mirroring `modules/foundation/usm_guard/`.
`tests/foundation/usm_guard/CMakeLists.txt` builds them into `sr_usm_guard_tests`. `sr test` runs
them with the rest; `sr run sr_usm_guard_tests -- --gtest_filter='<pattern>'` runs one program
alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_usm_shared.cpp

The file tests USMShared: an allocation the host reads and writes, freed once, moved without a copy.

| Test | What it proves |
| --- | --- |
| `TheHostWritesAndReadsEveryElement` | The host writes and reads every element. |
| `AMoveHandsTheAllocationOver` | A move hands the allocation over and empties the source. |
