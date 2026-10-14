# scheduler

The work-stealing scheduler, its deque, the event poller and the rebalancer.
The module is internal: its headers are for the other modules of the library and are not installed.

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

### PolledEvent::executed

executed is true for a task whose work ran to its end, which is the default, and false otherwise:
a node of an aborted run, a late-bound node disabled for the step, or a node whose work threw. The
poller reaps both kinds the same way and reports only an executed graph node to the
offload coordinator. The flag sits in the padding after has_event, so the record stays 40 bytes
under the MSVC ABI, where it was measured.

### ActiveEventList

The common case (at most K_INLINE_CAP outstanding events) stays in an inline buffer with no
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

When an offload coordinator is installed, its on_local_completion() runs next for a graph node
that executed, before the successors are released. A node the scheduler released without running it
wrote nothing, so the coordinator is not told of it. A write's version bump is then visible to any
dependent task, an offloaded reader for example, by the time that task is dispatched. With none
installed the step is one pointer load and a branch never taken.

trigger_successors() follows, and drop_inflight() comes last. The task's scheduling life ends at
drop_inflight(), and its successors are already counted by add_inflight inside trigger_successors().
The in-flight count therefore never dips to zero in the middle of a graph; it reaches zero only once
the terminal sentinel is reaped.

### poll_events: an event that throws

get_info and wait() can throw. An entry whose query or wait throws is turned, in that pass, into
what skipped_event() of task_scheduler.cpp builds for a task whose work threw: ready, with no event,
not executed. It then takes the four steps above like any other entry. Profiling writes nothing for
an entry with no event, the offload coordinator is not told, the successors are released and the
in-flight count drops, so the run reaches its sentinel. Each such entry is logged at error level
with its task's name. The first failure of the pass is kept as a `KernelFailed` `Error` and returned
after the backoff update, as a `std::optional<Error>` that is empty when nothing threw;
dispatch_loop() records it in the RunOutcome. The wait on the oldest
event after K_STALL_DRIVE_THRESHOLD idle polls goes the same way: an event whose wait throws
there is reaped as not executed and its error is returned. Left in the list, the entry would be queried again on
every poll, and an event that throws each time would keep wait_quiescent() from returning.

The translated call covers the status query and the wait of one entry. A pass in which nothing
throws allocates nothing and takes no lock.

A successor released by a failed entry is in the worker's deque before dispatch_loop() records the
failure, so a thief can dispatch it in that interval.

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

### completed_task_count

completed_task_count() returns a running total, not a per-interval rate; callers diff two snapshots
to compute a rate over an interval.

### migrate_tasks

migrate_tasks(from_worker_id, count) names no destination, because no implementation picks one: the
tasks go where any worker can take them.

## failure_naming.hpp

### name_failure

describe_task() names a task for a failure report: its label, and the registration index when it
is a graph node. name_failure() puts that name in front of the message of an `Error` and keeps
the code. The header is in `include/` because both layers that run a node use it: execute_task()
of this module for a node a worker dispatches, and `ChainRun::submit_step()` of `execution` for a
node of an in-order chain. The same failing node therefore reads the same from either layer.

## run_outcome.hpp

### RunOutcome

RunOutcome holds the two ways a run ends early: the first failure of a task, as an `Error`, and a
cancellation asked for from another thread. TaskScheduler holds one by value and its run-error
members forward to it. The dispatch loop reads is_aborting() once per task, which is two atomic
loads.

### A failure

record() keeps the first `Error` and drops later ones; error() returns it as a
`std::optional<Error>`, empty while no task has failed. A failure aborts only the current run.
Remaining tasks drain without executing so the DAG still reaches the sentinel, and the worker
pool stays alive and re-armable. It is distinct from shutdown_requested, which permanently tears
the pool down. arm_for_execution() calls clear_failure() at the start of each round.

### A cancellation

Like a failure, a cancellation makes remaining tasks drain without executing (the sentinel still
runs), but the run completes normally instead of throwing: the drive loop sees the flag and stops.
Unlike a failure it is not cleared per step by arm_for_execution(). A cancellation aborts the
whole multi-step run, so only reset_cancellation() clears it, at the start of a fresh run.

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

A range splits when the upper half, the one the owner keeps, holds at least 1024 elements, which a
range of 2047 or more does. is_splittable() and split() both ask the private can_split(), so a true
is_splittable() is followed by a chunk unless a concurrent split wins the exchange. The constructor
stops the process through `SR_VERIFY` for start > end; start == end is an empty range that runs
nothing.

