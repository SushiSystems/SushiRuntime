# memory

Allocation ownership, the USM registry and epoch-based reclamation.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## epoch_reclamation.hpp

### EpochThreadLocalState

In addition to the retire list, EpochThreadLocalState owns a thread-local pool of RetiredNode
objects (free_node_pool). Without the pool, every call to EpochManager::retire would invoke the
global operator new. That is an allocation cost in the very mechanism that exists to manage
allocations, and it introduces a hidden interaction with the system allocator on the hot path of any
data structure using EBR, the dependency tracker's access history for example.

The pool is bounded by FREE_POOL_CAP. When reclamation would push beyond the cap, the excess node
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

### unregister_thread

EpochThreadLocalState's destructor calls unregister_thread() when a thread exits. The next
check_and_reclaim() picks up the nodes in `remaining_nodes`.

### ~EpochThreadLocalState

Free pool storage belongs to this thread alone, so the destructor releases it directly. Orphaned
retire_list nodes are owned by the EpochManager by then and may come from any thread's pool, so they
cannot go back to this pool.

## usm_registry.hpp

### UsmRegistry

USM is bound to the context it was allocated from: a pointer must be freed against that device's
queue, and a kernel touching it must run there. SYCL offers no way to ask a pointer where it came
from that is reliable across backends, so the runtime records it at allocation and looks it up
later.

UsmRegistry is separated from RuntimeContext because it is the one part of it with an invariant of
its own: a pointer is known from allocation until it is freed, exactly once. That invariant is
testable without standing up a Runtime, which is not true of anything else RuntimeContext does.

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
