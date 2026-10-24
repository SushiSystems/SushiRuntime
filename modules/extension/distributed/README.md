# distributed

The master and worker layer that ships named kernels and buffer references across machines. It is
built only when the CMake option `SR_ENABLE_DISTRIBUTED` is on, which `sr build --distributed`
sets.

The module depends on `core`, `api`, `transport` and `cluster_load` through its headers. Its
sources depend on `logging`, `memory`, `device`, `task`, `execution`, `region`, `log`,
`context_access` and the internal modules `wire`, `link_security`, `network_socket`,
`frame_transport`, `buffer_exchange`, `pending_tasks`, `dispatch_deduplication`,
`process_environment` and `offload_coordination`, which hold the bricks the master and the worker
are built from.

How to run a master and a worker is in
[`DISTRIBUTED_GUIDE.md`](../../../docs/guides/DISTRIBUTED_GUIDE.md); this page describes the code.
The tests are described in [the README of its tests](../../../tests/extension/distributed/README.md). The design is
`docs/agent/2026_10_09_USABILITY_AUDIT/SPEC.md`, phase D.

## Public surface

Headers under `include/SushiRuntime/distributed/`. Everything is in `SushiRuntime::Distributed`.
The vocabulary of a link, `Endpoint`, `MessageType`, `BufferHandle` and `Transport`, is the public
module `transport`; the offload policy and the cluster's statistics are the public module
`cluster_load`. Both are installed with this one.

| Header | Declares |
| --- | --- |
| `distributed.hpp` | Nothing; it includes what a master or a worker program needs |
| `cluster.hpp` | `Cluster`, `ClusterOptions`, the chunk size limits |
| `shared_secret.hpp` | `SharedSecret` |
| `buffer_view.hpp` | `BufferView` |
| `tcp_transport.hpp` | `TCPTransport`, `TCPMasterOptions`, `TCPWorkerOptions` |
| `in_process_transport.hpp` | `InProcessFabric`, `InProcessTransport`, `FaultPolicy`, `FaultedFrame`, `FaultAction` |
| `kernel_registry.hpp` | `KernelRegistry` and its `Kernel` form |
| `worker_agent.hpp` | `WorkerAgent`, `WorkerAgentOptions`, `WorkerEnd`, `WorkerStatistics` |
| `worker_main.hpp` | `run_worker()`, `WorkerOptions`, `WorkerExitCode` |
| `worker_options_parser.hpp` | `parse_worker_options()`, `WorkerOptionsResult`, `WorkerOptionsStatus`, `EnvironmentReader` |
| `registered_task.hpp` | `record_registered_task()` |

`MessageType`, `GlobalBufferID` and `BufferHandle` are also reachable as `Wire::` names, which the
codec of `wire` uses.

`distributed.hpp` includes every header of the table except `in_process_transport.hpp` and
`worker_options_parser.hpp`, and the headers of `transport` and `cluster_load` except
`message_type.hpp`. The `_op` literal is no header of this module: it comes with
`core/operation_id.hpp`, which declares it in `SushiRuntime::Literals`. A worker program includes
`distributed.hpp` alone; a master includes it beside `SushiRuntime/sushiruntime.hpp`.

`KernelRegistry::register_kernel()` has one form, the one that takes
`span<const BufferView>`, so a generic
lambda registers without a cast. The last parameter of a kernel is a `const NodeOptions&`: the
name, the operation and the parameters of the task, valid during the call. A worker fills it from
the dispatch it received. The master fills it from the node's metadata through describe_task()
of `offload_coordination/task_description.hpp`, which sends the parameters up to the last one that is not
zero, and `OffloadPolicy::pick_worker()` is handed the same description of the task it places.

`record_registered_task(recorder, kernels, options, inputs, outputs)` records a task whose body
is the kernel registered for `options.operation`. A caller reaches the recorder through
`Graph::extend()` or `Region::extend()`, and the call returns the position of the rule, like
every recording member of `Recorder`. The node keeps its own copy of the kernel, the name, the
parameters and the two view lists, and hands the kernel the options as the caller gave them.

```cpp
graph.extend([&](SushiRuntime::Recorder& recorder)
{
    SushiRuntime::Distributed::record_registered_task(recorder, *kernels, options, inputs, outputs);
});
```

