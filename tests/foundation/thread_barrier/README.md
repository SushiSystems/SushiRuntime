# thread_barrier tests

The tests of the `thread_barrier` module, mirroring `modules/foundation/thread_barrier/`.
`tests/foundation/thread_barrier/CMakeLists.txt` builds them into `sr_thread_barrier_test_objects`, linked into `sr_foundation_tests`. `sr
test` runs them with the rest; `sr run sr_foundation_tests -- --gtest_filter='<pattern>'` runs
one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_barrier.cpp

The file tests Barrier: no thread passes before the last arrives, and it serves again.

| Test | What it proves |
| --- | --- |
| `Medium_Barrier.NoThreadPassesBeforeTheLastArrives` | Four threads meet twice at the barrier; none passes before every thread of the round has arrived. A helper starts the threads and counts the early passes. |
| `Small_Barrier.ABarrierOfOneNeverWaits` | A barrier of one returns at once on the calling thread, both times it is used; a hang ends at the size timeout. |
