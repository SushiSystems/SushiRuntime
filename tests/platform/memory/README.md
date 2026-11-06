# memory tests

The tests of the `memory` module, mirroring `modules/platform/memory/`.
`tests/platform/memory/CMakeLists.txt` builds them into `sr_memory_test_objects`, linked into `sr_platform_tests` and
`sr_memory_benchmarks`, built under `SR_BUILD_BENCHMARKS`. `sr test` runs them with the rest;
`sr run sr_platform_tests -- --gtest_filter='<pattern>'` runs one program alone.

A benchmark is a `Benchmark_` suite; this module has none now.

A section is named after a test file, and a subsection after the test, helper or topic it describes.
Sizes and the shared fixture base are described in [the tests README](../../README.md#sizes). The
allocator tests derive from that fixture base and skip where SYCL reports no device.

## compile_memory_log_category.cpp and compile_usm_request.cpp

Each file includes one header alone. See [the scheduler tests](../../engine/scheduler/README.md)
for why such a file stands in for a test.

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

### The retire helpers

`retire_guarded`, `retire_unguarded`, `retire_from_threads`, `run_short_threads` and `retire_parents`
hold the loops that build the load, so a test body holds none. `retire_and_drain` retires the given
number of objects under guards, then retires 512 more to advance the epoch, and returns the number
destroyed by then.

### `NestedGuardsBalanceTheDepthOfTheThread`

Two nested guards raise the nesting depth of the thread by two, the inner one's exit leaves it one
above where it began, and the outer one's exit restores it. The test reads the depth from the
module's own thread-local state.

### `RetiredObjectsEventuallyReclaimed`

The test retires 1024 objects through `retire_and_drain`, which retires 512 more, and asserts that more
than 1024 of the 1536 objects are destroyed. It reaches the process-wide manager, so it stays medium until
the manager can be built for one test.

### `ThreadExitHandsOffOrphans`

The producer thread retires 300 objects under guards and exits. The main thread then retires 1024 more
objects under guards. A scan runs once per `K_RETIRE_THRESHOLD` (128) further retires, so those retires
run scans, and the test asserts that at least one of the producer's objects is destroyed by the end.

### `K_EPOCH_SLOTS`

`EpochManager` holds `K_MAX_WORKER_THREADS + 32` thread slots, 288 of them. The constant
repeats that sum because the manager keeps its own private.

### `ThreadPark`

`ThreadPark` counts the threads that have reported and blocks each one until the test releases
them. It replaces a sleep: the test waits for a count, not for time.

### `AThreadPastTheSlotLimitStopsNamingTheLimit`

A thread takes a slot at its first `EpochGuard` and returns it when the thread exits, not when the
guard is destroyed. The test starts one thread more than there are slots and parks every one of
them, so at least one finds no slot, whether or not the main thread already holds one. That thread
stops the process with a message that carries the slot count, which the death test matches.

### `AnExitedThreadReturnsItsSlot`

The test runs more threads than there are slots, one after another, each taking a guard and leaving.
A slot that did not come back would stop the process on the last of them.

### `AnOpenReaderKeepsEveryRetiredObjectAlive`

A reader thread enters a guard and parks. The main thread then retires 10 times `K_RETIRE_THRESHOLD`
objects, and the test asserts that none of them is destroyed. The scan count of the thread is not read.

### `RetiringParent`

`RetiringParent` is a payload whose destructor bumps its own counter and retires a `Tracked` child
that holds the second counter.

### `ADestructorThatRetiresDeletesParentAndChildOnce`

The test retires 4 times `K_RETIRE_THRESHOLD` parents under guards. Each parent's destructor retires one
child. The test then retires 32 times `K_RETIRE_THRESHOLD` objects under guards and asserts that the
parent counter and the child counter each equal the number of parents.

### `Large_EpochStress`

Two high-churn stress tests for epoch-based reclamation, moved here from a benchmark file that
measured nothing. `HighChurnNoLeak` has eight threads retire 20 000 objects each, then drains on the
main thread; most of the objects must be reclaimed and the live count must not go negative.
`ThreadSpawnExitHandoff` runs 64 short-lived threads that retire 1000 objects each and exit, while a
drainer thread keeps advancing the epochs; the exits force the orphan handoff, and some of the
objects must be reclaimed. The helpers `retire_objects`, `retire_from_threads`,
`retire_from_short_lived_threads` and `BackgroundDrainer` build the load, so the test bodies hold no
loop. Under ASan and TSan the same run surfaces races and use-after-free. The counters are
`shared_ptr`-owned for the reason `Tracked` gives.

## test_recording_usm_release_observer.cpp

A `RecordingUSMReleaseObserver` keeps the addresses it hears. The test calls `on_release()` itself,
with no allocator and no device.

| Test | What it proves |
| --- | --- |
| `KeepsEachAddressInArrivalOrder` | The observer keeps each address it hears, in arrival order, repeats included. |

## test_usm_allocation_owner.cpp

The file tests USMAllocationOwner: an Allocation returns its USM to the allocator once.

| Test | What it proves |
| --- | --- |
| `DestroyingTheAllocationFreesTheUSM` | Destroying the allocation frees the USM through the allocator. It stays medium, since the owner holds a concrete allocator. |
| `AnOwnerWithNoAllocatorLeavesTheMemoryAlone` | An owner with no allocator releases nothing; the memory it is handed keeps its value. |

## test_usm_allocator.cpp

The file covers `USMAllocator`. The kind and alignment tests run as rows and skip when no SYCL device
supports the row's kind. The release observer is the module's `RecordingUSMReleaseObserver`, and the
ERROR and WARN lines are read through `LogCapture`.

### `AllocatesIsUsableIsCountedAndIsReleased`

One row per kind. A device writes device USM; the host writes host and shared USM.

### `ReturnsAnAddressOnTheAlignment`

One row per kind, alignment and length. A shared row skips where the toolchain's aligned shared
allocation does not honour the alignment.

### `AnAllocationResolvesToItsDeviceAtItsBase`, `AnInteriorAddressFindsItsBase`, `TheAddressPastTheEndHasNoBase` and `AReleasedAllocationHasNoBase`

An allocation answers for its device at its first address through `device_of` and `owning_device`. An
interior address has no device of its own and finds its base through `containing_base`; the address one
past the end and a released allocation find none.

### `RecordOfKeepsTheKindAnAllocationWasMadeAs` and `RecordOfAnAdoptedAddressHasNoKind`

An allocation's record holds its device and the kind requested until it is released; an adopted
address is recorded with no kind.

### `AdoptRecordsTheDeviceOfAnAddress`, `AnAdoptedAddressContainsItsBytes`, `ForgetDropsTheRecordWithoutFreeing` and `ForgetIgnoresNull`

`adopt` records a device and counts the address live, and an adopted address with a length resolves
from inside it. `forget` drops the record and does not free; null is ignored.

### `TheFirstReleaseLogsNothing` and `ASecondReleaseLogsOneErrorWithTheAddressAndFreesNothing`

The first release of a block logs no ERROR line. The second leaves `live_count` unchanged, and logs
exactly one ERROR line that contains the address and the words "no record".

### `ReleaseOfForeignUSMLeavesItAllocated`

The test allocates shared USM with `sycl::malloc_shared` outside the allocator and releases it through
the allocator. The release logs exactly one ERROR line and leaves `live_count` unchanged. The block keeps
its bytes, which one whole-buffer comparison checks, and the test frees it with `sycl::free`.

### `TheReleaseObserverHearsNothingBeforeARelease`, `ANullReleaseAndAForgetAreNotHeard`, `AnAdoptedAddressIsHeardWhenReleased` and `ARemovedObserverHearsNothingMore`

Each test states one promise of the observer: it hears nothing before a release, neither a null release
nor a `forget` reaches it, an adopted address is reported like any other, and a removed observer is
not told. That the observer hears a released address once is in `test_usm_release_observer.cpp`.

### `SetReleaseObserverWaitsForAnObserverCallInFlight`

The test installs an observer whose `on_release` blocks, then releases an adopted address on a second
thread, so one call is in flight. A third thread removes the observer. The test asserts that the removal
has not returned after 200 ms, opens the observer, and asserts that the removal returns. The wait is a
sleep, since the module takes no clock the test could advance.

### `Medium_UsmAllocatorFailure.LogsTheRefusedRequest`

The test asks device 0 for twice its global memory, with an alignment of 64, under a `LogCapture` at
`Warn`. It asserts the allocator returns null and logs exactly one `WARN` in the `memory` module
whose message holds the byte count and the alignment. A SYCL runtime that throws for the request
instead of returning null makes the test skip, since the refusal is then not made in this file.

## test_usm_registry.cpp

The registry holds values only, so every test but the threaded one runs small.

### `fake`

Registry keys are opaque and nothing is dereferenced, so distinct fake addresses are enough, and the
test needs no `Runtime`.

### `K_EXTENT`

The fake addresses sit 64 bytes apart, so a 64-byte extent covers exactly one of them and never
reaches its neighbour.

### `TakeReportsTheOwner` and `ATakenPointerIsFoundByNoLookup`

The free path erases the entry and reads the owner in one step. A second free of the same pointer
must therefore be distinguishable from the first and must not report a stale device. The rows ask the
registry for the taken pointer through `take`, `owner_of` and `containing_allocation`.

### `RecordingAPointerTwiceKeepsTheLatestOwner`

Recording a pointer twice is reachable only if an allocator handed back an address the registry had
not been told was freed. The last writer wins, which matches the map assignment the runtime has
always done. The test asserts it so that a future change of container does not quietly make the
first writer win.

### `ConcurrentRecordAndTakeLoseNothing`

Buffers are constructed and destroyed from whichever thread owns them, so several threads write the
registry at once. Every pointer the test records is taken exactly once, by exactly one thread. Each
thread keeps what its takes returned, and the test compares them with the expected owners after the
join.

### `RecordOfReportsTheDeviceAndKindAtTheBaseOnly`

record_of() returns the device and kind given to record(), no kind for an entry recorded without
one, and nothing for an interior pointer or null.

### `FindsTheAllocationOnlyInsideItsExtent`

The graph builder needs this case: a caller offsets into a plan array and hands over an address that
the table, which is keyed by base address, cannot find. The rows are an interior byte, the base, the
last byte, one past the end, an unrelated pointer and null.

### `AnEntryWithoutALengthResolvesOnlyAtItsBase`

An adopted address whose importer knew no size must not read as zero-length and must not swallow
offsets it cannot vouch for.

## test_usm_release_observer.cpp

The file tests USMReleaseObserver: the allocator tells an observer of each release, with its
address.

| Test | What it proves |
| --- | --- |
| `HearsEachReleaseWithItsAddress` | The observer hears the release of the address the allocator gave. |