## task_scheduler.hpp

### TaskScheduler

TaskScheduler is built from a WorkerPoolSettings, a Internal::Topology::TopologyView, a Internal::Device::DeviceQueueSet
and a Internal::Memory::USMAllocator, and holds the last three by reference; all three must outlive it. It
names no type of the execution module.

TaskScheduler owns a fixed-size pool of worker threads started once (start()) and kept alive across
graph runs. Each run is gated by a pair of arm_for_execution() and submit(root); the sentinel node's
completion promise unblocks the caller. Workers steal from node-local victims first; cross-node
steals are tracked separately so the RunReport can report the balance within and across NUMA nodes.

### first_core

The requested range is clamped into the machine's core count at construction, so first_core reports
what is in effect and not what was asked for. A caller verifying that it reserved cores should read
it together with worker_count().

### drain_scratch_

Each scratch vector is owned by exactly one worker (indexed by its thread id) and only touched
inside its own drain, so it needs no synchronisation. It exists so the drain stops allocating: a
local vector meant one heap allocation per worker per drain, which on a frame loop is a per-tick
allocation on the hot path and the thing that keeps a steady-state tick from being allocation-free.
Capacity is retained between drains, so it settles at the high-water mark after the first few steps
and never allocates again.

### SubmissionQueue

A vector with a read cursor backs the injector. It has no block boundaries, so pop_front() advances
the cursor. When the queue drains, it clears and reuses the same storage. Capacity settles at the
high-water mark, and the queue allocates nothing after that.

The interface is the subset of std::deque the injector uses.

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

### pinning_

pinning_ is read by dispatch_loop() on every worker and by build_steal_topology(). Both run after
the constructor has written it and it is never mutated afterwards, so no synchronisation is needed.
It is copied from the WorkerPoolSettings the constructor receives, so the worker's first action
reads a member of the scheduler.

### WorkerCompletedCounter

Each counter sits in a cache-line-aligned, zero-initialised struct, as WorkerTimeBuckets does, so no
two counters share a cache line.

### quiescence_

The completion sentinel signals the caller from its host_work, which runs on a worker thread before
that worker and its peers have finished the poll bookkeeping (trigger_successors and release) for
the run's final nodes. Returning from execute() then destroys the CompiledGraph while those workers
still touch its nodes: a use-after-free the TSan lane catches in Node::~Node. inflight_ is
incremented at submit() and at successor push and decremented after each poll release, so it reaches
zero only once every node has been fully finalized. wait_quiescent() blocks the caller on that.

### queue_device_index

queue_device_index() serves one caller: the NUMA rotation, DeviceQueueSet::queue_or_numa_local(),
which only a task that is not a graph node takes, returns a queue and not an index. On a profiled
round the scan finds that queue's device so the dispatch still goes through dispatch_queue().

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

disarm() is the counterpart of arm_for_execution(). It clears the armed flag, so an idle pool waits
in the dispatch loop.

disarm() is only safe once the run has quiesced. The wait predicate does not look at the per-worker
deques, so disarming while a worker still holds queued tasks would park its peers and leave it to
drain the graph alone. SchedulerExecutionEngine::complete() calls it after wait_quiescent(), which is the
point where quiescence is guaranteed.

### submit_batch

Each step re-injects every root through submit(), paying one pending_mutex acquire and one
condition-variable notify per root. submit_batch() collapses a step's whole root set into one lock
acquisition, one inflight_ increment and one wake burst, so the injector cost does not scale with
root fan-out. It is semantically identical to calling submit() per root: same add_ref, same
quiescence accounting, same priority split. The single-task submit() remains for the coordinator
re-injection path, so there is still one injector seam per shape.

### wait_quiescent

wait_quiescent() returns once every task counted into the run has been reaped by the poller or
handed to the offload coordinator, which means no worker still touches a graph node. SchedulerExecutionEngine::execute calls it after the sentinel
signals completion and before it destroys the CompiledGraph, closing the window in which a worker
finalizing the run's last nodes races the graph's destruction. The inflight_ section describes the
counter it waits on.

## thermal_proxy_strategy.hpp

### ThermalProxyStrategy

