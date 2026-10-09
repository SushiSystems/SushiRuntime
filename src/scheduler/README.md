# scheduler

The persistent worker pool: dispatch, work stealing, event polling hand-off and the
distributed offload branch. The headers are in `include/SushiRuntime/scheduler/`. This page
records why `scheduler.cpp` behaves as it does; the source comments state the invariant and
point here.

## scheduler.cpp

### Sizing and placing the pool

The constructor takes a `WorkerPoolSettings`, a `Topology::TopologyView`, a
`Device::DeviceQueueSet` and a `Memory::UsmAllocator`. It includes no header of `execution`;
`Execution::worker_pool_settings()` maps a `RuntimeSettings` onto the first argument. The
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

### Steal topology

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

### Starting once

`start()` spawns the workers exactly once. A second call would assign over live, joinable
`std::thread` handles, which calls `std::terminate`, and would create a second set of owners
for the per-worker Chase-Lev deques, breaking their single-producer invariant without any
sign. Workers begin waiting; `arm_for_execution()` must be called before any work is
submitted.

### Arm, disarm, stop

`running` is one term of the idle wait predicate, so `arm_for_execution()`, `disarm()` and
`stop()` change it under `pending_mutex`. `arm_for_execution()` also clears the previous
round's failure and publishes the plan's profiling flag to the poller before workers run, so
completion accumulates per-node timing under profiling and reads no timestamps otherwise.

In `stop()` a lost wakeup is worse than a late start. The idle wait has no timeout, so a
worker that missed the shutdown flag would never be woken again and the join would never
return. `stop()` is idempotent, and the destructor calls it so that cleanup happens even if
the owning `ExecutionEngine` did not call `shutdown()`.

### The injector and its shadow counters

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

### The dispatch loop

A worker's life has three phases.

1. Idle: `running` is false and there is no work. The worker waits until armed.
2. Active: `running` is true. The worker processes tasks and polls events.
3. Drain: `running` is false again because `stop()` was called. The worker continues until
   in-flight events and pending submissions are drained, then exits.

This lets the pool be reused across graph execution rounds without recreating threads.

#### Parking

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

#### Polling

`poll_events` touches SYCL events through `get_info` and `wait`, which can surface an
asynchronous or device error. Uncaught, it would escape the `std::thread` body and call
`std::terminate`. The loop routes it through `record_run_error`, as `execute_task` does: the
current run fails and the pool stays re-armable.

#### Backoff

A thread must be outside the epoch during its backoff, so a sleeping thread does not stall
global memory reclamation. A worker holding in-flight events never sleeps, because its
successors wait on its poll; it yields. Others yield for the first 100 spins and then sleep
100 microseconds at a time.

Stall-attribution timing is gated on `profiling_active_`, so a run without profiling takes
no clock samples in this loop.

#### The offload branch

For the overwhelming majority of tasks, the local ones, the offload branch is a single bool
load. Only nodes that `compile()` stamped offloadable, in Master mode, reach the
coordinator. On a successful offload the scheduling reference passes to the coordinator and
is released when the remote result fires `complete_offloaded_node`, so the loop neither
executes nor releases the task.

An offloaded node has left the local poll path and will not be reaped there, so its
quiescence count is retired at once. Its remote completion re-triggers successors through
the ordinary `submit()` and poll cycle, which counts them afresh. A transient dip to zero is
harmless: the sentinel cannot fire, and so nobody waits on the counter, until every
offloaded node's result has landed.

### execute_task

#### Aborting and disabled nodes

When the current run is aborting, because a task threw or the run was cancelled, no more
kernels execute. The task is routed through the poller flagged ready, so its successors
still fire and the DAG drains to the sentinel instead of deadlocking on a successor nobody
triggered. The sentinel is exempt: it must run to deliver the outcome and unblock
`execute()`.

A late-bound node that is disabled for the step, a sleeping island or a system switched off
by level of detail, runs no kernel either and takes the same route, so the DAG drains as it
would for a completed node. Toggling a subsystem costs no recompile. The relaxed load of
`enabled` is safe: the step boundary's completion barrier and the acquire/release submit
path supply the happens-before. The sentinel carries no bindings, so it is never skipped.

#### Completion accounting

A completion is counted against the executing worker's id, not the EBR slot id. EBR ids are
handed out in order of first epoch touch across all threads. A non-worker thread that
compiles the graph takes slot 0 and shifts every worker, so keying by EBR id credited the
wrong worker and fed the thermal strategy false per-worker rates. The read side,
`completed_tasks_for`, already uses the worker id.

#### Downcast

The downcast to `Graph::Node` goes through the virtual `is_graph_node()` type tag, not RTTI.
It runs for every dispatched task, and `dynamic_cast` there was the one RTTI query left on
the hot path. `EventPolling` uses the same tag, so the two downcast sites share one
strategy.

#### Inline host tasks

A pure-CPU host task needs no queue and runs inline; the comment this page replaces cites
"design spec §0". Under profiling its timing follows the run's plan flag, not the
context-wide setting. The dispatch step is captured before the task runs, not read at reap,
so a later round's step cannot land on the record. The steady-clock times are written raw
and converted onto the run's event clock at `collect()`. A host node submits no dispatch, so
its `launched_width` and `live_width` stay `WIDTH_UNKNOWN`.

#### Queue selection

Both the KERNEL and the HOST path must run on the queue whose context owns the node's USM. A
HOST task is as dangerous as a KERNEL one: its `host_work` commonly submits a device kernel
on exactly the queue it is handed, so a sibling device's queue picked round-robin causes a
cross-context illegal access, for example CPU-allocated USM dispatched onto a CUDA queue.
The queue is routed by the data pointer, the first write region or else the first read
region, and falls back to NUMA-local round-robin only when the node carries no USM to anchor
on.

