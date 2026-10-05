# distributed

The opt-in master-worker layer: a coordinator on the master, an agent on each worker, two
transports, and the registries both sides share. The headers are in
`include/SushiRuntime/distributed/`. This page records why the sources behave as they do; the
source comments state the invariant and point here.

## buffer_registry.cpp

Registering a pointer that is already tracked means the allocator handed the same USM
address out again. The registry keeps the stable id and bumps the version, so a worker that
cached the old bytes treats its copy as stale.

## offload_policy.cpp

`LeastLoadedOffloadPolicy` ranks workers by an estimated completion cost in microseconds. It
is the network-tier counterpart of the scheduler's NUMA distance: the cost of a worker is how
long the task is expected to take to get there, run and free its slot, approximated from
live telemetry.

    cost = rtt_micros + est_bytes / BYTES_PER_MICROSECOND + inflight * QUEUE_PENALTY_US_PER_TASK

- `BYTES_PER_MICROSECOND` is 1000, about 1 GB/s. The figure is a conservative one for a
  homogeneous lab network. It over-counts transfer cost slightly, so a large payload is
  offloaded only when a worker is worth the trip.
- `QUEUE_PENALTY_US_PER_TASK` is 1000. It is the delay each inflight task is assumed to add
  to a newcomer in that worker's queue, and it keeps the "least loaded" intent: between two
  equally near workers the emptier one wins.
- `rtt_micros` is refreshed by the coordinator's PING/PONG probe, so a distant or lagging
  worker loses rank without any extra rule.

A payload below `min_offload_bytes_` stays local, because the network would only add
latency. A worker whose `free_mem` is known and smaller than the payload is skipped. When no
worker is eligible the policy returns nothing and the task runs locally.

## distributed_coordinator.cpp

### Bookkeeping

`last_seen` and `worker_ops` are kept parallel to `workers` so that `WorkerStats`, the type
the offload policy sees, stays free of coordinator internals. `last_seen` is the liveness
clock. `worker_ops` is the op set the worker advertised at HELLO, kept so that `op_support`
can be undone when the worker dies.

`op_support` counts, per op id, how many workers expose it. The comment this page replaces
stated the intent as "an op is offloadable only when every live worker can run it (the
negotiated common kernel set)". `supports()` as written returns true when the count is above
zero.

`MAX_OFFLOAD_ATTEMPTS` is 2. After that many failed remote dispatches, from a dead worker or
a kernel error, `try_offload()` clears the node's offload flag and the node runs on the home
machine from then on. Without the cap a kernel that always fails on workers would bounce
between master and workers forever.

The counters `offloaded_count`, `declined_count` and `redispatched_count` exist so that a
silent local fallback can be observed. `stale_writebacks` counts writebacks the version
fence rejected.

### Liveness, two axes

The monitor thread declares a silent worker dead after `liveness_timeout_ms` (5000 by
default) and re-injects its in-flight tasks, so the master never stalls on a lost peer. The
timeout is atomic because the public setter can race the running monitor thread.

The per-task deadline (`task_deadline_ms`, 30000 by default, 0 disables it) is a second
liveness axis, independent of heartbeats. A worker can keep heartbeating while a single lost
TASK_DISPATCH or TASK_RESULT leaves one task pending forever. A task past its deadline is
treated as failed: the worker's inflight count drops and the node is re-injected.

Any inbound message proves the peer is alive, so `handle_message()` refreshes `last_seen`
before it dispatches on the message type. A worker busy pulling buffers is therefore never
reaped by mistake.

The monitor polls at half the tightest of the two deadlines, with a floor of 50 ms, which
bounds detection latency without spinning. Its wait has no predicate, so the notify sent by
`set_liveness_timeout()` or `set_task_deadline()` wakes it to recompute the period instead
of sleeping out an interval sized for the old value. Each pass first sends RTT pings, which
refresh the `rtt_micros` and `free_mem` the policy reads, then reaps. Dead workers and
overdue tasks hand back their nodes the same way: through `on_resubmit` when it is wired, or
through `on_complete(node, false)` so that the graph at least does not stall.

