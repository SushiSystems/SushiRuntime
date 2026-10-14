# offload_coordination

The module holds the master's side of an offload: the directory of connected workers with their
handshake, liveness and job limits, and the dispatcher that decides whether a node leaves this
machine and sends it. It is an internal module: its headers reach the modules that name it and the
tests, and are never installed. It is built with the distributed layer, under
`SR_ENABLE_DISTRIBUTED`, and every name it declares is in `SushiRuntime::Distributed`.

It depends on `core`, `api`, `task`, `transport`, `wire`, `link_security`, `cluster_load`,
`buffer_exchange` and `pending_tasks`, and its sources on `logging` and `region`.

## Public entry points

| Header | Holds |
| --- | --- |
| `worker_directory.hpp` | `WorkerDirectory`, `HandshakeStep`, `HelloReply`, `RemovedPeer`, `WorkerIdentity` |
| `offload_dispatcher.hpp` | `OffloadDispatcher`, `OffloadDispatcherStatistics`, `K_MAXIMUM_OFFLOAD_ATTEMPTS` |
| `task_description.hpp` | `describe_task()` |
| `offload_handles.hpp` | `OffloadHandles` |

## offload_dispatcher.hpp

### prepare

`prepare()` decides each node of a compiled graph and stores an `OffloadHandles` payload
(`offload_handles.hpp`) on a fixed-buffer node.

| Node | `offloadable` | Payload |
| --- | --- | --- |
| op-tagged, an alive worker has the kernel, every buffer shared | true | input handles, then output handles |
| op-tagged, supported, every buffer shared, two writes name one buffer | false | the handles of the shared buffers it writes |
| any other node that writes a shared buffer | false | the handles of the shared buffers it writes |
| any other node | false | none, and no `OffloadState` is created for it |
| double-buffered (`live_reads` or `live_writes` set) | true when tagged, supported, every live address is shared and no two live writes name one buffer | none |

A dispatch raises the version of each output once. A buffer listed twice among the outputs would
leave with two versions, and the output fence would refuse the chunks of one of them, so such a
node runs locally. The same buffer as an input and as an output is one write and may leave.

One WARN per call names how many op-tagged nodes stay local, how many for each of the three
reasons (no live worker for the op, buffer not shared, buffer written twice) and the first
operation id.

### try_offload

The steps run in this order, and no lock is held across two of them.

1. Decline when the node has no `OffloadState`, when its `LateBindings::enabled` is false, or when
   `attempts` has reached `K_MAXIMUM_OFFLOAD_ATTEMPTS`. `offloadable` is not cleared.
2. `attempts` rises by one.
3. Registry lock: a fixed-buffer node refreshes the versions of its stored handles, a
   double-buffered node resolves the addresses of its live lists.
4. Directory lock: `WorkerDirectory::pick()`.
5. Registry lock: `bump()` on the outputs. The dispatch is encoded.
6. Pending lock: `PendingTasks::insert()`. From here the dispatcher does not read or write the
   node.
7. `Transport::send()`. On any status but `Queued` it calls `take()`; with the task back it
   lowers the worker's inflight count and returns false, without it a removal owns the node and
   the answer is true.

A `TaskDispatchMessage` declares what it sends and embeds no type of the engine: the task id,
the operation, a name of at most `K_MAX_DISPATCH_NAME_BYTES` bytes for the worker's log, a
parameter count and at most `K_MAX_DISPATCH_PARAMETERS` parameters, then the input and the
output handles. Both limits are constants of `wire_messages.hpp`, and encode() returns `TooLong`
for a message that breaks one. The dispatcher fills the message from the node: it cuts the name
to the limit and sends the parameters up to the last one that is not zero. The worker builds the
`NodeOptions` its kernel is handed from the same fields.

A frame header carries `K_PROTOCOL_VERSION`, which is 3. check_protocol_version() of
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