The public headers include only headers of `api`, `core`, `transport`,
`cluster_load` and this module, and no signature in
them names an engine type: `Cluster::attach()` and `WorkerAgent` take a `Runtime&`, and
`record_registered_task()` a `Recorder&` and a `NodeOptions`. The tests of this module, the
offload benchmark and `worker_main.cpp` build a `Runtime` and run a `Graph`.

A `WorkerAgent` seals the registry in its constructor, before it reads the operations it sends in
`PROOF` and before the exec thread exists, and `run_worker()` seals it on entry. From then on
`register_kernel()` stops the process through `SR_VERIFY`, because the exec thread reads the map
without a lock and the master was already told which operations the worker runs. An empty `Kernel`
stops it at any time: both are rules of the call, not conditions of the machine. A `Cluster` only keeps `ClusterOptions::kernels` alive and reads
nothing from it, so attach() does not seal it. A view a kernel receives is valid until the event
the kernel returned completes: the batch that runs the kernel holds the cache pins until its run
returns.

## cluster.hpp

### Cluster

Cluster::attach() is the way into the layer. It builds a master on a runtime, binds it to the
runtime's offload sink, starts it and its transport, then installs it as the runtime's
Internal::Graph::OffloadCoordinator and as the allocator's release observer, and returns a handle to its
knobs and counters. Until attach() runs, the runtime is a
single machine: no header of `api` or `execution` names a transport or a share, and a buffer is
shared through the handle attach() returns.

attach() takes a Runtime. It reaches the context through `Internal::ContextAccess` of the
`context_access` module in `cluster.cpp`, so the header names no engine type.

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
DistributedCoordinator::start(). Only when that returns `TransportStatus::Ok` does it stop and remove the
coordinator the runtime held before, set the release observer and install the new coordinator.
start() starts the transport, then the monitor, and logs
`distributed master listening on 127.0.0.1:7000, protocol v4, authentication off`. The endpoint is
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
| 1 | `HELLO`, worker to master | a 32-byte worker nonce, the build id, the node name, the operating system, the devices (kind, name, memory) and the labels |
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
FIPS 180-4 and RFC 4231 vectors in `tests/extension/distributed/test_shared_secret.cpp`. Nonces come from
`std::random_device`. Frames after the handshake carry no MAC and are not encrypted.

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

The bricks a master and a worker are composed of. A header written with a folder is in the
internal module of that name; the others are private to this module, under `source/`. Each class
owns at most one mutex, holds it only inside its own
members and calls no other brick, no transport and no sink while holding it, so no lock order
exists to get wrong. The one exception is written on the class: `WorkerDirectory` calls
`OffloadPolicy::pick_worker()` under its mutex, and the policy must not call back.

No brick constructs another. `DistributedCoordinator` builds the master's bricks and
`WorkerAgent` the worker's; `PeerConnection` holds its `Outbox` as a member, since the two have one
lifetime.

### Master

| Header | Class | Owns | Runs on |
| --- | --- | --- | --- |
| `buffer_exchange/buffer_registry.hpp` | `BufferRegistry` | ids, versions, the only copies of master buffer bytes | caller's thread; `read()` on a peer writer, `write()` on a peer reader |
| `offload_coordination/worker_directory.hpp` | `WorkerDirectory` | one session per peer: handshake state, kernels, liveness, inflight count, alive count | caller's thread |
| `pending_tasks/pending_tasks.hpp` | `PendingTasks` | the task table and every offloaded node reference | caller's thread |
| `buffer_exchange/buffer_exchange.hpp` | `BufferExchange` | the pull sources it makes, progress of incoming output transfers | reader thread; its sources on a writer thread |
| `offload_coordination/held_nodes.hpp` | `HeldNodes` | the nodes a rule holds for an absent or busy target, and each waiting node's first offer | pool worker, reader thread, monitor thread |
| `offload_coordination/rule_placement.hpp` | `RulePlacement` | the decision for a node that has a winning rule; the master's own facts | pool worker |
| `offload_coordination/offload_dispatcher.hpp` | `OffloadDispatcher` | `prepare`, `try_offload`, local and remote settlement of a node | compiling thread, pool worker |
| `cluster_monitor.hpp` | `ClusterMonitor` | the monitor thread | its own thread |
| `ping_clock.hpp` | `make_ping_token()`, `ping_round_trip_micros()` | the clock a `PING` token is read from and measured on | monitor thread, reader thread |
| `offload_coordination/task_resubmission.hpp` | `resubmit_tasks()`, `reoffer_nodes()` | the one sequence that returns a taken task to the scheduler, and the one that offers held nodes again and forgets the first offer of each node the sink refuses | monitor, reader, the thread in `stop()` |
| `distributed_coordinator.hpp` | `DistributedCoordinator` | the bricks above, the router, start and stop order | every thread of the seam |

