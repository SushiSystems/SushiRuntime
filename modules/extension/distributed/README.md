# distributed

The master and worker layer that ships named kernels and buffer references across machines. It is
built only when the CMake option `SR_ENABLE_DISTRIBUTED` is on, which `sr build --distributed`
sets.

How to run a master and a worker is in
[`DISTRIBUTED_GUIDE.md`](../../../docs/guides/DISTRIBUTED_GUIDE.md); this page describes the code.
The tests are described in [`tests/README.md`](tests/README.md). The design is
`docs/agent/2026_10_09_USABILITY_AUDIT/SPEC.md`, phase D.

## Public surface

Headers under `include/SushiRuntime/distributed/`. Everything is in `SushiRuntime::Distributed`.

| Header | Declares |
| --- | --- |
| `distributed.hpp` | Nothing; it includes what a master or a worker program needs |
| `cluster.hpp` | `Cluster`, `ClusterOptions`, the chunk size limits |
| `cluster_statistics.hpp` | `ClusterStatistics`, `WorkerSnapshot` |
| `shared_secret.hpp` | `SharedSecret` |
| `endpoint.hpp` | `Endpoint`, `EndpointParseResult` |
| `message_type.hpp` | `MessageType`, the type byte of a frame |
| `buffer_handle.hpp` | `GlobalBufferID`, `BufferHandle` |
| `buffer_view.hpp` | `BufferView` |
| `transport.hpp` | `Transport`, `TransportEvents`, `FrameSource`, `OutboundFrame`, `PeerID`, `PeerDescription`, `PeerLoss`, `SendStatus`, `TransportStatus`, `TransportResult`, `TransportExposure`, `TransportStatistics` |
| `tcp_transport.hpp` | `TCPTransport`, `TCPMasterOptions`, `TCPWorkerOptions` |
| `in_process_transport.hpp` | `InProcessFabric`, `InProcessTransport`, `FaultPolicy`, `FaultedFrame`, `FaultAction` |
| `offload_policy.hpp` | `OffloadPolicy`, `WorkerLoad`, `LeastLoadedOffloadPolicy` |
| `kernel_registry.hpp` | `KernelRegistry` and its `Kernel` form |
| `worker_agent.hpp` | `WorkerAgent`, `WorkerAgentOptions`, `WorkerEnd`, `WorkerStatistics` |
| `worker_main.hpp` | `run_worker()`, `WorkerOptions`, `WorkerExitCode` |
| `registered_task.hpp` | `add_registered_task()` |

`MessageType`, `GlobalBufferID` and `BufferHandle` are also reachable as `Wire::` names, which the
private codec uses.

`distributed.hpp` includes every header of the table except `in_process_transport.hpp` and
`message_type.hpp`. The `_op` literal is no header of this module: it comes with
`core/operation_id.hpp`, which declares it in `SushiRuntime::Literals`. A worker program includes
`distributed.hpp` alone; a master includes it beside `SushiRuntime/sushiruntime.hpp`.

`KernelRegistry::register_kernel()` has one form, the one that takes
`span<const BufferView>`; the pointer form and its `RemoteKernel` alias are removed, so a generic
lambda registers without a cast.

A `WorkerAgent` seals the registry in its constructor, before it reads the operations it sends in
`PROOF` and before the exec thread exists, and `run_worker()` seals it on entry. From then on
`register_kernel()` stops the process through `SR_VERIFY`, because the exec thread reads the map
without a lock and the master was already told which operations the worker runs. An empty `Kernel`
stops it at any time: both are rules of the call, not conditions of the machine. A `Cluster` only keeps `ClusterOptions::kernels` alive and reads
nothing from it, so attach() does not seal it. A view a kernel receives is valid until the event
the kernel returned completes: the exec thread holds the cache pins across that wait.

## cluster.hpp

### Cluster

Cluster::attach() is the way into the layer. It builds a master on a runtime, binds it to the
runtime's offload sink, starts it and its transport, then installs it as the runtime's
Internal::Graph::OffloadCoordinator and as the allocator's release observer, and returns a handle to its
knobs and counters. Until attach() runs, the runtime is a
single machine: no header of `api` or `execution` names a transport or a share, and a buffer is
shared through the handle attach() returns.

One overload takes an Runtime and the other an Internal::Execution::RuntimeContext. The first forwards
to the second through Runtime::unsafe_context(); the second serves code that drives a bare context.

attach() refuses an empty secret when the transport's exposure() is Network. When the transport
does not start, attach() installs nothing and returns `InvalidState` with the transport's
message; a cluster attached before stays installed and keeps working. That holds when the caller
passed the transport the earlier cluster runs on: the transport answers `AlreadyStarted`, and a
coordinator stops a transport only when its own `start()` call started it.

