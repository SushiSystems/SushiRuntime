# memory

The USM allocator and its allocation owner, the residency directory with its retire observer and
coherence interval map, the horizon that retires coherence history (`CoherenceHorizon`), the memory spaces (`MemorySpace`, `DeviceMemorySpace`, `HostMemorySpace`, `FileMemorySpace`)
and the pool sum `usable_pool_bytes`, and epoch-based reclamation.
The module is internal: its headers are for the other modules of the library and are not installed.

Everything is in `SushiRuntime::Internal::Memory`.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## epoch_reclamation.hpp

### EpochThreadLocalState

In addition to the retire list, EpochThreadLocalState owns a thread-local pool of RetiredNode
objects (free_node_pool). Without the pool, every call to EpochManager::retire would invoke the
global operator new. That is an allocation cost in the very mechanism that exists to manage
allocations, and it introduces a hidden interaction with the system allocator on the hot path of any
data structure using EBR, the dependency tracker's access history for example.

The pool is bounded by K_FREE_POOL_CAP. When reclamation would push beyond the cap, the excess node
goes to the global operator delete. This keeps memory bounded under workloads that retire faster
than they reclaim.

### nest_depth

enter() only publishes the epoch slot on the outermost entry (0 -> 1) and exit() only clears it on
the outermost exit (1 -> 0). Without nest_depth, a nested guard's exit() would zero the slot in the
middle of a critical section and let another thread reclaim nodes the outer guard is still
traversing, a use-after-free.

### EpochManager

EpochManager maintains a global epoch counter and per-thread epoch slots. Threads enter and leave
critical sections with enter() and exit(); objects retired through retire\<T>() are deleted only
after every active thread has advanced past the epoch in which they were retired. Thread slots are
cache-line padded to eliminate false sharing. The manager is thread-safe and lock-free on the fast
path.

It is an ordinary object, not a singleton: its owner builds it and passes it to what retires into it
(the DependencyTracker takes one or makes its own). A thread's state is kept per manager, and a
thread that outlives its manager frees what it still held for it when it exits.

### enter: re-entrancy

enter() is re-entrant: only the outermost enter() publishes the slot. A nested guard of the same
manager must not re-stamp, and its matching exit() must not clear the slot early.

### enter: the seq_cst fence

The release store that publishes the slot does not stop this thread's subsequent shared-structure
reads from being reordered ahead of the slot becoming globally visible (store-buffer forwarding on
x86 TSO, free reordering on ARM). Without a full fence a concurrent retire() or reclaim could miss
the slot and free a node this thread is about to dereference, a classic Dekker-style store/load
race. A seq_cst fence orders the slot publication before any later load, as production EBRs do
(folly, crossbeam).

### retire: when a scan runs

retire() runs check_and_reclaim() when the thread's retire_count reaches next_scan_at, which
starts at K_RETIRE_THRESHOLD and is set to the count left after each scan plus K_RETIRE_THRESHOLD. A
reader that holds a guard for a long time therefore costs one scan per K_RETIRE_THRESHOLD retires,
where a fixed threshold would scan on every retire once the list had grown past it.

### check_and_reclaim: deleters that retire

The scan detaches the thread's retire list and zeroes retire_count before it runs any deleter,
and sets is_reclaiming for its duration. A destructor that calls retire() therefore appends to an
empty list and does not start a second scan; the scan puts the nodes it kept back in front of
what the deleters added.

### check_and_reclaim: the seq_cst fence

The scan opens with a `std::memory_order_seq_cst` fence, the counterpart of the one in enter(). The retiring thread
unlinks an object and then reads the slots; a reader publishes its slot and then reads the shared
structure. With a fence on both sides, either the scan sees the reader's slot or the reader sees
the unlink. This holds only if the caller unlinked the object before retire(), which is retire()'s
precondition.

### unregister_thread

EpochThreadLocalState's destructor calls unregister_thread() when a thread exits. The next
check_and_reclaim() picks up the nodes in `remaining_nodes`.

### ~EpochThreadLocalState

Free pool storage belongs to this thread alone, so the destructor releases it directly. Orphaned
retire_list nodes are owned by the EpochManager by then and may come from any thread's pool, so they
cannot go back to this pool.

## residency_view.hpp

### ResidencyView

The port `engine/coherence` reads and writes the directory through. It extends `HandleResolver` and
adds what a planner needs and nothing else: `inspect()`, the coherence map of a handle,
`set_instance()`, `drop_instance()` and `instance_kind()`. `EntryView` and `ElementLayout` are
declared here with it. Minting, retiring, describing and the retire observer stay on
`ResidencyDirectory`, so a planner cannot call them. A test fake implements the port without a
table behind it.