### Worker

| Header | Class | Owns | Runs on |
| --- | --- | --- | --- |
| `worker_buffer_cache.hpp` | `WorkerBufferCache` | cached buffers, chunk assembly, the byte budget, pins | exec thread asks, reader thread delivers |
| `dispatch_deduplication/dispatch_deduplicator.hpp` | `DispatchDeduplicator` | the task ids seen | reader thread |
| `master_link.hpp` | `MasterLink` | the handshake, the heartbeat thread, the master watch, the final `WorkerEnd`, the check that every HELLO fact fits 255 bytes | reader thread and its own thread |
| `dispatch_executor.hpp` | `DispatchExecutor` | the job queue, the exec thread, the pins and the device check of each job | reader thread enqueues, its own thread gathers batches |
| `dispatch_job.hpp` | `DispatchJob`, `DispatchEntry` | one queued dispatch; one job of a batch and the run of its kernel | exec thread builds, the runtime's pool runs the kernel |
| `dispatch_recorder.hpp` | `DispatchRecorder` | the graph each batch runs on, the borrowed buffers, the run | exec thread |
| `dispatch_settler.hpp` | `DispatchSettler` | the commit and output stream of a job that ran, every failed `TASK_RESULT`, the task counters | exec thread, reader thread for a refused enqueue |
| `held_tasks.hpp` | `HeldTasks` | the task ids held until their result leaves | exec thread, reader thread, transport writer |
| `process_environment/stop_signal.hpp` | `StopSignal` | the process's stop handlers and their flag | signal handler, main thread |
| `process_environment/environment.hpp` | `read_environment()` | the one read of an environment variable | caller's thread |
| `worker_session.hpp` | `serve_worker()`, `exit_code_of()` | one agent on a given transport, from start to exit code | caller's thread |

### Transport and codec

| Header | Class | Owns | Runs on |
| --- | --- | --- | --- |
| `frame_transport/outbox.hpp` | `Outbox` | one peer's control queue and stream list | any thread pushes, one writer pops |
| `peer_connection.hpp` | `PeerConnection` | one socket, its reader thread, writer thread and outbox | its two threads |
| `network_socket/socket.hpp` | `Socket`, `Listener` | the platform's socket calls | caller's thread |
| `wire/wire_codec.hpp`, `wire/wire_frame.hpp`, `wire/wire_messages.hpp` | field codec, `FrameHeader`, the sixteen messages | bytes only | caller's thread |
| `wire/message_router.hpp` | `MessageRouter` | one handler per message type | reader thread |
| `link_security/handshake.hpp`, `link_security/sha256.hpp`, `link_security/hmac_sha256.hpp` | nonce and proofs | pure functions | caller's thread |
| `offload_coordination/offload_handles.hpp` | `OffloadHandles` | the handles baked on a fixed-buffer node | compiling thread writes, pool worker reads |

## distributed_coordinator.hpp

### Placement

A node no rule matches, or any node when no rule set is in force, is placed by the cluster's
`OffloadPolicy`, as before rules existed. A node with a winning rule is placed by `RulePlacement`
instead: on the worker its target names, on the device the target's device part picks from that
worker's facts, carried in `TASK_DISPATCH::device_index`; on the master when the target names the
master; and by the rule's `if_absent` when no alive worker matches. `prepare()` hands the
dispatcher the rules the compile resolved the nodes against, which `PlanCache::build` passes down,
and each node keeps them in `OffloadState::rules`; `Cluster::attach()` hands the coordinator a copy
of `Runtime::topology()`. A `Wait` for an absent target, and any rule whose target is alive but at
its `max_jobs`, hold the node in `HeldNodes`, without a thread; the node is offered again when a
worker registers, a task result or a resubmit frees a slot, or a `Wait` bound, counted from the
first offer, passes. `Fail` ends the run through `OffloadSink::fail()` with an `InvalidState`
error that names the rule and the target. A rule's `deadline` replaces `task_deadline` for the
tasks it places. The details are in `offload_coordination/README.md`.