attach() returns `Result<Cluster>`. A null transport is a broken precondition and stops the
process. It then checks, in this order, that no run is in flight on the runtime
(`ExecutionEngine::run_in_progress()`), that `liveness_timeout` and `handshake_timeout` are at
least 1 ms, that the secret is not empty on a Network transport and that `chunk_bytes` lies
between 4 KiB and 1 MiB less 64. Each refusal is logged and returned as `InvalidState` before
anything is built or stopped: the options come from files and the command line. The master's
session id is drawn next, and a platform with no random source returns `Unsupported`. A liveness or handshake timeout of 0 would count every peer as
expired on the monitor's first pass; `task_deadline` is the one timing where 0 means no bound, and
`Cluster::liveness_timeout()` returns `InvalidState` for a value below 1 ms like attach() does. It then builds the
new master, binds it to `RuntimeContext::offload_sink()` and calls
DistributedCoordinator::start(). Only when that returns `Ok` does it stop and remove the
coordinator the runtime held before, set the release observer and install the new coordinator.
start() starts the transport, then the monitor, and logs
`distributed master listening on 127.0.0.1:7000, protocol v3, authentication off`. The endpoint is
the message of the `TransportResult` the transport's `start()` returned: the bound `address:port`
for TCP, so a bind to port 0 shows the port the OS chose, and `in-process` for a fabric.

The handle is a view. Copies name the same master and destroying one changes nothing. The class
declares no move, so `std::move` of a handle copies it and the source stays usable. attach() is
`[[nodiscard]]`, so a caller that wants no handle says so with a cast to void. The runtime shares
ownership of the master, stops it when the runtime is destroyed and releases it after its engine
has shut down. A handle that outlives its runtime still reads the stopped master's counters.

`alive_workers()` returns `std::size_t` and `wait_for_workers()` takes one, like every other count
on the runtime's surface.

### detach

detach() stops the master, clears the allocator's release observer and removes the master from the
scheduler, in that order. Every graph compiled afterwards runs locally. detach() does nothing when
the runtime has already released the master, which covers a destroyed runtime and a second
attach() on the same runtime.

The scheduler reads the coordinator's address without a lock, so detach() does the last two steps
only when `ExecutionEngine::run_in_progress()` is false. During a run in flight it stops the
master and logs one WARN. Stopping returns every task still out on a worker to the scheduler, which
runs it on this machine, and the stopped master declines every later node of that run. The stopped
master stays installed until a detach() or an attach() after the run, or the runtime's
destruction, removes it. This is how a caller gives up on a cluster that stopped answering while
execute() waits on it.

Once the master is stopped, by detach() or by the runtime, every handle reports no worker:
`alive_workers()` is 0, `supports()` is false for every operation, `wait_for_workers()` returns
false whatever the count, and `statistics()` has both gauges at 0 and an empty worker list. The
counters keep their totals.

### bump_buffer and buffer_handle

Both read the master's buffer registry by USM address. Offload bumps versions itself at dispatch and
at completion; bump_buffer() is for a write the runtime did not see, and for tests that need a
stale version.

### share

A buffer enters the registry through Cluster::share(). It is opt-in: only a shared buffer can
cross to a worker, and the master stays authoritative for it. share(buffer) registers the
allocation of a `Buffer` and share(state) both fields of a `State`; the two fields keep their
handles for the State's lifetime, and the dispatcher follows which one is read at each step from
the live lists of the rule's node. An empty handle is skipped. A handle that owns memory and is
device-resident stops the process, because the master reads the allocation on the host, so the
handle must be Shared or Host. share(address, bytes) registers USM the caller owns.

Each handle form has two overloads to make chaining safe. The lvalue overload returns its
argument. The rvalue overload moves a temporary into the caller, so the allocation survives the
chain: `auto a = cluster.share(rt.buffer<float>(n).value());`

share() is a member of the extension and not of `Buffer`, and the engine's
Internal::Graph::OffloadCoordinator has no share: a runtime without this module names no share.

## transport.hpp

### The transport contract

A transport moves frames of protocol version 3. It checks each frame's 12-byte header and knows
nothing about what a payload means; the handshake runs above it, so both transports exercise the
same session code.

**Peers.** A transport numbers its connections from 1 and never reuses a number. A worker that
reconnects is a new `PeerID` on the master, and each connection to the master is a new `PeerID` on
the worker. There is no fixed id for the master and no peer count on the interface.

**Start and stop.** `start(events)` is called once. A master binds and listens inside it, so a
port that is taken comes back as `BindFailed` with the endpoint and the OS reason in the message.
On `Ok` a master's message is the endpoint it listens on. A worker returns `Ok` with an empty
message and connects on its own thread. A second `start()` returns `AlreadyStarted`.
`stop()` is `noexcept`, blocks until every transport thread has ended and makes no event once it
returns; it closes peers without `on_peer_lost()`. A stopped transport does not start again.

**Events.** For one peer: `on_peer_connected()`, then `on_frame()` in the order the peer sent, then
`on_peer_lost()` exactly once, never two at a time. Events of different peers may overlap. The
payload span is valid during `on_frame()` only. An event may call `send()`, `send_stream()` and
`close_peer()`, also for its own peer; it must not call `start()` or `stop()`. An exception that
leaves `on_frame()` is caught by the transport, which closes that peer with `HandlerFailed` and
keeps the process alive. The transport does not log a loss; the receiver does, because only the
receiver knows whether the peer was a registered worker.

