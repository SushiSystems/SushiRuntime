# work_stealing

The module holds the lock-free queues of the scheduler. The Chase-Lev deque is the one each worker
owns: the owner pushes and pops at one end, thieves steal at the other, and a push past the capacity
parks the task in an overflow list. The two bounded rings carry values between threads without a
lock: many threads into one (`MPSCRing`) and one into one (`SPSCRing`). It is an internal module:
its headers reach the modules that name it and the tests, and are never installed. It is
header-only, and its names are in `SushiRuntime::Internal::Scheduler`.

It depends on `core` and `task`; the rings need `core` alone.

## Public entry points

| Header | Holds |
| --- | --- |
| `work_stealing_deque.hpp` | `Deque` |
| `mpsc_ring.hpp` | `MPSCRing<Value, Capacity>` |
| `spsc_ring.hpp` | `SPSCRing<Value, Capacity>` |

## mpsc_ring.hpp

### MPSCRing

A bounded ring of `Capacity` slots, a power of two, that any number of threads push into and one
thread pops from. Each slot holds a sequence word beside its value. A producer claims the next
position with a compare-and-swap on the head, writes the value and publishes it by storing
position + 1 in the slot's sequence with release order; the consumer pops a slot whose sequence
reads position + 1 and frees it for the push one lap later by storing position + `Capacity`.

`try_push()` never blocks: on a full ring it returns false and leaves the value with the caller,
who decides what to do with it. `try_pop()` returns false on an empty ring, and also while the
oldest claimed slot is not yet published, so a producer stopped between its claim and its publish
holds back the values behind it until it publishes. Only the consumer thread may pop.

## spsc_ring.hpp

### SPSCRing

A bounded ring of `Capacity` slots, a power of two, between one producer thread and one consumer
thread. The producer owns the head and the consumer the tail, each on its own cache line; the
producer publishes a slot through a release store of the head and the consumer frees one through a
release store of the tail. `try_push()` returns false on a full ring and leaves the value with the
caller; `try_pop()` returns false on an empty one.

## work_stealing_deque.hpp

A private header: it sits in `source/`, nothing outside the module includes it, and it is
not installed.

### Deque

A fixed-capacity power-of-2 ring buffer avoids dynamic resizing and the memory-reclamation hazards a
growable lock-free buffer would carry. The owner thread pushes and pops the bottom (LIFO); stealer
threads steal from the top (FIFO). Each slot is a std::atomic\<Task*> so the array is accessed
atomically.

Overflow never drops. push() is single-producer (owner-only), so when the ring is full the surplus
goes into an owner-private overflow buffer that needs no synchronization; pop() drains it back into
the ring as space frees up. A dropped task would leave its successors' in_degree above zero forever
and deadlock the run, so dropping is not an option. Overflowed tasks are not visible to stealers
until drained, which is acceptable: the owner always makes progress on them, and overflow is a rare
pathology.

### Deque(): uninitialised slots

The constructor default-initializes the slots, with no trailing "()". A slot is always written by
push() before any consumer can read it: push stores the slot, then publishes through the release
store on bottom, and steal() and pop() only read items[i] for i \< bottom. Zeroing all K_CAPACITY
slots up front is therefore wasted work that faults in the whole 2 MB ring eagerly. Leaving them
indeterminate lets the pages fault lazily as the deque actually grows.

### push: publishing through bottom

push() publishes the slot write by releasing it through the bottom store and not through a
standalone release fence. The two are equivalent for correctness, since a stealer's acquire load of
bottom orders the slot write before its own read. A release store on the very variable the consumer
acquires is synchronization ThreadSanitizer models precisely, whereas it does not fully track
standalone atomic_thread_fence and falsely flags the published task as racy.

### steal: relaxed emptiness pre-check

steal() opens with a relaxed emptiness check. A stealer scanning idle peers, and every worker at
end-of-step quiescence, hits empty victims far more often than not, and the full protocol pays a
seq_cst fence (an MFENCE on x86) on every attempt. The check skips the protocol when the ring is
obviously empty. It can only ever make steal() fail spuriously, which the caller already tolerates:
it retries or scans other victims, and the owner still makes progress. It never returns a stale
task, because the fenced protocol re-reads and re-validates through the top CAS before returning
anything.
