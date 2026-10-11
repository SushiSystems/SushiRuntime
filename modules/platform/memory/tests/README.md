# memory tests

The tests of the `memory` module, built into one program, `sr_memory_tests`, which
`modules/platform/memory/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_memory_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_epoch_reclamation.cpp

The file covers epoch-based reclamation. `EpochManager` is a process-wide singleton, so the tests
assert observable contracts: retired objects are eventually deleted once, `EpochGuard` enter and
exit are balanced, and the objects a thread retires before it exits are reclaimed later.

### `Tracked`

`Tracked` is a heap payload that increments a shared counter when it is destroyed, so a test can
observe reclamation without reaching into `EpochManager`.

The counter is a `shared_ptr` and not a raw pointer to an atomic on the stack. Reclamation is
deferred, so a tail of retired objects can be freed during a later test. A raw stack pointer would
then be written after the frame of the test is gone, which is a stack-use-after-return. The
`shared_ptr` keeps the counter alive as long as any `Tracked` refers to it.

### `retire_and_drain`

`retire_and_drain` retires the given number of objects under guards, then retires 512 more to advance
the epoch. It returns the number of objects destroyed by then. The tests use it to drain pending nodes.

### `RetiredObjectsEventuallyReclaimed`

The test retires 1024 objects through `retire_and_drain`, which retires 512 more, and asserts that more
than 1024 of the 1536 objects are destroyed.

### `ThreadExitHandsOffOrphans`

The producer thread retires 300 objects under guards and exits. The main thread then retires 1024 more
objects under guards. A scan runs once per `RETIRE_THRESHOLD` (128) further retires, so those retires
run scans, and the test asserts that at least one of the producer's objects is destroyed by the end.

### `K_EPOCH_SLOTS`

`EpochManager` holds `MAX_WORKER_THREADS + 32` thread slots, 288 of them. The constant
repeats that sum because the manager keeps its own private.

### `ThreadPark`

`ThreadPark` counts the threads that have reported and blocks each one until the test releases
them. It replaces a sleep: the test waits for a count, not for time.

### `AThreadPastTheSlotLimitIsRefusedWithTheLimit`

A thread takes a slot at its first `EpochGuard` and returns it when the thread exits, not when the
guard is destroyed. The test starts one thread more than there are slots and parks every one of
them, so at least one finds no slot, whether or not the main thread already holds one. That thread
gets `SushiException` with `Errc::unsupported` and a message that carries the slot count.

After the parked threads exit, one more thread takes a guard without a throw, which shows the
slots came back.

### `ABlockedReclaimScansOncePerThreshold`

A reader thread enters a guard and parks. The main thread then retires 10 times `RETIRE_THRESHOLD`
objects and counts the scans recorded in its thread-local state. The test asserts that no object is
destroyed, that at least one scan ran, and that no more than 10 scans ran.

### `RetiringParent`

`RetiringParent` is a payload whose destructor bumps its own counter and retires a `Tracked` child
that holds the second counter.

### `ADestructorThatRetiresDeletesParentAndChildOnce`

The test retires 4 times `RETIRE_THRESHOLD` parents under guards. Each parent's destructor retires one
child. The test then retires 32 times `RETIRE_THRESHOLD` objects under guards and asserts that the
parent counter and the child counter each equal the number of parents.

## test_resource_region.cpp

The file covers the overlap and containment queries of `ResourceRegion`. Two integers stand in for two
allocations, `first_allocation` and `second_allocation`. Only their addresses are used.

### `ADefaultRegionOverlapsNothing`

A default region is not valid and overlaps no region, itself included. Two regions with a null base,
of 8 bytes at offsets 0 and 4, do not overlap.

### `ADefaultRegionContainsNothingAndIsContainedByNothing`

A default region contains neither another default region nor a region of an allocation. The whole
allocation does not contain a default region.

### `RegionsOfOneAllocationOverlapWhenTheirIntervalsIntersect`

The whole allocation overlaps every region of it. `[0, 8)` overlaps `[4, 12)`, and `[4, 12)` overlaps
`[8, 16)` and the tail from offset 12 to the end. The intervals are half-open, so `[0, 8)` overlaps
neither `[8, 16)` nor the tail.

### `RegionsOfDifferentAllocationsNeverOverlap`

Regions of two different allocations neither overlap nor contain each other.

### `ARegionContainsWhatLiesEntirelyInside`

The whole allocation contains itself and `[0, 8)`. `[0, 8)` contains itself and `[2, 6)`. `[0, 8)`
contains neither the whole allocation nor `[4, 12)`, and `[2, 6)` does not contain `[0, 8)`.

## test_usm_allocator.cpp

The file covers `USMAllocator`. The sections below cover its release of an address that it holds no
record of, and the removal of its release observer. The first two tests skip when no SYCL device
supports the USM kind they need.

### `ASecondReleaseLogsOneErrorWithTheAddressAndFreesNothing`

The test allocates on device 0 and releases the block. That first release logs no ERROR line and lowers
`live_count` by one. The second release leaves `live_count` unchanged, makes `device_of` return no
value, and logs exactly one ERROR line that contains the address and the words "no record".

### `ReleaseOfForeignUSMLeavesItAllocated`

The test allocates shared USM with `sycl::malloc_shared` outside the allocator and releases it through
the allocator. The release logs exactly one ERROR line and leaves `live_count` unchanged. The block keeps
its bytes, and the test frees it with `sycl::free`.

### `SetReleaseObserverWaitsForAnObserverCallInFlight`

The test installs an observer whose `on_release` blocks, then releases an adopted address on a second
thread, so one call is in flight. A third thread removes the observer. The test asserts that the removal
has not returned after 200 ms, opens the observer, and asserts that the removal returns.

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