ThermalProxyStrategy identifies the worker with the longest queue among those completing few tasks
(a thermal or load-spike signature) and calls migrate_tasks() for half the gap between that queue
and the shortest one, at least 10 tasks. The tasks go to the global priority queue, where any worker
takes them; the worker with the shortest queue is named in the log line only. It
fires only when the queue imbalance exceeds K_IMBALANCE_THRESHOLD and the victim's completion rate is
at or below K_THROTTLING_RATE.

## workload_coordinator.hpp

### WorkloadCoordinator

WorkloadCoordinator runs a dedicated coordinator thread that calls
RebalanceStrategy::evaluate_and_rebalance every 5 ms. The strategy inspects live telemetry through
TelemetryProvider and issues task migrations through TaskMigrator. The strategy can be swapped at
runtime while the coordinator is running.

### strategy_mtx_

strategy_mtx_ guards strategy_ against a concurrent set_strategy() destroying the object the monitor
thread is mid-call on. It is held only across the fast evaluate_and_rebalance call and the swap,
never across the 5 ms sleep.

## Implementation

The persistent worker pool: dispatch, work stealing, event polling hand-off and the
distributed offload branch. The headers are in `include/SushiRuntime/scheduler/`. This page
records why `scheduler.cpp` behaves as it does; the source comments state the invariant and
point here.

### scheduler.cpp

#### Sizing and placing the pool

The constructor takes a `WorkerPoolSettings`, a `Internal::Topology::TopologyView`, a
`Internal::Device::DeviceQueueSet` and a `Internal::Memory::USMAllocator`. It includes no header of `execution`;
`Internal::Execution::worker_pool_settings()` maps a `RuntimeSettings` onto the first argument. The
settings size and place the worker pool: how
many workers to run, which core range to bind them into, and whether to bind at all. The
defaults reproduce the sole-tenant behaviour, one worker per logical core with worker i on
core i. A host that shares the machine names a narrower range, so its own threads keep
their cores.

A core range that runs past the last core is clamped and logged. The OS affinity calls
accept an out-of-range mask and do not bind, so without the warning a configuration would
look as if it reserved cores and would not: the isolation the caller asked for would be
absent without a word.

Placement is the co-tenancy guarantee. Bound to core `first_core_ + id`, a worker cannot
drift onto a core the host reserved for its main, render or audio thread. `PinPolicy::Float`
leaves placement to the OS.

#### Steal topology

`build_steal_topology()` runs once, at construction. For each worker, all other workers are
ordered by ascending NUMA distance from that worker's node, with ties broken randomly to
spread contention. Node-local victims therefore lead every list, and `local_tier_len_`
records how many there are. If the provider exposes no CPU topology, every worker maps to
node 0, which gives one flat tier and the NUMA-blind behaviour.

Under `PinPolicy::Float` a worker has no fixed core and so no fixed node, and the same flat
single tier is the only honest answer. Ordering victims by a distance that does not hold
would make stealing worse, by sending a worker to a "local" victim it shares no node with.

`find_task_to_steal()` walks the list in order. The node-local tier comes first, with a
rotating start so that idle workers do not all hammer the same victim. Remote tiers follow
in ascending distance and are reached only once the local tier is exhausted. That keeps a
task on the node where its data lives and avoids the remote memory hop NUMA-blind stealing
would pay.

#### Starting once

`start()` spawns the workers exactly once. A second call would assign over live, joinable
`std::thread` handles, which calls `std::terminate`, and would create a second set of owners
for the per-worker Chase-Lev deques, breaking their single-producer invariant without any
sign. Workers begin waiting; `arm_for_execution()` must be called before any work is
submitted.

#### Arm, disarm, stop

`running` is one term of the idle wait predicate, so `arm_for_execution()`, `disarm()` and
`stop()` change it under `pending_mutex`. `arm_for_execution()` also clears the previous
round's failure and publishes the plan's profiling flag to the poller before workers run, so
completion accumulates per-node timing under profiling and reads no timestamps otherwise.

In `stop()` a lost wakeup is worse than a late start. The idle wait has no timeout, so a
worker that missed the shutdown flag would never be woken again and the join would never
return. `stop()` is idempotent, and the destructor calls it so that cleanup happens even if
the owning `SchedulerExecutionEngine` did not call `shutdown()`.

#### The injector and its shadow counters

External submissions go into a dedicated injector queue. Pushing them straight into a
per-worker deque would violate the single-producer constraint of the Chase-Lev deques.