**Sending.** `send()` copies a control frame into the peer's queue and returns; it never waits for
a socket. `send_stream()` hands over a `FrameSource`, which the peer's writer asks for one frame at
a time. The writer sends every queued control frame, then one stream frame, taking the peer's
streams in turn, so a control frame waits behind at most one chunk. Control frames to one peer keep
their order, and so do the frames of one stream; a control frame queued after a stream may arrive
before the stream's frames. A result that must follow its output bytes is therefore the last frame
of the same stream. `QueueFull` means the control queue is at its bound and nothing was queued.

**Closing.** `close_peer()` returns at once. Sends to the peer return `UnknownPeer` from that
point, streams are dropped, queued control frames are written for a bounded time, and
`on_peer_lost()` reports `ClosedLocally`.

Over TCP the writer gets `close_linger` (1 s) to write those frames. A writer that has already
waited that long inside one socket write when `close_peer()` is called has had its time:
`PeerConnection::close()` shuts the socket itself, the reader reports the loss at once and the
queued frames are dropped. A write that has waited for less runs on until it ends or
`write_idle_timeout` passes, and in the second case the loss is reported as `WriteStalled`. The
in-process transport has no such wait: its pump never blocks in a send, `Outbox::close()` drops
the streams, and the next round of the pump reports the loss.

**A worker that cannot connect.** `on_connect_abandoned()` is called once when a worker-side
transport stops trying: the time allowed for the first connection ran out, or the master was lost
and reconnecting is off.

### Exposure

`exposure()` is `Loopback` for an in-process fabric and for a TCP endpoint whose bind address or
master host is a loopback address, and `Network` otherwise. `TCPMasterOptions::bind` defaults to
`127.0.0.1`.

## tcp_transport.hpp

`TCPTransport::make_master(TCPMasterOptions)` and `make_worker(Endpoint, TCPWorkerOptions)` make
the two ends. Nothing is bound or connected until `start()`.

A master runs one accept thread. It polls `accept` every 100 ms, so `stop()` takes up to that
long, and it joins finished connections between two accepts. Every open connection counts
towards `max_peers` (64), whether or not its peer has registered. A connection that arrives at
the cap gets no `PeerID` and makes no event: the acceptor writes `REJECT(busy)` with a 250 ms
bound, shuts the socket and logs a WARN for the first refusal and every tenth after it. The
write is best effort, since the socket is shut straight after it.

A worker runs one connect thread. Each attempt resolves the master's host again and gives each
resolved address `attempt_timeout` (3 s). After a failure it waits `backoff_initial` (100 ms),
doubling up to `backoff_ceiling` (5 s), and logs `cannot reach master` at WARN for the first
attempt and every tenth. `connect_timeout` bounds only the time before the first connection.
After a connection is lost the thread connects again while `reconnect` is set; a connection that
lasted at least `backoff_ceiling` starts the backoff over. The transport does not stop
connecting on its own after a version mismatch or bad magic: `MasterLink` stops it (see "A link
that ended").

Each connection is a `PeerConnection` with a reader thread, a writer thread and an `Outbox`
(4096 control frames by default). A write in which no byte leaves for `write_idle_timeout`
(5 s) ends the connection with `WriteStalled`. `Socket::shutdown()` ends a read or a write that
waits on another thread within 100 ms on every platform, which is what lets `stop()` and
`close()` end a connection whose peer has stopped answering.

`statistics()` sums the byte counts of the open connections and of those that have left, under
the peer table's mutex.

## shared_secret.hpp and the handshake

`SharedSecret` owns the key bytes, wipes them when it dies and does not copy implicitly. A
secret holds 32 bytes to 1 MiB, read from a file byte for byte, or nothing. `from_environment()`
reads the file a variable names. No stream operator prints one.

The handshake runs above the transport, in `WorkerDirectory` on the master and `MasterLink` on
the worker, so the in-process fabric exercises the same code as TCP.

| Step | Frame | Carries |
| --- | --- | --- |
| 1 | `HELLO`, worker to master | a 32-byte worker nonce, the build id, the node name |
| 2 | `CHALLENGE`, master to worker | a 32-byte master nonce, the master's proof, the session id |
| 3 | `PROOF`, worker to master | the worker's proof, its operation ids, `max_jobs`, its cache budget |
| 4 | `WELCOME`, master to worker | the worker id, the liveness timeout, the ping interval, `chunk_bytes` |

A proof is `HMAC-SHA256(secret, label || worker_nonce || master_nonce)`, where the label is
`SRDP2 master` or `SRDP2 worker`, so one side's proof cannot be replayed as the other's
(`handshake.hpp`). Proofs are compared in constant time. The worker verifies the master's proof
before it sends its own, so a worker with another secret closes the connection without `PROOF`:
the master counts nothing for it and logs only that the peer closed before it registered. An
empty secret runs the same exchange under an empty key.

Until `WELCOME` a peer is not a worker. `WorkerDirectory::admits()` passes `HELLO` and `GOODBYE`
from any connected peer, `PROOF` from a peer that owes one, and every other type from a
registered worker only. A peer that has not registered within `handshake_timeout` of connecting
is closed by the monitor; a repeated `HELLO` restarts the handshake and not that clock.

SHA-256 and HMAC are written in the module (`sha256.hpp`, `hmac_sha256.hpp`) and pinned by the
FIPS 180-4 and RFC 4231 vectors in `tests/test_handshake.cpp`. Nonces come from
`std::random_device`. Frames after the handshake carry no MAC and are not encrypted.

## offload_policy.hpp

`WorkerDirectory::pick()` offers the policy the registered, alive workers that are below their
`max_jobs` and registered the kernel of the task's operation, under the directory's mutex. It
does not call the policy when no such worker has room, and an answer that names a worker it did
not offer counts as no pick. Workers of one cluster may therefore hold different kernels.

`LeastLoadedOffloadPolicy` is the default. It returns nothing, which runs the task locally, when
the task's inputs total fewer bytes than `min_offload_bytes` (64 KiB unless the constructor is
given another value). It skips a worker whose `free_mem` is not 0 and is below the input size.
Among the rest it picks the lowest cost, in microseconds: `rtt_micros`, plus the input bytes
divided by 1000, plus 1000 per inflight task. The two constants are in
`source/offload_policy.cpp`.