### Stop order

1. `PendingTasks::close()`, `HeldNodes::close()` and `WorkerDirectory::close()`: nothing more is
   dispatched or held. The
   directory marks every worker not alive, the way a lost peer is marked, and registers nobody
   from here on. A transport's `stop()` reports no loss, so this is what empties the counts.
2. `ClusterMonitor::stop()` joins the monitor.
3. `Transport::stop()` joins every reader, so no handler runs. A coordinator whose `start()` did
   not start the transport skips this step: the transport then belongs to another master.
4. `PendingTasks::drain()` and `resubmit_tasks()`, then `HeldNodes::drain()` and
   `reoffer_nodes()`: each node still out or held goes back to the scheduler, which the seam allows
   until `stop()` returns. `try_offload()` answers false from step 1, so they run locally.

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
| `PROOF` | `accept_proof()`; sends `WELCOME`, logs the registration with what `WorkerDirectory::identity()` returns (name, endpoint, the build id of `HELLO`) and offers every held node again (`HeldNodes::take_all()`, `reoffer_nodes()`), or sends `REJECT`, counts `handshake_rejected_auth` and closes the peer |
| `TASK_ACCEPTED` | `PendingTasks::acknowledge()` |
| `TASK_RESULT` | `PendingTasks::take()`; nothing more when the task is not pending. Otherwise `sink.complete_remote()` when the status is `Succeeded` and `BufferExchange::finish_task()` says every output arrived whole, `resubmit_tasks()` when not; then every held node is offered again |
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
coordinator keeps it beside the four counters of `CoordinatorStatistics`, its own type: the
engine's seam has no statistics call, and a RunReport's offloaded count comes from the scheduler.
`peers_closed_malformed` counts the peers closed for bad magic, a malformed header or a payload
that does not decode.

The handlers raise `completed_remote`, `redispatched` and `peers_lost` before they hand the node
to the sink, so a caller that reads the statistics when its run returns sees them.
`handshake_rejected_version`, `peers_closed_malformed` and the line that reports the peer rise and
log together, for a peer the directory still knows; a peer it has forgotten was counted and logged
when it was dropped.

## cluster_monitor.hpp

### A pass

1. `PING` to every alive worker; the token is `make_ping_token()` of `ping_clock.hpp`.
2. `take_silent()`: each worker listed gets `GOODBYE(liveness)` and `close_peer()`. The
   transport then reports the loss and the coordinator returns the worker's tasks.
3. `take_handshake_overdue()`: each peer listed is closed.
4. `take_overdue()`, then `resubmit_tasks()`. Each cause has its counter. A WARN names the task
   and the cause, at most one per worker per second.
5. `HeldNodes::take_expired()`, then `reoffer_nodes()`: a node whose `Wait` bound passed is
   offered again, and its rule then runs it on the master. After a reap in step 4 every held node
   is offered again, since the reaped tasks freed slots.

The monitor's mutex guards only its sleep. An exception out of a pass is logged and the next pass
runs; tasks a failed `take_overdue()` left in the monitor's list are resubmitted by that next pass.

### Period

`period()` is the smaller of `ClusterMonitorOptions::period` (250 ms) and a quarter of the
tightest timeout in force (the liveness timeout, or the task deadline when it is not 0), and at
least 1 ms. `set_liveness_timeout()` and `set_task_deadline()` wake the thread, so a shorter
timeout applies from the next pass. The sleep also ends at `HeldNodes::earliest()`, and
`wake_by()` wakes the thread when a node is held whose wait ends before the next pass; the
coordinator calls it after each `try_offload()` that answers true, which costs two atomic loads
when nothing is held. `WELCOME` carries the period a worker registered under.

## ping_clock.hpp

`make_ping_token()` reads the steady clock in microseconds, and `ping_round_trip_micros(token)`
returns the time since a token was made, 0 for a token from the future and the largest `u32` for a
longer trip. The monitor makes the token of each `PING`; the coordinator measures the `PONG` that
brings it back. Both are header-only.