`pending_count_` and `priority_count_` shadow the sizes of the two injector queues. They are
the source of truth for emptiness and are changed only under `pending_mutex`, so they can be
read without it. That keeps the idle and shutdown liveness checks off the global lock, which
an armed, idle pool polls often between runs. In `drain_pending_submissions()` it lets a
worker skip the mutex entirely when the injector is empty, the steady-state case; the
declaration of `pending_count_` says why a stale read is safe. In the dispatch loop it keeps
the per-iteration cost of the priority check at one relaxed atomic load, because a priority
submission is rare.

A worker whose local deque is at half capacity pulls nothing more from the injector, which
prevents the Chase-Lev deque from overflowing. A drain takes at most 4096 tasks, to keep the
local deque from overflowing and to spread load fairly, and wakes another worker when work
is left.

The batch buffer belongs to the worker and keeps its capacity across drains, so the steady
state does not allocate. A local vector there meant one heap allocation per worker per
drain: invisible in a batch benchmark, an allocation per tick on a frame loop.

#### The dispatch loop

A worker's life has three phases.

1. Idle: `running` is false and there is no work. The worker waits until armed.
2. Active: `running` is true. The worker processes tasks and polls events.
3. Drain: `running` is false again because `stop()` was called. The worker continues until
   in-flight events and pending submissions are drained, then exits.

This lets the pool be reused across graph execution rounds without recreating threads.

##### Parking

The owner's own deque is part of the idle condition. Nothing in the wait predicate looks at
it, and nothing can: the deques are lock-free and no notify accompanies a push. A worker
that parked while holding queued tasks would strand them until something unrelated woke it.
The idle branch was unreachable after the first run while the armed flag was never cleared,
which is why the omission never showed; `disarm()` makes the branch reachable, so the check
has to be there.

The park has no timeout. Every term of the predicate is changed under `pending_mutex`, by
`arm_for_execution`, `stop`, `submit`, `submit_batch`, `drain_pending_submissions` and the
rebalancer's rescue path, so a state change cannot be missed and a periodic re-check would
find nothing.

This is the co-tenancy guarantee for a pool that is not running. A 10 ms poll cost every
worker a wakeup and a `pending_mutex` acquisition a hundred times a second while the runtime
did nothing: cache and lock traffic against the host's own threads on a machine where the
runtime is a guest, and enough to keep a laptop out of its deep idle states. A parked worker
costs nothing.

##### Polling

`poll_events` touches SYCL events through `get_info` and `wait`, which can surface an
asynchronous or device error. Uncaught, it would escape the `std::thread` body and call
`std::terminate`. The loop records it in the run's `RunOutcome`, as `execute_task` does: the
current run fails and the pool stays re-armable.

##### Backoff

A thread must be outside the epoch during its backoff, so a sleeping thread does not stall
global memory reclamation. A worker holding in-flight events never sleeps, because its
successors wait on its poll; it yields. Others yield for the first 100 spins and then sleep
100 microseconds at a time.

Stall-attribution timing is gated on `profiling_active_`, so a run without profiling takes
no clock samples in this loop.

##### The offload branch

The branch is `OffloadGate::offer()` of `offload_gate.hpp`, which the scheduler holds by value.
With no coordinator installed it is one load of the gate's pointer and a branch
never taken; the node's offload state is not read. With one installed, only nodes its
`prepare()` marked offloadable reach `try_offload()`, one virtual call each. The marking runs
through `OffloadGate::prepare()`, which the engine calls once per compiled plan. The gate adds
one to an atomic for each offer the coordinator takes; `offloaded_task_count()` returns it, and
it is the engine's only figure about offload.
The run's failed and cancelled flags are read inside that branch, after the offloadable test,
so a node that is not offloadable pays nothing for them. An offloadable node of an aborting
run is not offered; it goes to `execute_task()`, which skips it.
On a successful offload the scheduling reference passes to the coordinator and is released
when it returns the node through `complete_remote()` or `resubmit()`, so the loop neither
executes nor releases the task. Those two members are defined in `source/offload_return.cpp`.

The gate sits with the scheduler's read-mostly members, after the allocator reference,
away from the counters workers write. It holds a plain pointer: `set_offload_coordinator()`
writes it through `install()` only while the pool runs no node.