## cluster_statistics.hpp

`Cluster::statistics()` returns `ClusterStatistics`, which the coordinator assembles from the
brick that causes each count. What each field means to a user is in §9 of the guide.

| Fields | Kept by |
| --- | --- |
| `offloaded`, `declined`, `send_queue_full` | `OffloadDispatcher` |
| `tasks_reaped_deadline`, `tasks_reaped_unacknowledged`, `tasks_reaped_lost_result`, part of `redispatched` | `ClusterMonitor` |
| `chunks_sent`, `chunks_received`, `transfers_aborted`, part of `stale_writebacks` | `BufferExchange` |
| `completed_remote`, `handshake_rejected_auth`, `handshake_rejected_version`, `peers_lost`, `peers_closed_malformed`, the rest of `redispatched` and `stale_writebacks` | `DistributedCoordinator` |
| `bytes_sent`, `bytes_received` | the transport |
| `alive_workers`, `workers` | `WorkerDirectory` |
| `pending_tasks` | `PendingTasks` |

## in_process_transport.hpp

One `InProcessFabric` joins one master endpoint and any number of worker endpoints, made with
`InProcessTransport::make_master(fabric)` and `make_worker(fabric)`. The fabric numbers worker
endpoints from 0 in the order they are made; that index, not a `PeerID`, is what
`set_partitioned()` and a `FaultPolicy` name, because it stays the same across reconnects.

A `FaultPolicy` sees every frame as it enters the fabric, control or stream, as a `FaultedFrame`:
direction, worker index, type, flags and payload. A partition discards frames in both directions
and tells neither side, which is a half-open link; the connection ends only when the master's
liveness check closes the peer.

## Bricks

Private headers under `source/`. Each class owns at most one mutex, holds it only inside its own
members and calls no other brick, no transport and no sink while holding it, so no lock order
exists to get wrong. The one exception is written on the class: `WorkerDirectory` calls
`OffloadPolicy::pick_worker()` under its mutex, and the policy must not call back.

No brick constructs another. `DistributedCoordinator` builds the master's bricks and
`WorkerAgent` the worker's; `PeerConnection` holds its `Outbox` as a member, since the two have one
lifetime.

### Master

| Header | Class | Owns | Runs on |
| --- | --- | --- | --- |
| `buffer_registry.hpp` | `BufferRegistry` | ids, versions, the only copies of master buffer bytes | caller's thread; `read()` on a peer writer, `write()` on a peer reader |
| `worker_directory.hpp` | `WorkerDirectory` | one session per peer: handshake state, kernels, liveness, inflight count, alive count | caller's thread |
| `pending_tasks.hpp` | `PendingTasks` | the task table and every offloaded node reference | caller's thread |
| `buffer_exchange.hpp` | `BufferExchange` | the pull sources it makes, progress of incoming output transfers | reader thread; its sources on a writer thread |
| `offload_dispatcher.hpp` | `OffloadDispatcher` | `prepare`, `try_offload`, local and remote settlement of a node | compiling thread, pool worker |
| `cluster_monitor.hpp` | `ClusterMonitor` | the monitor thread | its own thread |
| `ping_clock.hpp` | `make_ping_token()`, `ping_round_trip_micros()` | the clock a `PING` token is read from and measured on | monitor thread, reader thread |
| `task_resubmission.hpp` | `resubmit_tasks()` | the one sequence that returns a taken task to the scheduler | monitor, reader, the thread in `stop()` |
| `distributed_coordinator.hpp` | `DistributedCoordinator` | the bricks above, the router, start and stop order | every thread of the seam |

### Worker