A dead worker leaves the common kernel set and has its load reset, so the policy stops
choosing it at once.

### Messages

A HELLO with a different `PROTOCOL_VERSION` is rejected. The peer's raw structs would
otherwise be parsed against an incompatible layout.

RTT is computed on the master clock alone. The worker echoes the steady-clock token the
master stamped into the PING, so `now - token` is the round trip, free of clock skew.

A TASK_RESULT whose task id is no longer in `pending` belongs to a task the master already
re-dispatched: the worker was slow, not dead. Both its completion and its superseded output
bytes are dropped. For a live result, the inline output bytes are copied into the master's
authoritative buffers before `on_complete` releases successors, so a dependent reader sees
the produced data.

A BUFFER_PUSH from a worker is an eager writeback of an output into the master's
authoritative buffer.

### Version fence

Each dispatch and re-dispatch bumps the version of the task's output buffers; the version is
a fencing token in Kleppmann's sense. A writeback, whether a BUFFER_PUSH or the inline
outputs of a TASK_RESULT, that carries a version older than the buffer's current one belongs
to a superseded dispatch, for example from a worker that was declared dead and was only
slow. Accepting it would overwrite a newer result, so it is dropped and counted.

`try_offload()` bumps the output versions only after the policy has chosen a worker. A
declined task, which runs locally, therefore never bumps a version without cause.

Input handle versions are refreshed from the registry at dispatch. The master is
authoritative, so a buffer written since `compile()` carries a newer version, and that is
what tells a worker its cached copy is stale.

A double-buffered node carries no baked handles. `try_offload()` resolves them from the
node's live pointer lists for the step, and a pointer that is not registered, or no longer
registered, makes the task fall back to local execution.

The scheduler calls `try_offload()` only for a node flagged offloadable, so the node's
offload state already exists and `offload()` returns it without allocating.

## in_process_transport.cpp

The master is node id -1 and workers are 0..N-1.

`MAX_HELD` is 100000. It bounds the messages parked by `FaultAction::Hold`, so a test
nemesis that holds aggressively cannot grow memory without bound. Past the cap a held
message is dropped, as a link's finite buffer would overflow, and the recovery paths treat
it as an ordinary loss.

A `Held` entry stores the destination endpoint already resolved, so `flush_held()` delivers
it verbatim whatever the routing state has become since.

`flush_held()` delivers outside the fabric mutex. `enqueue()` touches only the destination
endpoint's own queue, and the fabric mutex must not be held while a peer's handler may call
back into `send()`. A non-zero seed shuffles the backlog first, which adds message
reordering on top of the delay.

A worker's message to the master carries the worker's node id, so the receiver learns which
worker sent it.

## tcp_transport.cpp

`set_recv_timeout()` exists because a peer that connects and then never sends, or sends a
partial frame, would park `read_n` forever and hold a worker slot: a connection-slot denial
of service. With the timeout an idle `recv` fails, the connection is torn down and the slot
is freed. The master applies 30 seconds to every accepted worker.

The frame header's length is trusted only up to `MAX_FRAME_BYTES`. A corrupt peer, or one on
another protocol, can send any 32-bit length, and allocating it blindly would exhaust
memory. The reader drops the connection instead.

A failed write means the peer's connection is gone, reset or half-open. `send_to()` shuts
the socket down so that the reader thread for that peer unblocks and exits, which turns a
wedged link into a clean teardown. The master's liveness monitor then reaps the worker, or
the worker notices the master is gone. The shutdown is idempotent: a later send on the same
dead socket shuts it again.

`stop()` calls `shutdown()` on every socket before it joins the threads. On Linux `close()`
alone does not wake a `recv()` or `accept()` blocked in another thread, and joining the
readers would deadlock. The descriptors are closed after the joins.