An offloaded node has left the local poll path and will not be reaped there, so its
quiescence count is retired at once. Its remote completion re-triggers successors through
the ordinary `submit()` and poll cycle, which counts them afresh. A transient dip to zero is
harmless: the sentinel cannot fire, and so nobody waits on the counter, until every
offloaded node's result has landed.

#### execute_task

##### Aborting and disabled nodes

When the current run is aborting, because a task threw or the run was cancelled, no more
kernels execute. The task is routed through the poller flagged ready, so its successors
still fire and the DAG drains to the sentinel instead of deadlocking on a successor nobody
triggered. The sentinel is exempt: it must run to deliver the outcome and unblock
`execute()`.

A late-bound node that is disabled for the step, a sleeping island or a system switched off
by level of detail, runs no kernel either and takes the same route, so the DAG drains as it
would for a completed node. Both kinds are pushed by `skipped_event()` with `executed` false,
so the poller does not report them to the offload coordinator. Toggling a subsystem costs no
recompile. The relaxed load of `enabled` is safe: the step boundary's completion barrier and
the acquire/release submit path supply the happens-before. The sentinel carries no bindings,
so it is never skipped.

##### Completion accounting

A completion is counted against the executing worker's id, not the EBR slot id. EBR slot ids are
handed out in order of first epoch touch across all threads, so they do not match worker ids. The
read side, `completed_task_count`, uses the worker id too.

##### Downcast

The downcast to `Internal::Graph::Node` goes through the virtual `is_graph_node()` type tag, not RTTI.
`EventPolling` uses the same tag.

##### Inline host tasks

A pure-CPU host task needs no queue and runs inline. Under profiling its timing follows the run's
plan flag, not the context-wide setting. The dispatch step is captured before the task runs, not
read at reap, so a later round's step cannot land on the record. The steady-clock times are
written raw and converted onto the run's event clock at `collect()`. A host node submits no
dispatch, so its `launched_width` and `live_width` stay `K_WIDTH_UNKNOWN`.

##### Queue selection

Both the KERNEL and the HOST path must run on the queue whose context owns the node's USM. A
HOST task is as dangerous as a KERNEL one: its `host_work` commonly submits a device kernel
on exactly the queue it is handed, so a sibling device's queue picked round-robin causes a
cross-context illegal access, for example CPU-allocated USM dispatched onto a CUDA queue.
A graph node therefore has one placement, and `Internal::Graph::placement_of()` in the task module is
the one function that names it, from the first of these that applies:

1. The node names a region a device owns: the owner of its first write region, else of its
   first read region.
2. The node states a placement with `Node::place_on()`: that device.
3. Otherwise device 0, `Internal::Graph::K_DEFAULT_PLACEMENT`, the device a default buffer is allocated
   on.

A kernel that captured USM the runtime cannot see names no region, so rule 2 or 3 decides
where it runs, and it runs there on every step. A node whose region no device owns, such as
a host address used as a dependency key, also falls to rules 2 and 3.
`TaskMetadata::numa_affinity` plays no part in the choice.

A single kernel cannot dereference USM from two device contexts that are not peers. A node
whose regions straddle a device boundary would hit an illegal cross-context access, so the
first dispatch of a node checks co-location and fails the run with a precise error. The
result is cached on the node; nodes are dispatched serially across steps, so a plain flag is
safe and steady-state replay pays nothing. A failure is not cached, so a graph that is still
misconfigured fails again on a later run. Cross-device execution itself needs region
ownership and halo exchange, which the runtime does not have; the check enforces the
co-location contract the layer above must honour.

Both steps are `place_for_dispatch()` of `source/dispatch_placement.cpp`, a function of the node,
the allocator and the device count that execute_task() calls for a node it has not placed yet.
The first dispatch asks `placement_of()` once, after the co-location check, and caches the
answer on the node as `cached_queue_index_`. Steady-state replay then routes through
`DeviceQueueSet::queue(index)`, an indexed read, and calls neither `placement_of()` nor
`USMAllocator::owning_device()`. `place_for_dispatch()` returns `Result<void>`. A placement at or
past the device count is an `ErrorCode::InvalidGraph` error with the index and the count, and
execute_task() names the node in it and fails the run; like a co-location failure it is not
cached, so it recurs on a later run.

A task that is not a graph node has no placement. It takes
`DeviceQueueSet::queue_or_numa_local()` for NUMA node 0, which rotates over the devices of
that node and returns a queue. Every branch goes through `dispatch_queue()`, except that
the rotation without profiling uses the returned queue and skips the index lookup.