| Header | Class | Owns | Runs on |
| --- | --- | --- | --- |
| `worker_buffer_cache.hpp` | `WorkerBufferCache` | cached buffers, chunk assembly, the byte budget, pins | exec thread asks, reader thread delivers |
| `dispatch_deduplicator.hpp` | `DispatchDeduplicator` | the task ids seen | reader thread |
| `master_link.hpp` | `MasterLink` | the handshake, the heartbeat thread, the master watch, the final `WorkerEnd` | reader thread and its own thread |
| `dispatch_executor.hpp` | `DispatchExecutor` | the job queue, the exec thread, held task ids | reader thread enqueues, its own thread runs |
| `stop_signal.hpp` | `StopSignal` | the process's stop handlers and their flag | signal handler, main thread |
| `environment.hpp` | `read_environment()` | the one read of an environment variable | caller's thread |
| `worker_options_parser.hpp` | `parse_worker_options()` | the table of flags, variables and defaults | caller's thread |
| `worker_session.hpp` | `serve_worker()`, `exit_code_of()` | one agent on a given transport, from start to exit code | caller's thread |

### Transport and codec

| Header | Class | Owns | Runs on |
| --- | --- | --- | --- |
| `outbox.hpp` | `Outbox` | one peer's control queue and stream list | any thread pushes, one writer pops |
| `peer_connection.hpp` | `PeerConnection` | one socket, its reader thread, writer thread and outbox | its two threads |
| `socket.hpp` | `Socket`, `Listener` | the platform's socket calls | caller's thread |
| `wire_codec.hpp`, `wire_frame.hpp`, `wire_messages.hpp` | field codec, `FrameHeader`, the sixteen messages | bytes only | caller's thread |
| `message_router.hpp` | `MessageRouter` | one handler per message type | reader thread |
| `handshake.hpp`, `sha256.hpp`, `hmac_sha256.hpp` | nonce and proofs | pure functions | caller's thread |
| `offload_handles.hpp` | `OffloadHandles` | the handles baked on a fixed-buffer node | compiling thread writes, pool worker reads |

## pending_tasks.hpp

### Who owns an offloaded node

`PendingTasks::insert()` takes the scheduling reference `try_offload()` was handed. From then on
the node leaves through exactly one of `take()`, `take_for_worker()`, `take_overdue()` and
`drain()`, once, and whoever receives the `PendingTask` owns the node and passes it to the sink:
`complete_remote()` after a result, `resubmit()` otherwise. The entry carries the worker and the
output handles as dispatched, so nothing reads the node's lists after dispatch.

Before a resubmit the owner bumps the task's outputs in the registry. `take()` has already made
`expects_output()` false for the task, and a chunk that passed that check a moment earlier either
wrote before the bump, and is overwritten by the local run, or meets the bumped version in
`BufferRegistry::write()` and is refused. `resubmit_tasks()` is the one place this sequence lives.

### A send that fails after insert

`try_offload()` inserts before it sends. When `send()` returns anything but `Queued` it calls
`take(task_id, worker)`. If that returns the task, the dispatcher lowers the worker's inflight
count and returns false, and the pool runs the node. If it returns nothing, a reaper already took
the node and will resubmit it, so `try_offload()` returns true.

### Lost results

A result is lost when a worker's heartbeats stop listing a task it accepted.
`HEARTBEAT.oldest_held_task_id` is the smallest id the worker still holds, or, when it holds none,
the largest id whose result it has handed to its writer plus one. A heartbeat is a control frame
and can overtake the stream that ends with a task's result, and a heartbeat built just before a
dispatch arrived does not list it. One heartbeat is therefore not proof: `take_overdue()` reaps an
accepted task as `LostResult` only after `K_LOST_RESULT_HEARTBEATS` (2) heartbeats in a row
reported an oldest held id above it.

## distributed_coordinator.hpp

### Stop order

1. `PendingTasks::close()` and `WorkerDirectory::close()`: nothing more is dispatched. The
   directory marks every worker not alive, the way a lost peer is marked, and registers nobody
   from here on. A transport's `stop()` reports no loss, so this is what empties the counts.
2. `ClusterMonitor::stop()` joins the monitor.
3. `Transport::stop()` joins every reader, so no handler runs. A coordinator whose `start()` did
   not start the transport skips this step: the transport then belongs to another master.
4. `PendingTasks::drain()` and `resubmit_tasks()`: each node still out goes back to the scheduler,
   which the seam allows until `stop()` returns.

`stop()` is `noexcept`. Nothing in the module rethrows or nests an exception: on this toolchain a
rethrow inside a helper and `std::throw_with_nested` crash at run time.

### Frame handlers

The coordinator's `Impl` is the transport's `TransportEvents` receiver. `on_frame()` asks
`WorkerDirectory::admits()`, refreshes the peer's liveness and hands the frame to a
`Wire::MessageRouter`. Each handler is set once, in the constructor, through one template that
decodes the payload and calls a member; a payload that does not decode closes the peer.