## residency_directory.hpp

### ResidencyDirectory

`ResidencyDirectory` implements `HandleSpace` from `foundation/handle` and `ResidencyView`, which
carries `HandleResolver`. It is
one table indexed by the handle's slot. The table grows in chunks of `K_CHUNK_ENTRIES` entries; the
array of chunk pointers is sized for `K_MAX_CHUNKS` when the directory is built, and a chunk is made
the first time a slot in it is handed out. A chunk never moves, so a reader holds no lock: a resolve
loads one chunk pointer, compares the generation with an acquire load and tests one bit of the
valid-spaces mask. Writers (mint, retire, describe, set_instance, drop_instance) take one mutex
among themselves and never block a reader.

An entry holds its generation, a live flag, the size, the element layout, the home space and one
address per memory space, with a 64-bit mask of the spaces that hold one. Beside each address it
keeps the `USMKind` the instance was allocated as, stored as the kind plus one so that 0 means
unknown: memory a caller borrowed has no kind. `set_instance()` takes the kind, `drop_instance()`
and `retire()` clear it, `instance_kind()` reads it without a lock, and `EntryView::home_kind` is the
kind of the home instance. A caller that asks what kind of USM a buffer is therefore asks the
handle, and no table of addresses exists to ask. `K_MAX_MEMORY_SPACES` is
64, one bit of that mask each. The entry also holds the buffer's coherence interval map, which
`coherence()` returns for a live handle and `retire()` clears, and the generation of the last host
write on the buffer.

### Host writes

`record_host_write()` raises the directory's host-write generation by one, under the writer mutex,
and stores the new value in the entry; `host_write_of()` reads it back without a lock, and returns 0
for a handle the host never wrote or that is not live. `retire()` clears it, so a reused slot starts
at 0. A stored copy plan keeps the generation it was made at and the buffers it covers: while
`host_write_generation()` still equals that generation no buffer was written, which is one atomic
load per check, and otherwise a buffer whose mark is higher was written after the plan.

### retire_coherence_history

`retire_coherence_history()` calls `CoherenceIntervals::retire_history()` on every live entry under
the writer mutex and returns the intervals left across them. It runs between runs, at a horizon,
because the maps take no lock and the planner writes them while it plans.

### mint and retire: the ordered free list

A retired slot goes back to a free list that hands out the lowest slot first, and a slot never
handed out comes only after every freed one. The same program therefore gets the same handles in
every run. Retire raises the generation at once, so every copy of the old handle resolves to
`StaleHandle`. A slot whose generation wraps to 0 is not reused; generation 0 is the null handle.
`live_count()` counts the handles minted and not retired, Borrowed ones included.

The constructor refuses the universe `K_LOCAL_HANDLE_UNIVERSE` through `SR_VERIFY`. That universe
belongs to `LocalHandleSpace`, so a handle it minted resolves and retires to `ForeignUniverse`.

### retire: the observer

`set_retire_observer()` installs one `RetireObserver`. A successful `retire()` calls its
`on_retire()` with the retired handle, on the retiring thread, after the writer mutex is released,
so the observer may mint or retire in turn. A retire of a stale handle calls nothing. retire()
counts itself in before it loads the observer and out after on_retire() returns, and
`set_retire_observer()` stores the new pointer and then yields until that count is zero, so when
it returns no call into the previous observer is running and the caller may destroy it. Both sides
use `std::memory_order_seq_cst`: the store-then-read on one side and the increment-then-load on the
other are the Dekker pattern, and a weaker order lets each side miss the other. An observer that
calls set_retire_observer() from on_retire() waits for itself and never returns.

### resolve: a reader that races a retire

A retire stores the new generation, then a release fence, then clears the instances. A resolve
loads the mask and the address, then an acquire fence, then loads the generation again. A reader
that saw a cleared or reused instance therefore sees the new generation and returns `StaleHandle`,
never another handle's address. Because the slot goes back to the free list at once, nothing in the
directory is left for epoch-based reclamation to free.

### error_code_of and to_result

`error_code_of` turns a failed `HandleStatus` into the `ErrorCode` the runtime reports:
`StaleHandle` to `StaleHandle`, `NullHandle` and `ForeignUniverse` to `InvalidArgument`,
`NotResident` to `InvalidAccess` and `SpaceExhausted` to `AllocationFailed`. `to_result` wraps a
`ResolveOutcome` into a `Result<void*>` with that code.

## borrowed_index.hpp

