# offload_coordination

The module holds the master's side of an offload: the directory of connected workers with their
handshake, liveness and job limits, and the dispatcher that decides whether a node leaves this
machine and sends it. It is an internal module: its headers reach the modules that name it and the
tests, and are never installed. It is built with the distributed layer, under
`SR_ENABLE_DISTRIBUTED`, and every name it declares is in `SushiRuntime::Distributed`.

It depends on `core`, `api`, `task`, `transport`, `wire`, `link_security`, `cluster_load`,
`machine`, `placement`, `buffer_exchange` and `pending_tasks`, and its sources on `logging` and
`region`.

## Public entry points

| Header | Holds |
| --- | --- |
| `worker_directory.hpp` | `WorkerDirectory`, `HandshakeStep`, `HelloReply`, `RemovedPeer`, `WorkerIdentity` |
| `offload_dispatcher.hpp` | `OffloadDispatcher`, `OffloadDispatcherStatistics`, `K_MAXIMUM_OFFLOAD_ATTEMPTS` |
| `task_description.hpp` | `describe_task()` |
| `offload_handles.hpp` | `OffloadHandles` |
| `rule_placement.hpp` | `RulePlacement` |
| `held_nodes.hpp` | `HeldNodes` |

## worker_directory.hpp

### Facts per worker

The directory keeps one `NodeFacts` (name, operating system, labels, devices) per worker name,
copied from the worker's `HELLO` once `accept_proof()` returns `Accepted`; a `HELLO` from a peer
that has not proved the secret replaces nothing. A worker that reconnects under the same name
replaces that entry with the facts of its new `HELLO`. The entry is keyed by name, so it outlives
the peer: `remove_peer()` erases the session and keeps the facts. Entries are replaced in place and
never erased, so a pointer to one stays valid, and the map grows by one entry per distinct name
that ever registered. Two live workers with one name share one entry.

`pick()` sets `WorkerLoad::facts` to that entry for each worker it offers. Every change to an
entry happens under the directory's mutex, which `pick()` also holds across
`OffloadPolicy::pick_worker()`, so the facts cannot change while a policy reads them.

### pick_target

`pick_target(target, operation)` serves a rule instead of the policy. Under the mutex it walks the
alive workers by rising id and takes the first whose facts match the target's machine part
(`machine_matches()` of `placement`), that registered the kernel of `operation`, and on which the
device part picks a device (`pick_device()`). With room, its inflight count rises and the answer
is `Picked` with the worker and the device. When every match is at its `max_jobs` the answer is
`Busy`; with no match, or a closed directory, `Absent`.

## offload_dispatcher.hpp

### prepare

`prepare()` decides each node of a compiled graph and stores an `OffloadHandles` payload
(`offload_handles.hpp`) on a fixed-buffer node.

| Node | `offloadable` | Payload |
| --- | --- | --- |
| op-tagged, an alive worker has the kernel or the winning rule names another machine, every buffer shared | true | input handles, then output handles |
| op-tagged, supported, every buffer shared, two writes name one buffer | false | the handles of the shared buffers it writes |
| any other node that writes a shared buffer | false | the handles of the shared buffers it writes |
| any other node | false | none, and no `OffloadState` is created for it |
| double-buffered (`live_reads` or `live_writes` set) | true when tagged, supported, every live address is shared and no two live writes name one buffer | none |

A dispatch raises the version of each output once. A buffer listed twice among the outputs would
leave with two versions, and the output fence would refuse the chunks of one of them, so such a
node runs locally. The same buffer as an input and as an output is one write and may leave.

`prepare()` takes the rule set the compile resolved the nodes against and keeps it until the
next call; a node's `TaskMetadata::placement_rule` is a position in that set. A node whose rule
names another machine is offloadable with no worker alive yet, so its rule can wait for one.

One WARN per call names how many op-tagged nodes stay local, how many for each of the three
reasons (no live worker for the op, buffer not shared, buffer written twice) and the first
operation id.

### try_offload

The steps run in this order, and no lock is held across two of them.

1. Decline when the node has no `OffloadState`, when its `LateBindings::enabled` is false, or when
   `attempts` has reached `K_MAXIMUM_OFFLOAD_ATTEMPTS`. `offloadable` is not cleared.
2. Registry lock: a fixed-buffer node refreshes the versions of its stored handles, a
   double-buffered node resolves the addresses of its live lists.
3. The decision, a `PlacementDecision`. A node with a winning rule asks `RulePlacement::decide()`;
   any other node asks `WorkerDirectory::pick()` under the directory lock, and its answer becomes
   `RunOnWorker` on device 0 or `RunLocally`.
4. `RunLocally` declines. `WaitForTarget` hands the node to `HeldNodes::hold()` and answers
   true; a hold refused as `Stale` goes back to step 3. `FailRun` hands the node and the error to
   `OffloadSink::fail()` and answers true (see "Fail" below). `RunOnWorker` goes on.
5. `attempts` rises by one.
6. Registry lock: `bump()` on the outputs. The dispatch is encoded, with the decision's device in
   `TaskDispatchMessage::device_index`.
