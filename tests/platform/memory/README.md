# memory tests

The tests of the `memory` module, mirroring `modules/platform/memory/`.
`tests/platform/memory/CMakeLists.txt` builds them into `sr_memory_test_objects`, linked into `sr_platform_tests` and
`sr_memory_benchmarks`, built under `SR_BUILD_BENCHMARKS`. `sr test` runs them with the rest;
`sr run sr_platform_tests -- --gtest_filter='<pattern>'` runs one program alone.

A benchmark is a `Benchmark_` suite; this module has none now.

A section is named after a test file, and a subsection after the test, helper or topic it describes.
Sizes and the shared fixture base are described in [the tests README](../../README.md#sizes). The
allocator tests derive from that fixture base and skip where SYCL reports no device.

## compile_memory_log_category.cpp, compile_residency_view.cpp and compile_usm_request.cpp

Each file includes one header alone. See [the scheduler tests](../../engine/scheduler/README.md)
for why such a file stands in for a test.

## test_epoch_reclamation.cpp

The file covers epoch-based reclamation. Every test builds the `EpochManager` it uses, so no test
sees another's state, and the tests assert observable contracts: retired objects are eventually deleted once, `EpochGuard` enter and
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

## test_sycl_queues.cpp

| Test | What it proves |
| --- | --- |
| `Small_SYCLQueues.NoQueuesMakeASourceOfNoDevices` | `make_sycl_queues()` over no queues gives a source that counts no device; it builds no queue. |

## test_host_usm_source.cpp and test_sycl_usm_source.cpp

`USMSourceContract` (`tests/support/usm_source_contract.hpp`) runs as `Small_HostUSMSource` on
`make_host_usm_source()` and as `Medium_SYCLUSMSource` on `make_sycl_usm_source()` over the program's
queue.

| Test | What it proves |
| --- | --- |
| `ASharedAllocationKeepsItsBytes` | A shared allocation holds the bytes written through it. |
| `AnAlignedAllocationStartsOnItsAlignment` | An allocation with an alignment starts on it. |
| `ADeviceSharesItsContextWithItself` | A device shares its memory context with itself. |
| `ACopyPutsTheBytesInTheDestination` | `copy()` on a device puts the source bytes in a shared allocation. |
| `ASourceOfOneDeviceCountsOne` | A source of one device reports one. |
| `RefusesAnAlignmentThatIsNotAPowerOfTwo` | The host source refuses an alignment that is not a power of two with null. |
| `TwoDevicesShareOneContext` | Every pair of host devices shares one context. |
| `CountsTheDevicesItWasMadeWith` | A host source counts the devices it was made with. |

## test_residency_directory.cpp

| Test | What it checks |
| --- | --- |
| `MintResolveAndRetire` | A minted handle starts at slot 0, generation 1, resolves to the address set on a space, reports its description, and is stale once retired. |
| `AStaleHandleReturnsStaleHandle` | A retired handle resolves to `StaleHandle`, retires again to `StaleHandle`, and `to_result` reports `ErrorCode::StaleHandle`. |
| `NullAndForeignHandlesAreRefused` | The null handle gives `NullHandle`; a handle of another universe gives `ForeignUniverse`. |
| `AFreedSlotIsReusedLowestFirstUnderTheNextGeneration` | With slots 3 and 5 of 0..5 retired, the next mints get 3 at the next generation, then 5, then 6. |
| `GrowingPastOneChunkKeepsEarlierEntriesInPlace` | Minting past one chunk makes the second chunk and leaves the first entry at its address. |
| `AnInstanceKeepsItsKindUntilItIsDroppedOrRetired` | An instance set with a kind reports it through `instance_kind()` and `EntryView::home_kind`; one set without a kind, a dropped one and one on a retired or reused slot report none. |
| `LiveCountFollowsMintAndRetire` | `live_count()` rises with each mint and falls with each retire; a retire of a stale handle changes nothing. |
| `ReadersResolveWhileAWriterMints` | Four reader threads resolve one live handle while the test thread mints and retires four chunks of others; every resolve returns the same address. |

## test_borrowed_index.cpp

| Test | What it checks |
| --- | --- |
| `OneRangeGetsOneHandle` | One range borrowed twice gets one `Borrowed` handle, minted by the first call only. |
| `TheLastReleaseRetiresTheHandle` | The handle stays live until its last borrow is released, then is stale. |
| `DisjointRangesGetDistinctHandles` | Two disjoint ranges get two handles, and `find()` answers by containment. |
| `AnOverlappingRangeIsRefused` | A range inside or across a live range it does not equal is refused with `InvalidArgument`. |
| `ANullOrEmptyRangeIsRefused` | A null address or a range of no bytes is refused. |

## test_retire_observer.cpp

The file tests RetireObserver: the directory tells an observer of each handle it retires. The
observers are local to the file; no allocator or device is involved.

| Test | What it proves |
| --- | --- |
| `HearsEachRetiredHandleOnce` | The observer hears a retired handle once; a second retire of the stale handle is not heard. |
| `ARemovedObserverHearsNothingMore` | An observer removed before the retire is not told. |
| `AnObserverMayMintFromInsideTheCall` | `on_retire()` runs after the writer lock is released, so it can mint. |
| `SetRetireObserverReturnsOnlyAfterACallInFlightFinished` | Medium, since it hands a call over between real threads. An observer blocks inside `on_retire()`; a second thread removes it; the test opens the observer once the removal has begun and asserts the call had finished when the removal returned. It uses no sleep and no deadline. |

## test_usm_allocation_owner.cpp

The file tests USMAllocationOwner: an Allocation returns its USM to the allocator once, on its device.

| Test | What it proves |
| --- | --- |
| `DestroyingTheAllocationFreesTheUSMOnItsDevice` | Destroying the allocation frees the USM once, against the device the owner was given. A source local to the file records each free with its device. |
| `AnOwnerWithNoAllocatorLeavesTheMemoryAlone` | An owner with no allocator releases nothing; the memory it is handed keeps its value. |

## test_usm_allocator.cpp

The file covers `USMAllocator`. The release and copy tests run small, on an allocator over a
`make_host_usm_source(2)`. The kind test runs as rows and skips when no SYCL device supports the
row's kind; the alignment of the real SYCL allocation is checked by `Medium_AlignedAllocation` in
`tests/engine/execution/test_runtime_context.cpp`. The ERROR and WARN lines are read through
`LogCapture`.

### `AllocatesIsUsableAndIsReleased`

One row per kind. A device writes device USM; the host writes host and shared USM. The release on
the device it was allocated on logs no ERROR.

### `TheFirstReleaseLogsNothing` and `ANullReleaseLogsNothing`

A release of a block on its device, and a release of null, log no ERROR line.

### `AReleaseOnADeviceTheAllocatorDoesNotHoldLogsAndFreesNothing`

The test releases a heap block against a device index past the allocator's devices. The release
logs exactly one ERROR line, and the block keeps its bytes, which one whole-buffer comparison
checks.

### `CopyMovesTheBytesOnTheNamedDevice`

A copy into an allocation on the last device lands every byte.

### `ABadDeviceIndexStopsWithTheIndexAndTheCount`

allocate() and copy() on a device past the last one stop the process with the index and the count.

### `Small_UsmAllocatorFailure.LogsTheRefusedRequest`

The test asks a host source for the largest byte count, with an alignment of 64, under a `LogCapture` at
`Warn`. It asserts the allocator returns null and logs exactly one `WARN` in the `memory` module
whose message holds the byte count and the alignment.

## compile_memory_probe.cpp and compile_memory_space.cpp

Each file includes one interface header alone.

## fake_memory_probe.hpp

`FakeMemoryProbe` reports the total and free bytes it was built with and counts its calls, so a
test can check that a space probes once and derives its reserve from the result.

## test_coherence_intervals.cpp

The tests cover a partial write splitting one interval into three, a write invalidating every
other space, two copies joining back into one interval, a whole-buffer write leaving one
interval, and `drop_space()` removing a space and joining equal neighbours.

## test_device_memory_space.cpp and test_host_memory_space.cpp

Each builds a space on a fake probe and checks capacity, the measured reserve and sorted direct
targets.

## test_memory_pool_capacity.cpp

A host and an integrated GPU share pool 0 and a discrete GPU has pool 1; the sum counts pool 0 once.

## test_system_memory_probe.cpp

The probe reports some memory and no more free bytes than total.
