# scheduler

The work-stealing scheduler, its deque, the event poller and the rebalancer.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## event_polling.hpp

### PolledEvent::ready

A host task whose execute_host() runs inline returns an empty event, so there is nothing to wait on.
The scheduler still routes it through the poller, to trigger successors and release uniformly, but
flags it ready so completion is not gated on a SYCL event status query. This also sidesteps
toolchains (AdaptiveCpp) whose host operations are not observable through command_execution_status.

### PolledEvent::has_event

has_event is cached at push time so the poll loop can decide whether to wait or query without
constructing a throwaway `sycl::event{}` on every iteration. That default construction can
heap-allocate in DPC++, and the comparison runs per event per poll.

### ActiveEventList

The common case (at most INLINE_CAP outstanding events) stays in an inline buffer with no
allocation. Beyond that the list spills to a heap vector, the same inline-then-spill shape as
SmallEventVector. Dropping an event would orphan its successors (their in_degree never reaches zero)
and deadlock the run, so push() must always store, even if that means growing.

### Per-worker backoff state

Backoff and stall state is intrinsically per-worker: it tracks the polling cadence of the one thread
that owns the list. It lives in ActiveEventList, not on the shared EventPolling instance, so
concurrent pollers cannot race on it. An interleaved decrement could wrap a counter to about 2^64
and wedge every future poll into the early-return branch, stranding in-flight events and hanging the
run.

### set_profiling