## worker_agent.hpp

### What the agent composes

`WorkerAgent` builds `WorkerBufferCache`, `DispatchDeduplicator`, `MasterLink` and
`DispatchExecutor` over one transport and one `RuntimeContext`, and is the transport's only event
receiver. Its constructor takes a `Runtime` and reaches the context through
`Internal::ContextAccess`, as `Cluster::attach()` does. The agent holds the context's
`shared_ptr`, declared before the bricks, so the queue and the allocator they use are alive
until the last of them is destroyed, whatever happens to the `Runtime`. The agent also takes a
`Graph` and the `Interop` facade of that `Runtime` for the executor. A kernel runs on the queue of
the device its `TASK_DISPATCH` names (`device_index`), as a task of the runtime; the next section
says how. The cache allocates shared USM through the runtime's allocator on device 0, so each
buffer is recorded, measured and tracked for leaks like any other. Each task states the device its
dispatch names, and a stated device that shares the owner's SYCL context wins over device 0 as the
owner. Shared USM is reachable from every device of one SYCL context, so a dispatch for another device
sees the same bytes, moved by the driver. The executor refuses a dispatch whose device index the worker does not
have, and one whose device queue is in another context than device 0's, with a `TASK_RESULT` of
`KernelFailed` and one WARN that names the task and the device; the kernel is not called. A `cache_bytes` of 0 becomes half of the device's `memory_bytes`; a device that
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

### How a batch runs

The exec thread runs dispatches in batches, and each batch is one run of the executor's `Graph`.
It takes the first queued job, pins its outputs (`WorkerBufferCache::pin_output()`), gets each
input from the cache or pulls it from the master, and records one task with
`Recorder::task(access, body, On(device))`. The task's access names a borrowed buffer
(`Interop::borrow()`) over each pinned view, its inputs as reads and its outputs as writes. It then
takes the next queued job the same way, until the queue is empty. A job that reads a buffer a job
of the batch writes is left in the queue and opens the next batch: its input is in no cache until
the writer's run commits it. Jobs that arrive while a pull is open join the batch being built.

`Graph::run()` then runs the batch on the runtime's scheduler. Two tasks that name the same cached
allocation, a writer and a reader of one buffer or two writers of it, run in recording order; the
others run at the same time, on the runtime's worker threads and on the queues of their devices.
The body of a task waits for the events of the tasks before it, because a registered kernel takes
none, calls the kernel, waits for its event and copies each output into memory the task owns. It
returns a completed event, so each kernel in flight holds one worker thread. At most `min(batch
size, worker threads)` kernels are in flight; a batch holds at most `max_jobs` jobs, because the
queue does.

After the run the exec thread settles each job in recording order: one that ran commits its
outputs and sends the copies as its output stream, one that threw or never ran releases its
outputs and sends `KernelFailed`. An output stream sends its copy, so a later task that writes
the same buffer cannot change what an earlier one sends, and the stream holds no cache pin. The
batch drops every pin when the jobs are settled. An output is pinned without a pull, so its memory holds
the cache's older copy or fresh, uninitialised bytes, and the stream sends the whole buffer back,
which the master writes over its own. A registered kernel therefore writes every byte of every
output; a kernel that leaves some bytes as they were must not be recorded as a registered task. Every borrowed buffer names one liveness token
the executor holds for its whole life; the pins, not the token, keep the memory alive across the
run.

A batch ends at the first job it cannot take, so a job that arrives while a batch runs waits for
that run to end. The runtime runs one graph at a time, so a worker that shares its `Runtime` with
another graph sees its batch refused with `InvalidState` while that graph runs; its jobs then fail
as `KernelFailed`.

The exec thread still pulls one input at a time, so `pulling_` names the one pull
`discard_queued()` can abort. Two jobs of one batch that read one buffer version ask the cache one
after the other: the first pulls it, the second finds it.

### Threads

| Thread | Runs |
| --- | --- |
| Transport reader | every `on_` call of the agent, so the routing table and the handlers above |
| Exec (`DispatchExecutor`) | batch gathering, input pulls, output pins, `Graph::run()`, results |
| Runtime worker pool | the task of each dispatch: its kernel and the copy of its outputs |
| Heartbeat (`MasterLink`) | `HEARTBEAT`, the handshake restart, the master watch, `Transport::stop()` for a link that ended |
| Transport writer | output streams |
| Caller | `start()`, `wait_for_end()`, `stop()`, `statistics()` |

