# work_stealing tests

The tests of the `work_stealing` module, mirroring `modules/engine/work_stealing/`.
`tests/engine/work_stealing/CMakeLists.txt` builds them into `sr_work_stealing_tests`. `sr test`
runs them with the rest; `sr run sr_work_stealing_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_work_stealing_deque.cpp

The tests push tags, never real tasks: the deque moves pointers and never reads through them.

| Test | What it proves |
| --- | --- |
| `PushBeyondCapacityParksOverflow` | A push past the capacity parks the task in the overflow list; nothing is dropped. |
| `PopDrainsOverflowExactlyOnce` | The owner's pops return every task of the ring and the overflow once each. |
| `NoLossUnderConcurrentSteal` | While the owner pushes and pops, thieves steal; every task comes out exactly once. |
