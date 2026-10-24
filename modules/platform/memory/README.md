# memory

Allocation ownership, the USM registry and epoch-based reclamation.
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

It is a singleton reached through instance().

### enter: re-entrancy

enter() is re-entrant: only the outermost enter() publishes the slot. A nested guard, such as
DependencyTracker::resolve() invoked from a worker task that already holds a dispatch-loop guard,
must not re-stamp, and its matching exit() must not clear the slot early.

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

## usm_registry.hpp

### USMRegistry

USM is bound to the context it was allocated from: a pointer must be freed against that device's
queue, and a kernel touching it must run there. SYCL offers no way to ask a pointer where it came
from that is reliable across backends, so the runtime records it at allocation and looks it up
later.

USMRegistry has an invariant of its own: a pointer is known from allocation until it is freed,
exactly once. That invariant is testable without standing up a Runtime.

Every operation takes the same lock. That is not a bottleneck in practice: allocation happens when
the caller constructs a Buffer or State, which is a setup-time act, and a measured run allocates
nothing per step.

### record

Recording a pointer that is already known replaces its entry instead of rejecting it, which matches
an allocator handing back an address whose free was never reported. `device_index` is the index of
the owning device as the caller numbers its queues; the registry never interprets it.

### containing_allocation

containing_allocation() scans every entry, because the table is keyed by base address and an
interior pointer hashes nowhere near its own allocation. That cost is linear in live allocations,
and it is why this is a separate query and not a widening of owner_of(). Registration paths may call
it; dispatch must not, and resolves exact base addresses through owner_of() in constant time.

### take

The free path needs both the lookup and the erase and must not be interruptible between them:
looking up and then erasing under two separate locks would let a second free of the same pointer
read an owner the first is already acting on. A null pointer resolves to nothing.

## usm_request.hpp

### USMRequest

A request states the USM kind, whether an alignment is asked for, and a device index. A caller that
starts from a NUMA node resolves it to a device index first, so placement by NUMA stays out of the
memory module.

An alignment of 0 is how a request says it has no alignment to ask for. It selects the plain SYCL
call (`malloc_host`, `malloc_shared`, `malloc_device`); any other value selects the aligned one
(`aligned_alloc_host`, `aligned_alloc_shared`, `aligned_alloc_device`) and is passed through
unchecked, so an alignment SYCL refuses comes back as a null address.

## usm_release_observer.hpp

### USMReleaseObserver

The distributed layer keeps a table of buffers keyed by USM address and must drop a buffer's row
when its memory goes away. The observer lets it hear that without the memory module including a
distributed header, and without an `#ifdef` in the release path.

on_release() runs before the registry forgets the address and before the memory is freed. A row
dropped after the free could be matched by a new allocation that received the same address.

The destructor is protected and not virtual because the allocator never owns its observer.

## usm_allocator.hpp

### USMAllocator

The allocator copies the queues it is given. A `sycl::queue` is a handle, so the copies name the
same queues as the caller's, and the memory module needs no header of the module that builds them.
A device's index is its position in that list, the number USMRegistry stores.

The queue list never changes after construction and is read without a lock. Each registry
operation takes that registry's lock, and the observer is one atomic pointer beside a count of
the calls into it. Calls on different
addresses can therefore run on any threads at once. Two calls on the same address, a release and a
lookup for example, are not ordered by the allocator; whoever owns the address orders them.

The destructor frees nothing. An allocation still recorded at that point belongs to an owner that
outlived the allocator, and live_count() is how a teardown check finds it.

### allocate: the two failures

A device index that names no device is a caller's mistake and stops the process through
`SR_VERIFY`; the message carries the index and the device count. An allocation SYCL
cannot satisfy returns null and records nothing, because running out of device memory is a
condition a caller can handle.

### release

An adopted address is forgotten and not freed. It was allocated outside SYCL, a mapping of
another process's memory for example, and `sycl::free` on it is undefined. The allocator tells
adopted addresses apart with a second USMRegistry that holds only them, because USMRegistry's
entry has no field for it. allocate() clears that mark for the address it returns: an adopted
mapping that was unmapped without forget() can leave its address to a later allocation, and the
stale mark would then leak that allocation.

An address with no record is not freed, and one ERROR names it. It is either USM that was never
allocated here or a second release of the same address, and the allocator cannot tell which:
freeing it would be a double free in the second case and a free against a guessed device in the
first. An allocator with no device logs a WARN instead.

release() is `noexcept`, because USMAllocationOwner calls it from a `noexcept` override and
Allocation calls that from a destructor. A `sycl::free` that throws is caught and logged as one
ERROR; the record is already gone by then, so the address is not tracked.

### set_release_observer

release() counts itself in before it loads the observer and out after on_release() returns.
set_release_observer() stores the new pointer and then yields until that count is zero, so when
it returns no call into the previous observer is running and the caller may destroy it. Both
sides use `std::memory_order_seq_cst`: the store-then-read on one side and the increment-then-load on the other
are the Dekker pattern, and a weaker order lets each side miss the other.

The count covers every release() between those two points, whichever observer it loaded, so a
removal made while other threads release without pause waits for a gap. An observer that calls
set_release_observer() from on_release() waits for itself and never returns.

### shares_context

`shares_context(first, second)` compares the contexts of two devices' queues. The task module's
`placement_of()` asks it before a stated device wins over the device that owns a node's memory,
since USM is only usable from the context it was allocated in. `device_count()` lets it check an
index first, because `shares_context()` stops the process on an index past the devices.

### owning_device

The record is the first answer and costs one hash lookup. Without a record the allocator asks each
queue's context for the pointer's kind and device. A shared allocation that matches is returned at
once; a device allocation that matches is kept while the remaining queues are tried, so a shared
match on a later queue wins. Host USM belongs to a context and to no device, and resolves to
nothing. A context that throws for an address it does not know counts as no match.