| Frame | Handler does |
| --- | --- |
| `HELLO` | draws a nonce, `accept_hello()`, sends `CHALLENGE` with the session id; from a worker that is already registered it sends `WELCOME` again and changes nothing else |
| `PROOF` | `accept_proof()`; sends `WELCOME` and logs the registration with what `WorkerDirectory::identity()` returns (name, endpoint, the build id of `HELLO`), or sends `REJECT`, counts `handshake_rejected_auth` and closes the peer |
| `TASK_ACCEPTED` | `PendingTasks::acknowledge()` |
| `TASK_RESULT` | `PendingTasks::take()`; nothing more when the task is not pending. Otherwise `sink.complete_remote()` when the status is `Succeeded` and `BufferExchange::finish_task()` says every output arrived whole, `resubmit_tasks()` when not |
| `BUFFER_REQUEST` | `send_stream()` with the source `BufferExchange::serve_pull()` makes |
| `BUFFER_CHUNK` | `PendingTasks::expects_output()`, then `BufferExchange::apply_output_chunk()`; a chunk the first check refuses counts as a stale writeback |
| `BUFFER_ABORT` | forgets the transfer progress of a task that is still pending on that worker |
| `PONG` | `WorkerDirectory::note_pong()` with the round trip measured from the token |
| `HEARTBEAT` | `WorkerDirectory::note_heartbeat()` and `PendingTasks::note_heartbeat()` |
| `GOODBYE` | closes the peer and returns its tasks at once |

A registered worker that sends any other type is closed. The source holds no `switch` and no
mutex; what it changes after construction is `sink`, two lifecycle flags and seven counters.

### A lost WELCOME

The master registers a peer on `PROOF`. A worker whose `WELCOME` is lost stays in its handshake,
answers no `PING` and says `HELLO` again every heartbeat interval. `WorkerDirectory::admits()`
passes that `HELLO`, so it refreshes the peer's liveness, and `accept_hello()` answers
`AlreadyRegistered`: the session, the worker id, the kernels and the inflight count stay as they
are, and the coordinator sends `WELCOME` again.

### Losing a peer

One function, `retire_peer()`, handles every way a peer leaves: `on_peer_lost()`, `GOODBYE`, a
failed proof, a malformed or unexpected frame. It calls `WorkerDirectory::remove_peer()` and, for
a registered worker, `PendingTasks::take_for_worker()` and `resubmit_tasks()`. The directory
forgets a peer once, so the second call for the same peer finds it unknown and does nothing. The
caller logs one line with the worker id, its name, its endpoint, the cause and the number of
tasks returned. A handler that closes a peer calls `close_peer()` first: from then on a dispatch
to that peer fails in `send()` and is undone by `try_offload()`.

### Counters

`statistics()` reads atomics only. `offloaded` is the dispatcher's count of dispatches whose
`TASK_DISPATCH` frame a worker's queue took. `redispatched` is the monitor's count plus the tasks
the handlers and `stop()` resubmitted. `stale_writebacks` is the exchange's count of chunks the
registry refused plus the chunks `expects_output()` refused. `cluster_statistics()` also asks the
transport for its byte counts, and `TCPTransport::statistics()` takes the peer table's mutex for
that.

`completed_remote` counts the tasks whose result was `Succeeded` with every output whole. The
coordinator keeps it, since `Internal::Graph::OffloadStatistics` has no field for it.
`peers_closed_malformed` counts the peers closed for bad magic, a malformed header or a payload
that does not decode.

The handlers raise `completed_remote`, `redispatched` and `peers_lost` before they hand the node
to the sink, so a caller that reads the statistics when its run returns sees them.
`handshake_rejected_version`, `peers_closed_malformed` and the line that reports the peer rise and
log together, for a peer the directory still knows; a peer it has forgotten was counted and logged
when it was dropped.

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
metadata its kernel is handed from the same fields.

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

## cluster_monitor.hpp

### A pass

1. `PING` to every alive worker; the token is `make_ping_token()` of `ping_clock.hpp`.
2. `take_silent()`: each worker listed gets `GOODBYE(liveness)` and `close_peer()`. The
   transport then reports the loss and the coordinator returns the worker's tasks.
3. `take_handshake_overdue()`: each peer listed is closed.
4. `take_overdue()`, then `resubmit_tasks()`. Each cause has its counter. A WARN names the task
   and the cause, at most one per worker per second.

The monitor's mutex guards only its sleep. An exception out of a pass is logged and the next pass
runs; tasks a failed `take_overdue()` left in the monitor's list are resubmitted by that next pass.

### Period

`period()` is the smaller of `ClusterMonitorOptions::period` (250 ms) and a quarter of the
tightest timeout in force (the liveness timeout, or the task deadline when it is not 0), and at
least 1 ms. `set_liveness_timeout()` and `set_task_deadline()` wake the thread, so a shorter
timeout applies from the next pass. `WELCOME` carries the period a worker registered under.

## ping_clock.hpp

`make_ping_token()` reads the steady clock in microseconds, and `ping_round_trip_micros(token)`
returns the time since a token was made, 0 for a token from the future and the largest `u32` for a
longer trip. The monitor makes the token of each `PING`; the coordinator measures the `PONG` that
brings it back. Both are header-only.

## frame_sending.hpp

`send_message(transport, peer, message, scratch)` encodes a wire message into a buffer the caller
reuses and queues it with `Transport::send()`; the three-argument form uses a buffer of its own.
The coordinator, the monitor, `MasterLink` and `DispatchExecutor` send every control frame through
it.

## worker_agent.hpp

### What the agent composes