The scheduler sets the flag once when it arms a run, from the context's profiling choice. The re-arm
at the end of a run happens on a worker thread (the sentinel's host_work), concurrently with other
workers polling, so the flag is an atomic with release/acquire ordering and not a plain bool.

### set_inflight_tracker

The poller is the single chokepoint where a task's scheduling life ends (release on completion) and
where new ready successors are born (trigger_successors). It therefore owns the increment and
decrement of the in-flight counter the scheduler blocks on in TaskScheduler::wait_quiescent() to
know a run has fully drained. A null tracker, one never wired, makes every hook do nothing.

### drop_inflight

The brief lock closes the classic lost-wakeup window: a waiter that reads a non-zero count just
before the store would otherwise sleep past the notify. Locking the same mutex the waiter waits
under orders the store ahead of its predicate re-check.

### poll_events: wait() on a complete event

In AdaptiveCpp, and in some other SYCL implementations, get_info\<complete> might return true before
implicit USM data migrations are fully visible. The event is already known to be complete, so wait()
returns at once, but it guarantees that the SYCL runtime flushes memory visibility.

### poll_events: stall drive

Some SYCL runtimes (AdaptiveCpp) use a lazy event model: command_execution_status never advances to
complete through non-blocking polling, and only an explicit wait() drives the DAG. After a few
consecutive polls with no progress, poll_events() blocks on the oldest pending event to force it
forward and marks it ready, so it is reaped on the next pass however the runtime reports status. On
runtimes whose status advances on its own (Intel), polling completes before the threshold, so the
stall drive rarely fires and the hot path is unchanged.

### poll_events: order of completion

A completed entry goes through four steps in a fixed order.

Profiling comes first. Every node passes through this point, so the per-node invocation count and
the kernel device time are added here exactly once per dispatch. The step does nothing unless the
run was armed for profiling.

In a distributed build the completion observer runs next, before the successors are released. A
write's version bump is then visible to any dependent task, an offloaded reader for example, by the
time that task is dispatched.

trigger_successors() follows, and drop_inflight() comes last. The task's scheduling life ends at
drop_inflight(), and its successors are already counted by add_inflight inside trigger_successors().
The in-flight count therefore never dips to zero in the middle of a graph; it reaches zero only once
the terminal sentinel is reaped.

### accumulate_profiling

poll_events() calls accumulate_profiling() before trigger_successors() and drop_inflight(). The zero
transition in drop_inflight() is what wakes a waiting complete() into collect_profiling() (or a next
reserve()), so every ledger read and write must finish in accumulate_profiling() first, not after
either of those calls.

### trigger_successors

trigger_successors() asks the task which of its dependents this completion freed, then makes each
one runnable: it takes the scheduling reference the queue will own, counts it in flight, and lands
it. The task answers the first question because only it knows how its dependents are stored; the
second half stays in the scheduler because only the scheduler knows the queue. Neither has to name
the other's type.

The buffer is thread-local and reused, so a steady-state step does no allocation, and pushing stays
inlined through the concrete queue. A virtual call per freed successor measured 4.6x slower on the
wide fan-out benchmark.

## ready_sink.hpp

### ReadySink

ReadySink is the seam between the two halves of a completion. Deciding which successors became
runnable is the task's own business: only it knows how its dependents are stored and what counts as
satisfied. Making a runnable task actually run is the scheduler's: the reference it must hold, the
in-flight count it must keep, the queue it must land on. Splitting them at ReadySink lets the
scheduler drain a graph it cannot name the type of, which is what keeps the layer boundary pointing
one way.

It is a buffer rather than a callback interface, and the difference is measured, not stylistic. A
virtual call per freed successor put an un-inlinable barrier in the middle of the completion loop
and cost 4.6x on the wide fan-out benchmark (286 ns/task to 1303). Collecting into a plain array
leaves one virtual call per completion and lets the scheduler keep pushing through its own fully
inlined queue.

The buffer belongs to the reaping worker and is cleared before each completion, so it is never
shared and needs no synchronisation.

## rebalancer_interfaces.hpp

### get_completion_rate

get_completion_rate() returns a running total, not a per-interval rate; callers diff two snapshots
to compute a rate over an interval.

## task.hpp

### on_complete

The poller calls on_complete() once when the task is reaped, on the reaping worker's thread. A task
that nothing depends on does nothing; a graph node walks its successors, satisfies one dependency on
each, and hands the sink the ones that have no dependencies left. The scheduler never has to know
which kind it reaped.

### submits_device_work

The scheduler reads submits_device_work() to decide between two host-execution paths. True, the safe
default for every task, routes through execute_host with a real queue, so a host_work lambda that
itself submits a kernel is dispatched on the queue owning its data. False marks a pure-CPU task the
scheduler runs natively through execute_native(), skipping queue acquisition entirely. A task
overrides it to false only when the work provably touches no SYCL device API.

### RangeTask

A RangeTask represents work for [start, end) and can donate the lower half to a thief through
split(). The start index is atomic so a split and an execute racing on the same task are both safe:
each sees a consistent range. The graph layer uses it to expose large parallel_for ranges as
steal-friendly units.

## task_scheduler.hpp

### TaskScheduler

TaskScheduler is built from a WorkerPoolSettings, a Topology::TopologyView, a Device::DeviceQueueSet
and a Memory::UsmAllocator, and holds the last three by reference; all three must outlive it. It
names no type of the execution module.

TaskScheduler owns a fixed-size pool of worker threads started once (start()) and kept alive across
graph runs. Each run is gated by a pair of arm_for_execution() and submit(root); the sentinel node's
completion promise unblocks the caller. Workers steal from node-local victims first; cross-node
steals are tracked separately so the RunReport can report the balance within and across NUMA nodes.

### first_core

The requested range is clamped into the machine's core count at construction, so first_core reports
what is in effect and not what was asked for. A caller verifying that it reserved cores should read
it together with num_workers.

### drain_scratch_

Each scratch vector is owned by exactly one worker (indexed by its thread id) and only touched
inside its own drain, so it needs no synchronisation. It exists so the drain stops allocating: a
local vector meant one heap allocation per worker per drain, which on a frame loop is a per-tick
allocation on the hot path and the thing that keeps a steady-state tick from being allocation-free.
Capacity is retained between drains, so it settles at the high-water mark after the first few steps
and never allocates again.

### SubmissionQueue

A std::deque as the injector allocated a fresh block every 64 pushes: with 8-byte elements libstdc++
fits exactly 64 pointers in its 512-byte block, and an injector that is pushed at the back and
popped at the front walks forward through the block map forever, buying a new block at every
boundary and releasing the one behind it. On a frame loop that is a heap allocation every 64 ticks
on the thread driving the run. It is small and perfectly periodic, and it is enough to break the
zero-allocation contract run(RunReport&) makes. CI measured it as exactly 1 allocation over 64 ticks
and 4 over 256.

A vector with a read cursor has no block boundaries to cross. The steady state drains fully every
tick, so pop_front() reaches the end, clears, and reuses the same storage: capacity settles at the
high-water mark and nothing is allocated again. This is the same trick drain_scratch_ already uses,
applied to the container instead of to the batch copied out of it.

The interface is the subset of std::deque the injector used, so every call site reads unchanged.

### SubmissionQueue: rewind

When pop_front() drains the queue it rewinds to the front of the same buffer. clear() keeps the
capacity, and this is the path that makes a steady-state tick free of allocation.

### SubmissionQueue: compaction

A queue that never empties would otherwise keep the consumed prefix forever. Dropping it is a
memmove within the existing buffer, not a reallocation, and the threshold keeps it off the path a
small queue takes.

### pending_count_ and priority_count_

Every worker checks the injectors on every dispatch iteration, but the common steady state is "both
empty": priority bypass is rare and external submissions arrive in bursts. Taking pending_mutex
unconditionally turned that check into a single global cache line ping-ponging across all cores,
throttling submit throughput. The two counters shadow the container sizes and are mutated only under
pending_mutex, so the hot path can read them relaxed and skip the lock entirely when they are zero.
A stale zero is harmless: the enqueuer always notifies and the loop re-checks. A stale non-zero,
which is rare, just takes the lock and finds nothing.

### run_failed_

run_failed_ aborts only the current run. Remaining tasks drain without executing so the DAG still
reaches the sentinel, and the worker pool stays alive and re-armable. It is distinct from
shutdown_requested, which permanently tears the pool down. arm_for_execution() clears it at the
start of each round.

### run_cancelled_

Like run_failed_, run_cancelled_ makes remaining tasks drain without executing (the sentinel still
runs), but the run completes normally instead of throwing: the drive loop sees the flag and stops.
Unlike run_failed_ it is not cleared per step by arm_for_execution(). A cancellation aborts the
whole multi-step run, so only reset_cancellation() clears it, at the start of a fresh run.

### pinning_

pinning_ is read by dispatch_loop() on every worker and by build_steal_topology(). Both run after
the constructor has written it and it is never mutated afterwards, so no synchronisation is needed.
It is copied from the WorkerPoolSettings the constructor receives, so the worker's first action
reads a member of the scheduler.

### WorkerCompletedCounter

The previous `alignas(CACHE_LINE_SIZE) atomic<size_t>[]` aligned only the array's start, so eight
8-byte counters shared one cache line and false-shared under contention. The elements were also
default-constructed with an indeterminate value in C++17, feeding the thermal strategy garbage
rates. Wrapping each counter in a cache-line-aligned, zero-initialised struct, as WorkerTimeBuckets
does, fixes both.

### inflight_

The completion sentinel signals the caller from its host_work, which runs on a worker thread before
that worker and its peers have finished the poll bookkeeping (trigger_successors and release) for
the run's final nodes. Returning from execute() then destroys the CompiledGraph while those workers
still touch its nodes: a use-after-free the TSan lane catches in Node::~Node. inflight_ is
incremented at submit() and at successor push and decremented after each poll release, so it reaches
zero only once every node has been fully finalized. wait_quiescent() blocks the caller on that.

### queue_device_index

queue_device_index() serves one caller: the NUMA rotation, DeviceQueueSet::queue_or_numa_local(),
returns a queue and not an index. On a profiled round the scan finds that queue's device so the
dispatch still goes through dispatch_queue().

### arm_for_execution

arm_for_execution() resets the running flag so workers continue their dispatch loops for the
upcoming graph. It must be called before the first root node of each round is submitted. Reusing the
persistent worker pool this way avoids creating and destroying threads between successive graph
runs.

### set_profiling_ledger

The pointer is only ever dereferenced between set_profiling_ledger() and the matching disarm(),
which clears it back to null. The caller must not let that window outlast the CompiledGraph that
owns the ledger, and must call set_profiling_ledger() again, with a ledger or with null, before
arming a different plan.

### disarm

disarm() is the counterpart of arm_for_execution(), and the reason a worker costs nothing between
runs. Without it the armed flag stayed set from the first run until the context was destroyed, so an
idle pool never reached the wait in the dispatch loop: every worker sat in the steal backoff,
yielding and sleeping in 100 microsecond increments, sweeping every peer deque thousands of times a
second for work that was not coming. On a machine the runtime shares with a host's render, audio and
job threads, such a pool competes with them while it has nothing to do.

disarm() is only safe once the run has quiesced. The wait predicate does not look at the per-worker
deques, so disarming while a worker still holds queued tasks would park its peers and leave it to
drain the graph alone. ExecutionEngine::complete() calls it after wait_quiescent(), which is the
point where quiescence is guaranteed.

### submit_batch

Each step re-injects every root through submit(), paying one pending_mutex acquire and one
condition-variable notify per root. submit_batch() collapses a step's whole root set into one lock
acquisition, one inflight_ increment and one wake burst, so the injector cost no longer scales with
root fan-out. It is semantically identical to calling submit() per root: same add_ref, same
quiescence accounting, same priority split. The single-task submit() remains for the coordinator
re-injection path, so there is still one injector seam per shape.

### wait_quiescent

wait_quiescent() returns once every task submitted for the run has been reaped by the poller, which
means no worker still touches a graph node. ExecutionEngine::execute calls it after the sentinel
signals completion and before it destroys the CompiledGraph, closing the window in which a worker
finalizing the run's last nodes races the graph's destruction. The inflight_ section describes the
counter it waits on.

## thermal_proxy_strategy.hpp

### ThermalProxyStrategy

ThermalProxyStrategy identifies the worker whose queue is growing fastest but completing fewest
tasks (a thermal or load-spike signature) and moves half of its backlog to the idlest worker. It
fires only when the queue imbalance exceeds IMBALANCE_THRESHOLD and the victim's completion rate is
at or below THROTTLING_RATE.

## work_stealing_deque.hpp

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
store on bottom, and steal() and pop() only read items[i] for i \< bottom. Zeroing all CAPACITY
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

## workload_coordinator.hpp

### WorkloadCoordinator

WorkloadCoordinator runs a dedicated low-overhead coordinator thread that calls
IRebalanceStrategy::evaluate_and_rebalance every 5 ms. The strategy inspects live telemetry through
ITelemetryProvider and issues task migrations through ITaskMigrator. The strategy can be swapped at
runtime while the coordinator is running.

### strategy_mtx_

strategy_mtx_ guards strategy_ against a concurrent set_strategy() destroying the object the monitor
thread is mid-call on. It is held only across the fast evaluate_and_rebalance call and the swap,
never across the 5 ms sleep.