### Stop order

1. The agent's phase becomes `Stopped`, and `on_peer_connected()` closes any connection that opens
   from here on.
2. `DispatchExecutor::stop()`: no more jobs, a `ShuttingDown` result per queued job, the running
   batch finishes, the exec thread is joined.
3. `MasterLink::say_goodbye()` queues `GOODBYE` and marks the link as leaving.
4. `close_peer()` on the registered connection, then a wait of at most 2 s for its
   `on_peer_lost()`. A transport's `stop()` drops queued control frames, and this is what gets the
   results and the `GOODBYE` onto the wire. The loss and a transport that then gives up are
   expected here; see the next section.
5. `Transport::stop()`: every reader and writer is joined and every kept stream is destroyed.
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
secret file, installs `StopSignal`, builds a `Runtime` and a TCP worker transport, and hands
them to `serve_worker()`. An application's `main()` registers its kernels and returns the call.

`serve_worker()` (`source/worker_session.hpp`) takes the runtime, any worker transport, the
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
| `--label key=value` | `SR_WORKER_LABELS` | none; the flag repeats, the variable lists pairs separated by `,` |
| `--max-jobs n` | `SR_WORKER_MAX_JOBS` | 1024; from 1 to 4294967295 |
| `--cache-bytes n` | `SR_WORKER_CACHE_BYTES` | 0, half of device memory |
| `--heartbeat-ms n` | `SR_WORKER_HEARTBEAT_MS` | 1000; at least 1 |
| `--pull-idle-timeout-ms n` | `SR_WORKER_PULL_IDLE_TIMEOUT_MS` | 5000; 0 waits without a limit |
| `--connect-timeout-s n` | `SR_WORKER_CONNECT_TIMEOUT_S` | 60; 0 waits without a limit |
| `--no-reconnect` | | reconnect on |
| `--log-level name` | | unchanged; `trace`, `debug`, `info`, `warn`, `error`, `fatal`, `off`; sets every category |
| `--version`, `--help` | | print and exit 0 |

A flag beats its variable, which beats the default. A variable set to nothing counts as unset.
Labels are the exception: the labels of `SR_WORKER_LABELS` come first and each `--label` follows, in
command-line order. A key must not be empty, a label must hold a `=`, a key may appear only once
across the variable and the flags, and a key or a value holds at most 255 bytes; the first label that
breaks a rule makes the command line invalid. `run_worker()` hands the labels to its `Runtime`
through `RuntimeConfig::labels`, and `WorkerAgent` sends the facts of `Runtime::topology()` (system,
devices, labels) in `HELLO` beside its own name. The master keeps these facts per worker name
and hands them to the offload policy in `WorkerLoad::facts`, and to `RulePlacement`, which
matches a placement rule's target against them; they survive a reconnect
(`offload_coordination/README.md`). A
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
| 2 | v0.4.0 | 12-byte little-endian header: magic `SRDP`, version, type, flags, payload length of at most 1 MiB. Sixteen messages with fixed-width fields. A four-message handshake with HMAC-SHA256 proofs in both directions. Buffers travel in chunks, and a task's output chunks precede its result. `TASK_ACCEPTED`, `BUFFER_ABORT`, `BUFFER_RELEASE`, `REJECT` and `GOODBYE` are new |
| 3 | v0.4.0 | The frame and the sixteen messages of version 2. A task's dispatch carries its own operation, name and parameters |
| 4 | unreleased | `HELLO` carries the worker's operating system, devices and labels; `TASK_DISPATCH` carries the index of the device that runs the kernel |

`Wire::K_PROTOCOL_VERSION` in `wire/wire_frame.hpp` holds the current number. Master and worker
must carry the same version: the reader checks it on every frame and there is no negotiation. The
header and the first three fields of REJECT and GOODBYE are frozen for every later version, so two
versions can always refuse each other in words. `tests/extension/wire/test_wire_messages.cpp` holds the golden bytes of
every message and fails on a layout change, which is when the number and this table move.
