# Regression tests

The tests in `tests/regression` are sentinels for defects that were found and fixed. They name
headers of more than one module by path, which is why they are here and not in a module. A module's
own tests are documented in `modules/<tier>/<module>/tests/README.md`. This document records what
each test proves and why it is built the way it is. A section is named after a test file, and a
subsection after the test it describes. The source comments state one invariant per line and cite
this file.

## test_regression.cpp

Each defect gets one fast, deterministic test, so that a later change cannot bring the defect back
unnoticed.

### `Epoch_OrphanedNodesReclaimed`

The counter is a `shared_ptr` and not a raw pointer to an atomic on the stack. Epoch-based
reclamation is deferred, so a tail of retired objects outlives the test and is freed during a later
one. A raw stack pointer would then be written after the frame is gone, which is a
stack-use-after-return. The `shared_ptr` keeps the counter alive as long as any `Tracked` refers to
it.

### `AThreadOutlivingTheRuntimeTearsDownCleanly`

A thread that allocated through a `Runtime` and is still alive when that `Runtime` is destroyed
unwinds after it. A long-running simulation process hits this shape when a worker outlives the
context it allocated from.

The risk is thread-local state that outlives what it points into. Epoch reclamation keeps a slot per
thread, and a thread that reaches its exit-time destructors after the `Runtime` is gone must not
touch freed memory.

The thread allocates and frees, so that its exit-time destructors have touched the per-thread
bookkeeping of the runtime. It runs those destructors when the test joins it. Reaching the statement
after the join without a fault is the assertion: under AddressSanitizer it is a use-after-free
check, and without AddressSanitizer the defect still crashes often enough to fail the test. The
`SUCCEED()` after the join records that the test got there on purpose.