7. Pending lock: `PendingTasks::insert()`, with the rule's `deadline` when the decision carries
   one. From here the dispatcher does not read or write the node.
8. `Transport::send()`. On any status but `Queued` it calls `take()`; with the task back it
   lowers the worker's inflight count and returns false, without it a removal owns the node and
   the answer is true.

A `TaskDispatchMessage` declares what it sends and embeds no type of the engine: the task id,
the operation, a name of at most `K_MAX_DISPATCH_NAME_BYTES` bytes for the worker's log, a
parameter count and at most `K_MAX_DISPATCH_PARAMETERS` parameters, then the input and the
output handles. Both limits are constants of `wire_messages.hpp`, and encode() returns `TooLong`
for a message that breaks one. The dispatcher fills the message from the node: it cuts the name
to the limit and sends the parameters up to the last one that is not zero. The worker builds the
`NodeOptions` its kernel is handed from the same fields.

A frame header carries `K_PROTOCOL_VERSION`, which is 4. check_protocol_version() of
`wire_frame.hpp` turns a header of another version into `ErrorCode::ProtocolMismatch` with both
versions in its message, and that message is the text of the `REJECT` the receiver sends before
it closes the connection.

The addresses, the handles, the `TaskDispatchMessage` and the encoded payload live in one
thread-local scratch per pool thread, so the dispatcher's own code allocates nothing once the
scratch has grown. `PendingTasks::insert()` allocates the entry and its copy of the output handles,
and `Transport::send()` copies the payload into the peer's queue.

### Local completion

`complete_local()` returns after one null check for a node without an `OffloadState`. Otherwise it
sets `attempts` to 0 and bumps what the node wrote: the addresses in `live_writes` for a
double-buffered node, the stored output handles for a fixed-buffer one. A fixed-buffer node whose
payload lists no output takes no lock.

## rule_placement.hpp

`RulePlacement::decide(rule, position, node, now)` places a node by the rule that won for it:

| Case | Decision |
| --- | --- |
| the rule's machine part matches the master's own `NodeFacts` (no machine part matches every machine) | `RunLocally` |
| `pick_target()` answers `Picked` | `RunOnWorker` with that worker, its device and the rule's `deadline` |
| `pick_target()` answers `Busy` | `WaitForTarget` with no end, whatever `if_absent` says: the target is present, and a pinned node stays on its machine |
| absent, `if_absent` is `Local` | `RunLocally` |
| absent, `Wait`, before the bound has passed since the node's first offer | `WaitForTarget` until first offer plus bound |
| absent, `Wait`, the bound has passed | `RunLocally` |
| absent, `Fail` | `FailRun` with `ErrorCode::InvalidState`, naming the rule (its name, else its position), the node and the target |

`InvalidState` and not `InvalidArgument`: the rule passed the checks of `PlacementRules::add()`,
and what fails is the state of the cluster, which lacks the worker. The master's facts are those of
`Runtime::topology()`, copied when the cluster attaches.

A `Wait` bound counts only while the target is absent: a busy target records no first offer.

### Fail

`try_offload()` passes a `FailRun`'s node and error to `OffloadSink::fail()`, which the
coordinator's `bind()` hands to the dispatcher. The scheduler records the error in the run's
`RunOutcome` and completes the node unrun, so the run skips the rest and returns the error. A
dispatcher no sink was bound to logs the error and runs the node on the master.

### Deadline

A rule's `deadline` travels in `RunOnWorker` to `PendingTasks::insert()`, which measures that task
against it in place of the cluster's `task_deadline`. The monitor's period still follows the
cluster's timeouts, so a rule deadline shorter than a quarter of the period is applied up to one
period late.

## held_nodes.hpp

`HeldNodes` owns the nodes a rule holds, for an absent target until its `Wait` bound or for a
busy one until a slot frees, with the end of each wait, and the time of each
waiting node's first offer, so a bound is measured from the first offer however often the node is
offered again. A node leaves through one of three calls, and the caller passes it to
`OffloadSink::resubmit()`, which offers it to `try_offload()` again:

| Call | Caller | When |
| --- | --- | --- |
| `take_all()` | the coordinator's `PROOF` handler, its `TASK_RESULT` handler, every resubmit of taken tasks, the monitor after a reap | a worker registered, freed a slot or left |
| `take_expired()` | the cluster monitor | a wait ended; the next decision is `RunLocally` |
| `drain()` | `DistributedCoordinator::stop()` | after `close()`, which refuses every later hold |

`take_all()` counts its calls in `releases()`. The dispatcher reads the count before it decides
and passes it to `hold()`, which refuses the node as `Stale` when a `take_all()` ran in between,
so a release that comes between a decision and its hold is not lost; the dispatcher decides again.
Every re-offer goes to every held node, which costs one decision per held node per release.

`earliest()` reads an atomic, so the coordinator can wake the monitor for the end of a new wait
without a lock. The dispatcher forgets a node's first offer once its rule places it anywhere but in
a wait.