`execute_host()` takes a vector of dependencies, so the rarer path of a HOST node that
submits its own device work still allocates one. The kernel path calls `depends_on()` per
event over the node's inline storage, which avoids the heap vector
`SmallEventVector::to_vector()` built there before.

The first device submission to reach the kernel path claims the run's clock pairing.

##### The event pushed to the poller

An empty event means the host work already ran inline, in a synchronous `execute_host`, and
there is nothing to wait on. It is routed through the poller flagged ready, so successors
fire and the task is released like any other, without a fabricated barrier event.

`push()` never drops: the list spills to the heap past its inline capacity. Dropping an
event would leave the task's successors untriggered for good and deadlock the run. `ready`
means "no real event"; `has_event` is its inverse, cached so the poll loop never constructs
a `sycl::event{}` to compare against.

##### A throwing task

A task that throws fails only its run. The dispatch runs inside
`Internal::SYCL::translate_exceptions()` of `modules/foundation/sycl`, which turns what is
thrown into an `Error`: `KernelFailed`, or `AllocationFailed` for refused memory. name_failure()
puts the task's name in the message, and an `Error` a dispatch step returned keeps its code. The
first one is recorded in the scheduler's
`RunOutcome`, but `shutdown_requested` is not latched, so the pool survives and can be re-armed for
the next graph. The failed task is pushed to the poller as ready so its successors still
fire; with the skip path for an aborting run, the whole DAG drains to the sentinel.

In `RunOutcome::record()` only the first failing task wins and later errors are dropped.
The mutex makes the error write and the flag publish atomic relative to other workers,
and its release pairs with the acquire in `run_failed()` and `run_error()`, so the sentinel
sees a fully written `Error`.

#### submit and submit_batch

`submit()` counts the task into the run before it becomes reachable to workers, so the
quiescence counter can never miss it. The count is balanced by the poller's release on
completion, or by the offload decrement when a coordinator accepts the node. The poller
owns the counter's
increment and decrement, because it is where tasks are born as successors and reaped on
completion.

`submit_batch()` mirrors `submit()` per node, with the same `add_ref`, priority split and
quiescence count, and spreads the fixed cost over the whole root set: one `pending_mutex`
acquisition, one `inflight_` increment and one wake burst, where there would be one of each
per root. The `inflight_` increment stays inside the locked region, so tasks are counted
before any worker can drain them, the same happens-before `submit()` relies on. Roots are
independent and there are `num_workers_` threads, so one `notify_all` replaces a storm of
`notify_one` calls; idle workers that find no work sleep again.

#### Offload coordinator

`TaskScheduler` implements `Internal::Graph::OffloadSink` and knows the coordinator only as a
`Internal::Graph::OffloadCoordinator`; `modules/engine/task/README.md` describes the seam.
`set_offload_coordinator()` calls `bind(*this)` on a non-null coordinator, then stores the
pointer in the scheduler and in the poller. A null pointer removes the coordinator from both.

The poller passes every graph node the pool executed to `on_local_completion()` before the
node's successors are released. A node skipped by an aborting run or disabled for the step is
not passed. What the coordinator does there is its own rule; see
`modules/extension/distributed/README.md`. An offloaded node never reaches that call: the coordinator
settles it itself before it calls `complete_remote()`.

`resubmit()` takes back a node no worker finished, orphaned by a dead worker or failed
remotely. `submit()` is the thread-safe injector, so calling it from a coordinator thread is
safe. The node still carries the scheduling reference that passed to the coordinator at
offload time. `submit()` takes its own reference, so `resubmit()` drops the transferred one
right after. Exactly one scheduling reference lives on, owned by the ready queue, and event
polling releases it when the node completes. Successors are not released, because the node
has not run.

`complete_remote()` takes back a node a worker ran. It walks `Node::for_each_successor()`
and satisfies one dependency on each successor, as `Node::on_complete()` does for a local
node, but a ready successor goes through `submit()`, because this code runs on a coordinator
thread and not on a worker. The walk covers the cross-region boundary successors of the
dynamic region graph. Last, the scheduling reference that passed to the coordinator at
offload is dropped.

#### migrate_tasks

`migrate_tasks()` steals up to `count` tasks, lock-free, from the congested worker and puts
them on the global priority queue for cooler workers to take, then wakes every sleeping or
idle worker.