`WorkerAgent` builds `WorkerBufferCache`, `DispatchDeduplicator`, `MasterLink` and
`DispatchExecutor` over one transport and one `RuntimeContext`, and is the transport's only event
receiver. One constructor takes an `Runtime` and forwards to the other through
`Runtime::unsafe_context()`, as the two `Cluster::attach()` overloads do. Kernels run on the queue
of device 0 of that context. The cache allocates shared USM on the same device through
`RuntimeContext::memory()`, so the context's allocator records every cached buffer. A `cache_bytes` of 0 becomes half of the device's `memory_bytes`; a device that
reports no size gets 1 GiB and one WARN.

Every frame goes to `MasterLink::note_frame()` first and then through a `MessageRouter`. The agent
decodes the payload and calls the brick that owns the message:

| Message | Goes to |
| --- | --- |
| `CHALLENGE`, `WELCOME`, `REJECT` | `MasterLink`, whose `LinkChange` the agent then acts on |
| `GOODBYE`, `PING` | `MasterLink` |
| `TASK_DISPATCH` | `DispatchDeduplicator::mark_seen()`, then `DispatchExecutor::enqueue()` |
| `BUFFER_CHUNK` with purpose `PullReply` | `WorkerBufferCache::accept_chunk()` |
| `BUFFER_ABORT` | `WorkerBufferCache::abort_transfer()` |
| `BUFFER_RELEASE` | `WorkerBufferCache::release()` |

A payload that does not decode closes the connection with one WARN that names the master, the
type, the length and the offset of the field. A dispatch from a connection the link is not
registered on is ignored. On `RegisteredNewSession` the agent clears the cache and the seen ids; on
`Disconnected` it calls `DispatchExecutor::discard_queued()`, which drops the queued jobs and
aborts the input pull the exec thread has open, so that task fails as `InputUnavailable` at once
instead of after `pull_idle_timeout`. The executor sends `TASK_ACCEPTED` itself.

The agent logs `connected to master <address:port>` when a connection opens and the transport's
cause when the transport gives up. `MasterLink` logs registration and loss by peer number.

### Threads

| Thread | Runs |
| --- | --- |
| Transport reader | every `on_` call of the agent, so the routing table and the handlers above |
| Exec (`DispatchExecutor`) | one kernel at a time, input pulls, output pins |
| Heartbeat (`MasterLink`) | `HEARTBEAT`, the handshake restart, the master watch, `Transport::stop()` for a link that ended |
| Transport writer | output streams; an output stream's destructor returns its cache pins here |
| Caller | `start()`, `wait_for_end()`, `stop()`, `statistics()` |

### Stop order

1. The agent's phase becomes `Stopped`, and `on_peer_connected()` closes any connection that opens
   from here on.
2. `DispatchExecutor::stop()`: no more jobs, a `ShuttingDown` result per queued job, the running
   kernel finishes, the exec thread is joined.
3. `MasterLink::say_goodbye()` queues `GOODBYE` and marks the link as leaving.
4. `close_peer()` on the registered connection, then a wait of at most 2 s for its
   `on_peer_lost()`. A transport's `stop()` drops queued control frames, and this is what gets the
   results and the `GOODBYE` onto the wire. The loss and a transport that then gives up are
   expected here; see the next section.
5. `Transport::stop()`: every reader and writer is joined and every kept stream is destroyed,
   which returns the last cache pins.
6. `MasterLink::stop()` joins the heartbeat thread; a `Running` end becomes `Stopped`.
7. One INFO line with the worker's counters. The cache is freed when the agent is destroyed.

A `start()` that fails stops the executor and the link and leaves no thread of the agent. It does
not stop the transport, which did not start for this agent and may be running for another. An
agent that ended
for a reason other than `stop()` still needs `stop()`.

### A loss the worker caused

`MasterLink` holds the rule, once: after `say_goodbye()` or `stop()` the link is leaving, and
`on_lost()` and `on_connect_abandoned()` then log at DEBUG, return `LinkChange::None` and change no
end, `on_connected()` closes the connection, and the heartbeat thread sends nothing. The link is
where it belongs because the link writes those lines and sends the `GOODBYE` that makes the next
loss the worker's own, so every composer of a link gets the rule, and the agent passes each loss
and each abandon to the link without a filter. The agent logs the transport's cause at WARN only
when the link answers `Ended`. A master that goes away while `DispatchExecutor::stop()` still
drains, before `say_goodbye()`, is a real loss and is logged as one.

### A link that ended

A link ends for good as `MasterUnreachable`, `RejectedAuthentication`, `ProtocolMismatch` or
`MasterLost`. `MasterLink` then stops the transport from its heartbeat thread, once, so the
connector makes no further connection. The link holds the rule because `finish()` is the one place
an end becomes final, and because an event cannot call `Transport::stop()`, which joins the
thread the event runs on; the heartbeat thread is no transport thread. A stopped transport reports
no loss, so the link forgets its connection itself. The agent's `stop()` still runs the whole stop
order, and its `Transport::stop()` is then the second call, which does nothing.

### Handshake frames the link ignores

`on_ping()` answers only on the connection the link is registered on. A `PONG` sent during the
handshake would keep a master waiting on a worker that takes no dispatch.