A single kernel cannot dereference USM from two device contexts that are not peers. A node
whose regions straddle a device boundary would hit an illegal cross-context access, so the
first dispatch of a node checks co-location and fails the run with a precise error. The
result is cached on the node; nodes are dispatched serially across steps, so a plain flag is
safe and steady-state replay pays nothing. A failure is not cached, so a graph that is still
misconfigured fails again on a later run. Cross-device execution itself needs region
ownership and halo exchange, which the runtime does not have; the check enforces the
co-location contract the layer above must honour.

The same lookup yields the owning queue index, which is cached on the node. Steady-state
replay then routes through `DeviceQueueSet::queue(index)`, an indexed read, and not through
`UsmAllocator::owning_device()`. Only a hit in the allocator's record, `device_of()`, is
cached. On a miss `owning_device()` asks SYCL which device owns the anchor, and that device
is routed by index too. A node with no anchor, or an anchor no device owns, takes
`DeviceQueueSet::queue_or_numa_local()`, which rotates over the devices of the NUMA node and
returns a queue. Every branch goes through `dispatch_queue()`, except that the rotation
without profiling uses the returned queue and skips the index lookup.

`execute_host()` takes a vector of dependencies, so the rarer path of a HOST node that
submits its own device work still allocates one. The kernel path calls `depends_on()` per
event over the node's inline storage, which avoids the heap vector
`SmallEventVector::to_vector()` built there before.

The first device submission to reach the kernel path claims the run's clock pairing.

#### The event pushed to the poller

An empty event means the host work already ran inline, in a synchronous `execute_host`, and
there is nothing to wait on. It is routed through the poller flagged ready, so successors
fire and the task is released like any other, without a fabricated barrier event. The
earlier barrier relied on host-operation status polling, which AdaptiveCpp does not report.

`push()` never drops: the list spills to the heap past its inline capacity. Dropping an
event would leave the task's successors untriggered for good and deadlock the run. `ready`
means "no real event"; `has_event` is its inverse, cached so the poll loop never constructs
a `sycl::event{}` to compare against.

#### A throwing task

A task that throws fails only its run. The first exception is recorded and `run_failed_` is
set, but `shutdown_requested` is not latched, so the pool survives and can be re-armed for
the next graph. The failed task is pushed to the poller as ready so its successors still
fire; with the skip path for an aborting run, the whole DAG drains to the sentinel.

In `record_run_error()` only the first failing task wins and later exceptions are dropped.
The mutex makes the exception write and the flag publish atomic relative to other workers,
and its release pairs with the acquire in `run_failed()` and `run_error()`, so the sentinel
sees a fully written exception.

### submit and submit_batch

`submit()` counts the task into the run before it becomes reachable to workers, so the
quiescence counter can never miss it. The count is balanced by the poller's release on
completion, or by the offload decrement in Master mode. The poller owns the counter's
increment and decrement, because it is where tasks are born as successors and reaped on
completion.

`submit_batch()` mirrors `submit()` per node, with the same `add_ref`, priority split and
quiescence count, and spreads the fixed cost over the whole root set: one `pending_mutex`
acquisition, one `inflight_` increment and one wake burst, where there would be one of each
per root. The `inflight_` increment stays inside the locked region, so tasks are counted
before any worker can drain them, the same happens-before `submit()` relies on. Roots are
independent and there are `num_workers` threads, so one `notify_all` replaces a storm of
`notify_one` calls; idle workers that find no work sleep again.

### Distributed master

`set_coordinator()` installs a completion observer: in Master mode every registered buffer a
locally completed node wrote has its version bumped. Combined with the DAG ordering, any
dependent task, notably an offloaded reader, is dispatched only after the version reflects
the write, so a stale worker cache is forced to pull again. Offloaded writers are handled
the same way in `complete_offloaded_node()`. Both call
`DistributedCoordinator::bump_buffer_version()`, which bumps the registry the coordinator was
built on, the one `RuntimeContext::bump_distributed_buffer()` bumps by hand.

The resubmit hook lets the coordinator re-inject a node orphaned by a dead worker or by a
failed remote result. `submit()` is the thread-safe injector, so calling it from the
coordinator's transport or monitor thread is safe. The orphaned node still carries the
scheduling reference that passed to the coordinator at offload time. `submit()` takes its
own reference, so the hook drops the transferred one right after, as
`complete_offloaded_node()` does. Exactly one scheduling reference lives on, owned by the
ready queue, and event polling releases it when the node completes.

In `complete_offloaded_node()` a failed remote result is treated like a lost worker: the
node is re-injected and runs on a survivor or, once the retry cap is reached, locally.
Versions are not bumped and successors are not released on failure, because the
authoritative buffers were not validly produced.

On success the offloaded write has already landed in the master's authoritative buffer
through the eager writeback, so the version is advanced and later dependents pull again.
Successors are released as `EventPolling::trigger_successors` releases them, but ready ones
go through `submit()`, because this code runs on the coordinator's transport thread and not
on a worker. Cross-region boundary successors of the dynamic region graph are released the
same way; the list is empty for ordinary offloaded nodes. Last, the scheduling reference
that passed to the coordinator at offload is dropped.

### migrate_tasks

`migrate_tasks()` steals up to `count` tasks, lock-free, from the congested worker and puts
them on the global priority queue for cooler workers to take, then wakes every sleeping or
idle worker. The `to_worker_id` argument is unused.