A worker retries its connect every 200 ms for up to 300 attempts, about 60 seconds, so it
can be started before the master, which is the usual order in the lab.

## worker_agent.cpp

### Threads and queues

Dispatch jobs run on a dedicated exec thread, not on the transport's pump thread, so a
blocking buffer pull never starves message delivery.

The pending-job queue is capped at `max_jobs` (1024 by default, 0 means unbounded). A master
that floods a worker faster than it drains could otherwise grow its memory without bound.
When the queue is full the dispatch fails fast with `TASK_RESULT{false}`, and the master
re-dispatches it elsewhere or runs it locally.

The message handler runs on the pump thread. A throw from it, from malformed wire input, a
failed SYCL allocation or a hostile blob, would leave the thread body and call
`std::terminate`, taking the whole process down on one bad message. `start()` contains it:
the message is logged and dropped.

### Idempotency

`seen_ids` and `seen_order` hold the last `MAX_SEEN_IDS` (4096) task ids. A duplicated
TASK_DISPATCH, for example a transport retransmit, is dropped, so a kernel runs at most once
per task id. A re-dispatch after a failure carries a fresh task id and is, correctly, not
deduplicated.

### Registration and liveness

`registered` is set when the master acknowledges HELLO. Until then the heartbeat loop keeps
re-sending HELLO, so a registration message lost to packet loss does not strand the worker.
The periodic HEARTBEAT is what lets the master detect a dead or hung worker and re-dispatch
its tasks. A zero heartbeat interval disables heartbeats; tests use it to feign death.

A PING is answered with a PONG that echoes the master's token and carries the worker's
free-memory headroom, so one probe refreshes both RTT and `free_mem` on the master.

### Buffer cache

The cache is keyed by buffer id and pulls from the master on a miss or a stale version.
`resolve_input()` waits at most `pull_timeout` (5000 ms) for the BUFFER_PUSH. If the push
never arrives, because the message was lost or the master died, it returns null and
`execute_dispatch()` fails the task instead of blocking the exec thread forever. Running the
kernel on a null buffer would crash the worker; failing lets the master re-dispatch to a
healthy peer. A non-positive timeout waits forever.

`alloc_cache_buffer()` receives its size from an untrusted peer. It rejects anything above
the frame cap, and it treats a failed allocation as an error: `malloc_shared` can return
null when memory runs out, and a later `memcpy` into the cache entry would then fault. Both
throws are contained by the pump-thread guard in `start()`.

In the BUFFER_PUSH handler a push older than the cached version is ignored, because it would
downgrade the entry. The copy is clamped to the allocated size. `handle.size_bytes`, which
sized the allocation, and `data.size()` are independent wire fields. A hostile or buggy peer
can send `size_bytes=16` with a 1 MiB blob and overflow the USM allocation. The master-side
handlers in `distributed_coordinator.cpp` clamp the same way.

### Running the kernel

The kernel runs through the worker's own `TaskGraph` and scheduler, not inline on queue 0.
The node carries its input and output pointers, so the scheduler routes it to the queue that
owns the USM, which is what makes a multi-device worker correct, and the work takes part in
the worker's normal NUMA-aware execution. A remote kernel has the host-work shape, queue and
dependencies in, event out, so it drops into a host-fallback task node unchanged.

The dependency tracker keys on regions. A remote kernel's buffers are whole allocations, so
each pointer maps to its whole-buffer region. The `void*` lists are kept for the kernel call.

Each output's bytes travel inside the TASK_RESULT, so completion and data reach the master
together. A lost result re-dispatches the whole task; the master is never left "done" with
no data.

### Free-memory report

`free_memory` arrived in version 5 of the Intel device-info extension, so the query is
guarded on that exact feature-test value and the symbol is referenced only where it exists.
A device without the query reports its total capacity as a coarse stand-in. On an exception
the worker reports 0, which the policy reads as "no headroom information".