The link remembers the nonces of its last four `HELLO`s. A `CHALLENGE` whose proof matches none of
them ends the link as `RejectedAuthentication` only while every `HELLO` of the connection is still
remembered. Once a fifth `HELLO` has gone out the proof may answer a forgotten one, so the link
logs it at DEBUG and waits for the answer to its next `HELLO`. A worker with the wrong secret that
gets there is closed by the master's handshake timeout and rejected on its next connection.

## worker_main.hpp

`run_worker(argc, argv, kernels)` parses the command line and calls
`run_worker(options, kernels)`, which is the whole worker process: it sets the log level, reads the
secret file, installs `StopSignal`, builds a `RuntimeContext` and a TCP worker transport, and hands
them to `serve_worker()`. An application's `main()` registers its kernels and returns the call.

`serve_worker()` (`source/worker_session.hpp`) takes the context, any worker transport, the
registry, the agent's options and the stop signal. It builds the `WorkerAgent`, calls
`wait_for_end()` every 100 ms until the stop flag is set or the agent ends, stops the agent and
returns the exit code `exit_code_of()` maps the end to. A test drives it with an in-process
transport and `StopSignal::request()`.

### Flags

| Flag | Environment | Default |
| --- | --- | --- |
| `--master host:port` | `SR_MASTER` | required; port 0 is refused |
| `--secret-file path` | `SR_CLUSTER_SECRET_FILE` | none |
| `--name text` | `SR_WORKER_NAME` | the machine's host name, else `worker` |
| `--max-jobs n` | `SR_WORKER_MAX_JOBS` | 1024; from 1 to 4294967295 |
| `--cache-bytes n` | `SR_WORKER_CACHE_BYTES` | 0, half of device memory |
| `--heartbeat-ms n` | `SR_WORKER_HEARTBEAT_MS` | 1000; at least 1 |
| `--pull-idle-timeout-ms n` | `SR_WORKER_PULL_IDLE_TIMEOUT_MS` | 5000; 0 waits without a limit |
| `--connect-timeout-s n` | `SR_WORKER_CONNECT_TIMEOUT_S` | 60; 0 waits without a limit |
| `--no-reconnect` | | reconnect on |
| `--log-level name` | | unchanged; `off`, `error`, `warn`, `info`, `debug` |
| `--version`, `--help` | | print and exit 0 |

A flag beats its variable, which beats the default. A variable set to nothing counts as unset. A
value follows its flag as the next argument or after `=`. `parse_worker_options()` holds this
table once; the usage text of `--help` is generated from it. The parser takes the environment
through an `EnvironmentReader`, and `read_environment()` is the one it gets in a worker process.

The default name is what `host_name()` of the socket seam returns, the name the OS gives the
machine; a machine that reports none is named `worker`.

### Exit codes

| Code | When |
| --- | --- |
| 0 | the stop flag was set and the agent stopped in order; also `--help` and `--version` |
| 1 | a bad or unknown flag or value, a missing master, an unknown log level, a secret file that cannot be read or holds fewer than 32 bytes or more than 1 MiB; one line on the error stream, prefixed `sushiruntime worker: `, says which |
| 2 | the transport gave up before the first registration (`WorkerEnd::MasterUnreachable`) |
| 3 | `REJECT(authentication)`, or a `CHALLENGE` whose proof is wrong |
| 4 | `REJECT(version)`, a frame of another version, or bytes that are not a frame |
| 5 | the master was lost after a registration and `--no-reconnect` was given |
| 70 | an exception reached `run_worker()`, the agent did not start, or the registry is null |
| 130 | a second signal; the handler ends the process without running destructors |

### Signals

`StopSignal` installs one handler for SIGINT, SIGTERM and, where the C runtime defines it,
SIGBREAK, through `std::signal`. The handler sets one lock-free flag and arms itself again; a
signal that finds the flag set calls `std::_Exit(130)`. `request()` sets the same flag from a test.
The flag is one per process, which is why at most one `StopSignal` exists at a time.

## Protocol versions

| Version | Release | What changed |
| --- | --- | --- |
| 1 | v0.3.0 and earlier | Frame `[u32 length][u8 type]` in native byte order, payloads written as raw structs, nine message types. No authentication, and a buffer travelled whole in one frame. Removed with `protocol.hpp` |
| 2 | unreleased | 12-byte little-endian header: magic `SRDP`, version, type, flags, payload length of at most 1 MiB. Sixteen messages with fixed-width fields. A four-message handshake with HMAC-SHA256 proofs in both directions. Buffers travel in chunks, and a task's output chunks precede its result. `TASK_ACCEPTED`, `BUFFER_ABORT`, `BUFFER_RELEASE`, `REJECT` and `GOODBYE` are new |

`Wire::K_PROTOCOL_VERSION` in `source/wire_frame.hpp` holds the current number. Master and worker
must carry the same version: the reader checks it on every frame and there is no negotiation. The
header and the first three fields of REJECT and GOODBYE are frozen for every later version, so two
versions can always refuse each other in words. `test_wire_format.cpp` holds the golden bytes of
every message and fails on a layout change, which is when the number and this table move.
