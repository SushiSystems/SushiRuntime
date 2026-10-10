# memory tests

The tests of the `memory` module, built into one program, `sr_memory_tests`, which
`modules/platform/memory/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_memory_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_epoch_reclamation.cpp

The file covers epoch-based reclamation. `EpochManager` is a process-wide singleton, so the tests
assert observable contracts: retired objects are eventually deleted once, `EpochGuard` enter and
exit are balanced, and a thread that retires and then exits hands its nodes to the global orphan
list.

### `Tracked`

`Tracked` is a heap payload that increments a shared counter when it is destroyed, so a test can
observe reclamation without reaching into `EpochManager`.

The counter is a `shared_ptr` and not a raw pointer to an atomic on the stack. Reclamation is
deferred, so a tail of retired objects can be freed during a later test. A raw stack pointer would
then be written after the frame of the test is gone, which is a stack-use-after-return. The
`shared_ptr` keeps the counter alive as long as any `Tracked` refers to it.

### `retire_and_drain`

`retire_and_drain` retires a number of objects and then runs extra guarded passes to advance the
epoch. The tests use it to drain pending nodes.

### `RetiredObjectsEventuallyReclaimed`

Most objects should be gone. The survivors are those retired in the final epoch, which has not
advanced yet. The bound is generous so that the test does not flake.

### `ThreadExitHandsOffOrphans`

When the thread exits, its destructor donates the remaining retire list to the global orphan list.

The main thread then drains. Each guarded retire triggers `check_and_reclaim`, which pulls in the
orphaned nodes and frees those past the safe epoch.

## test_usm_registry.cpp

### `fake`

Registry keys are opaque and nothing is dereferenced, so distinct fake addresses are enough, and the
test needs no `Runtime`.

### `EXTENT`

The fake addresses sit 64 bytes apart, so a 64-byte extent covers exactly one of them and never
reaches its neighbour.

### `TakeReportsTheOwnerAndForgetsThePointer`

The free path erases the entry and reads the owner in one step. A second free of the same pointer
must therefore be distinguishable from the first and must not report a stale device.

### `RecordingAPointerTwiceKeepsTheLatestOwner`

Recording a pointer twice is reachable only if an allocator handed back an address the registry had
not been told was freed. The last writer wins, which matches the map assignment the runtime has
always done. The test asserts it so that a future change of container does not quietly make the
first writer win.

### `ConcurrentRecordAndTakeLoseNothing`

Buffers are constructed and destroyed from whichever thread owns them, so several threads write the
registry at once. Every pointer the test records is taken exactly once, by exactly one thread.

### `ContainingAllocationResolvesAnInteriorPointer`

The graph builder needs this case: a caller offsets into a plan array and hands over an address that
the table, which is keyed by base address, cannot find.

### `AnEntryWithoutALengthResolvesOnlyAtItsBase`

An adopted address whose importer knew no size must not read as zero-length and must not swallow
offsets it cannot vouch for.