`BorrowedIndex` gives memory the caller owns a handle with the `Borrowed` flag. It is the only
lookup from an address to a handle, and it serves borrowed memory only: a sorted map from the first
byte of each live range to its end, its handle and its count of borrows. `acquire()` returns the
live handle when the same range is borrowed again and mints one in the directory on the first
borrow; a range that overlaps a live range without equalling it is refused with
`InvalidArgument`, since two handles over one byte would lose an ordering. `release()` drops one
borrow and retires the handle with the last. Its calls run when a buffer is made or dropped and
take one mutex; no dispatch calls it.

## usm_request.hpp

### USMRequest

A request states the USM kind, whether an alignment is asked for, and a device index. A caller that
starts from a NUMA node resolves it to a device index first, so placement by NUMA stays out of the
memory module.

An alignment of 0 is how a request says it has no alignment to ask for. It selects the plain SYCL
call (`malloc_host`, `malloc_shared`, `malloc_device`); any other value selects the aligned one
(`aligned_alloc_host`, `aligned_alloc_shared`, `aligned_alloc_device`) and is passed through
unchecked, so an alignment SYCL refuses comes back as a null address.

## retire_observer.hpp

### RetireObserver

The distributed layer keeps the buffers it shares keyed by handle and must drop a buffer when its
handle is retired. The observer lets it hear that without the memory module including a
distributed header, and without an `#ifdef` in the retire path. The handle it hears does not
resolve, and its slot may already be minted again under a new generation, so the observer
compares handles, never slots.

The destructor is protected and not virtual because the directory never owns its observer.

## usm_source.hpp

### USMSource

The port `USMAllocator` takes its memory from. A source makes, frees and copies bytes and
answers three questions about its devices: how many there are, whether two of them share one
memory context, and a device's name for a log line. `copy()` moves bytes on a device's memory
engine and returns when they have arrived. A source keeps nothing about its allocations; the
allocator keeps the records. Both adapters
pass `USMSourceContract` (`tests/support/usm_source_contract.hpp`): `Small_HostUSMSource` and
`Medium_SYCLUSMSource`. The adapter classes live in their source files; a caller makes one through
its factory and holds it as a `USMSource`.

## sycl_usm_source.hpp

### make_sycl_usm_source

The device adapter: `sycl::malloc_*` and `sycl::aligned_alloc_*` on one queue per device,
`sycl::free` on release and `queue.memcpy()` for a copy. The header includes no SYCL header: the factory takes a
`SYCLQueues`, a move-only handle that holds the queues behind a pointer.

## sycl_queues.hpp

### make_sycl_queues

Wraps a `std::vector<sycl::queue>` in a `SYCLQueues`, a device's index being its position. This is
the one header of the module that includes `<sycl/sycl.hpp>`: a function that takes a `sycl::queue`
needs its definition, and neither DPC++ nor AdaptiveCpp offers a declaration to forward.

## host_usm_source.hpp

### make_host_usm_source

The in-memory adapter: every kind of USM comes from the host heap, aligned by over-allocation,
with the heap block's address stored in the word below the one returned, and a copy is a
`std::memcpy`. It makes no device and starts no thread, so the host runtime and the small tests of
the allocator run on it. Every device shares one context, and an alignment that is not a power of
two is refused with null. It keeps no record of its allocations.

## usm_allocator.hpp

### USMAllocator

The allocator owns one `USMSource` for its whole life and asks it for every byte, and keeps no
table of addresses. The residency directory records each instance, its space and its kind, and
every question about a buffer is asked of its handle there. A caller with device queues passes
`make_sycl_usm_source(make_sycl_queues(queues))`; a `sycl::queue` is a handle, so the copies name
the same queues as the caller's, and the memory module needs no header of the module that builds
them. A device's index is the source's.

The source never changes after construction and is read without a lock, so calls can run on any
threads at once. Two calls on the same address, a release and a copy for example, are not ordered
by the allocator; whoever owns the address orders them.

The destructor frees nothing. An allocation still live at that point belongs to an owner that
outlived the allocator.

### allocate: the two failures

A device index that names no device is a caller's mistake and stops the process through
`SR_VERIFY`; the message carries the index and the device count. An allocation the source
cannot satisfy returns null, because running out of device memory is a
condition a caller can handle. The allocator logs one WARN for it, under `sushiruntime.memory`,
with the kind, the bytes, the alignment and the device index and name; the status the caller
builds from the null does not log again.

### What the allocator records

