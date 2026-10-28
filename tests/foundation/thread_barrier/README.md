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
| `NoThreadPassesBeforeTheLastArrives` | Holds the threads that meet at the barrier. */ constexpr std::size_t K_THREADS = 4; /** @brief Tests that every thread arrived before any passed, over two rounds. |
| `ABarrierOfOneNeverWaits` | A barrier of one never waits. |
