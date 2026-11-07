# usm_guard tests

The tests of the `usm_guard` module, mirroring `modules/foundation/usm_guard/`.
`tests/foundation/usm_guard/CMakeLists.txt` builds them into `sr_usm_guard_test_objects`, linked into `sr_foundation_tests`. `sr test` runs
them with the rest; `sr run sr_foundation_tests -- --gtest_filter='<pattern>'` runs one program
alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_usm_shared.cpp

The file tests USMShared: an allocation the host reads and writes, freed once, moved without a copy.

| Test | What it proves |
| --- | --- |
| `Medium_USMShared.TheHostWritesAndReadsEveryElement` | The host writes and reads every element; a helper writes the multiples and another reads them back. |
| `Small_USMShared.AGuardFreesItsAllocationOnceThroughItsSource` | A guard over an in-memory source allocates once and frees once when it ends. |
| `Small_USMShared.AMoveConstructionHandsTheAllocationOver` | A move construction hands the allocation over, empties the source, and the allocation is freed once. |
| `Small_USMShared.AMoveAssignmentHandsTheAllocationOver` | A move assignment hands the allocation over, with its count, and frees the one it replaced. |

The small tests run on an `InMemorySharedAllocationSource`. The medium queue comes from the fixture base of `tests/support/runtime_environment.hpp`; the fixture skips the test on a machine with no SYCL device.

## The source tests

`compile_shared_allocation_source.cpp` compiles the port header alone.
`Small_InMemorySharedAllocationSource` (`test_in_memory_shared_allocation_source.cpp`) runs
`SharedAllocationSourceContract` on host memory, and `Medium_QueueSharedAllocationSource`
(`test_queue_shared_allocation_source.cpp`) on the program's queue, the boundary host memory
stands in for.