`allocate()` writes the fine zone `memory.allocate` and the value `memory.allocate_bytes` with the
size of the request; `release()` writes the fine zone `memory.release`. Both go through the
`profile` front, so they cost an empty object when the profiler is compiled out.

### release

`release(address, device)` frees against the device the address was allocated on, which its owner
passes back: `USMAllocationOwner` keeps the device beside the allocator, the recorder's scratch keeps
it in its deleter, and the coherence planner's instance allocator derives it from the memory
space. A device the allocator does not hold frees nothing and logs one ERROR. The allocator cannot
detect a second release of the same address or memory it never made, since it keeps no record;
the owner that frees exactly once is the guard.

release() is `noexcept`, because USMAllocationOwner calls it from a `noexcept` override and
`Detail::Allocation` calls that from a destructor. A source free that throws is caught and logged as
one ERROR, and the memory stays allocated.

### copy

`copy(device, destination, source, bytes)` moves bytes on the named device's memory engine. The
caller names the device from the buffer's handle; a device the allocator does not hold stops the
process through `SR_VERIFY`, as allocate() does.

### shares_context

`shares_context(first, second)` asks the source whether two devices share one memory context;
the SYCL source compares their queues' contexts. The task module's
`placement_of()` asks it before a node whose kernel captured raw addresses runs on a stated device
other than the one that owns its memory, since USM is only usable from the context it was
allocated in. A node whose pointers are rebound to the stated device's instance is not asked
about. `device_count()` lets it check an index first, because `shares_context()` stops the process
on an index past the devices.

## coherence_intervals.hpp

### CoherenceIntervals

Each directory entry holds one map from `[begin, end)` to the valid spaces and the version of those
bytes. A write makes the written space the only valid one at a fresh version; a copy adds a space
and keeps the version. The map then joins neighbours with equal state, so a buffer written whole is
one interval again. `drop_space()` takes a space out of every interval, which is how a plan records
an eviction. The intervals sit in a sorted vector, so the order never depends on a hash.
The planner is the only writer of a buffer's map, at plan time, and the map takes no lock.

`retire_history()` sets every interval's version to 0 and joins the neighbours that then hold the
same spaces, so the history of writes no plan still reads is dropped. It returns the intervals
left. A later write takes a version above every version the map handed out before.

## coherence_horizon.hpp

### CoherenceHorizon

`CoherenceHorizon` counts the steps a runtime ran. `advance()` adds a run's steps, and when the
count reaches `steps_per_horizon()` it calls `ResidencyDirectory::retire_coherence_history()` and
starts the count again from 0. The interval is `K_STEPS_PER_HORIZON`, 64 steps, unless the
constructor names another; 0 counts as 1. `RuntimeContext` owns one, and both step drivers advance
it after a run's last step and after a `run_async()` step, when no plan is being made, so the maps
of a graph that runs forever stay the size of its state, not of its history.

## memory_space.hpp

### MemorySpace

Each kind of space is a class of its own behind `MemorySpace`, so the file space adds a file and
edits no sibling. A space measures its reserve once, when it is built, through a `MemoryProbe`:
the reserve is the bytes the probe reports as not free. Tests pass a fake probe.
`usable_pool_bytes` sums capacity less reserve per `shared_pool`, so an integrated GPU that shares
the host pool is counted once.

A space tells what it is through queries, never through a name. `is_host_memory()` is true for
the host space, whose bytes a copy reads or writes with one memcpy and no staging. `is_file_backed()`
is true for a file space, whose bytes are read through a stream. `kind_name()` is for logs only; no
code compares it. A new kind answers the queries it needs and no sibling changes.

## system_memory_probe.hpp

`SystemMemoryProbe` is the `MemoryProbe` of host memory. It reads the physical and available bytes
from `GlobalMemoryStatusEx()` on Windows and from `sysconf()` elsewhere; a refused query reports zero
bytes. `RuntimeContext` builds the host's `HostMemorySpace` through it.

## file_memory_space.hpp

### FileMemorySpace

A `FileMemorySpace` is one byte range of a file, named by `FileRange` (path, offset, bytes). Its
capacity is the range size, its reserve is zero, its pool is its own id, and it lists no direct
target, so no device addresses it and every copy out of it goes through a transfer path. It is
read-only: nothing writes to it.

The residency directory records a buffer's instance on a file space as the fixed token
`instance_address()` returns, with no kind of USM. The token is never dereferenced; it lets the
planner add a byte offset to the instance as it does for any other space, and `file_offset()` turns
the result back into a file offset. Because the token is a constant, the plan is the same in every
run. Because the instance has no kind, a release and a planned free leave it alone.
