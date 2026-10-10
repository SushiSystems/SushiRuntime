# distributed

The master and worker layer that ships named kernels and buffer references across machines. It is
compiled only under SR_DISTRIBUTED.

**The sources are being moved onto the headers described here, and the module does not build at
this commit.** The wire codec, the handshake, the secret, the endpoint and the socket seam compile
and are tested; `distributed_coordinator.cpp`, `worker_agent.cpp`, `tcp_transport.cpp`,
`in_process_transport.cpp`, `cluster.cpp`, `kernel_registry.cpp`, `buffer_registry.cpp` and the
three older test files still hold the code written against the headers this commit replaced.

The design is `docs/agent/2026_10_09_USABILITY_AUDIT/SPEC.md`, phase D.

## Public surface

Headers under `include/SushiRuntime/distributed/`. Everything is in `SushiRuntime::Distributed`.

| Header | Declares |
| --- | --- |
| `cluster.hpp` | `Cluster`, `ClusterOptions`, the chunk size limits |
| `cluster_statistics.hpp` | `ClusterStatistics`, `WorkerSnapshot` |
| `shared_secret.hpp` | `SharedSecret` |
| `endpoint.hpp` | `Endpoint`, `EndpointParseResult` |
| `message_type.hpp` | `MessageType`, the type byte of a frame |
| `buffer_handle.hpp` | `GlobalBufferID`, `BufferHandle` |
| `buffer_view.hpp` | `BufferView` |
| `i_transport.hpp` | `ITransport`, `TransportEvents`, `FrameSource`, `OutboundFrame`, `PeerId`, `PeerInfo`, `PeerLoss`, `SendStatus`, `TransportStatus`, `TransportResult`, `TransportExposure`, `TransportStatistics` |
| `tcp_transport.hpp` | `TcpTransport`, `TcpMasterOptions`, `TcpWorkerOptions` |
| `in_process_transport.hpp` | `InProcessFabric`, `InProcessTransport`, `FaultPolicy`, `FaultedFrame`, `FaultAction` |
| `i_offload_policy.hpp` | `IOffloadPolicy`, `WorkerStats`, `LeastLoadedOffloadPolicy` |
| `kernel_registry.hpp` | `KernelRegistry` and its `Kernel` form |
| `worker_agent.hpp` | `WorkerAgent`, `WorkerAgentOptions`, `WorkerEnd`, `WorkerStatistics` |
| `worker_main.hpp` | `run_worker()`, `WorkerOptions`, `WorkerExitCode` |
| `registered_task.hpp` | `add_registered_task()` |

`MessageType`, `GlobalBufferID` and `BufferHandle` are also reachable as `Wire::` names, which the
private codec uses.

Deprecated and forwarding: the four-argument `Cluster::attach()` overloads, which attach with an
empty secret, and `KernelRegistry::register_kernel()` for a `RemoteKernel`, which wraps the
pointer-form kernel. The tree compiles with `-Werror`, so a caller of either has to switch or
silence the warning where it calls.

## cluster.hpp

### Cluster

Cluster::attach() is the way into the layer. It builds a master on a runtime, installs it as the
runtime's Graph::OffloadCoordinator and as the allocator's release observer, starts it and its
transport, and returns a handle to its knobs and counters. Until attach() runs, the runtime is a
single machine: no header of `api` or `execution` names a transport, and Buffer::distributed() does
nothing.

One overload takes an API::Runtime and the other an Execution::RuntimeContext. The first forwards
to the second through Runtime::unsafe_context(); the second serves code that drives a bare context.

attach() refuses an empty secret when the transport's exposure() is Network. When the transport
does not start, attach() removes what it installed and throws an exception that carries the
transport's message.

The handle is a view. Copies name the same master and destroying one changes nothing. attach() is
`[[nodiscard]]`, so a caller that wants no handle says so with a cast to void. The runtime shares
ownership of the master, stops it when the runtime is destroyed and releases it after its engine
has shut down. A handle that outlives its runtime still reads the stopped master's counters.

`alive_workers()` returns `std::size_t` and `wait_for_workers()` takes one, like every other count
on the runtime's surface.

### detach

detach() stops the master, clears the allocator's release observer and removes the master from the
scheduler, in that order. Every graph compiled afterwards runs locally. The caller must make sure
no run is in flight, because the scheduler reads the coordinator's address without a lock; the code
cannot check that. detach() does nothing when the runtime has already released the master, which
covers a destroyed runtime and a second attach() on the same runtime.

### bump_buffer and buffer_handle

Both read the master's buffer registry by USM address. Offload bumps versions itself at dispatch and
at completion; bump_buffer() is for a write the runtime did not see, and for tests that need a
stale version. A buffer enters the registry through Graph::OffloadCoordinator::share(), which
Buffer::distributed() and State::distributed() call.

## i_transport.hpp

### The transport contract

A transport moves frames of protocol version 2. It checks each frame's 12-byte header and knows
nothing about what a payload means; the handshake runs above it, so both transports exercise the
same session code.

**Peers.** A transport numbers its connections from 1 and never reuses a number. A worker that
reconnects is a new `PeerId` on the master, and each connection to the master is a new `PeerId` on
the worker. There is no fixed id for the master and no peer count on the interface.

**Start and stop.** `start(events)` is called once. A master binds and listens inside it, so a
port that is taken comes back as `BindFailed` with the endpoint and the OS reason in the message. A
worker returns `Ok` and connects on its own thread. A second `start()` returns `AlreadyStarted`.
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
point, queued control frames are still written, streams are dropped, and `on_peer_lost()` reports
`ClosedLocally`.

**A worker that cannot connect.** `on_connect_abandoned()` is called once when a worker-side
transport stops trying: the time allowed for the first connection ran out, or the master was lost
and reconnecting is off.

### Exposure

`exposure()` is `Loopback` for an in-process fabric and for a TCP endpoint whose bind address or
master host is a loopback address, and `Network` otherwise. `TcpMasterOptions::bind` defaults to
`127.0.0.1`.

## in_process_transport.hpp

One `InProcessFabric` joins one master endpoint and any number of worker endpoints, made with
`InProcessTransport::make_master(fabric)` and `make_worker(fabric)`. The fabric numbers worker
endpoints from 0 in the order they are made; that index, not a `PeerId`, is what
`set_partitioned()` and a `FaultPolicy` name, because it stays the same across reconnects.

A `FaultPolicy` sees every frame as it enters the fabric, control or stream, as a `FaultedFrame`:
direction, worker index, type, flags and payload. A partition discards frames in both directions
and tells neither side, which is a half-open link; the connection ends only when the master's
liveness check closes the peer.

## Bricks

Private headers under `source/`. Each class owns at most one mutex, holds it only inside its own
members and calls no other brick, no transport and no sink while holding it, so no lock order
exists to get wrong. The one exception is written on the class: `WorkerDirectory` calls
`IOffloadPolicy::pick_worker()` under its mutex, and the policy must not call back.

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

1. `PendingTasks::close()` and `WorkerDirectory::close()`: nothing more is dispatched.
2. `ClusterMonitor::stop()` joins the monitor.
3. `ITransport::stop()` joins every reader, so no handler runs.
4. `PendingTasks::drain()` and `resubmit_tasks()`: each node still out goes back to the scheduler,
   which the seam allows until `stop()` returns.

`stop()` is `noexcept`. Nothing in the module rethrows or nests an exception: on this toolchain a
rethrow inside a helper and `std::throw_with_nested` crash at run time.

## Protocol versions

| Version | Frame | State |
| --- | --- | --- |
| 1 | `[u32 length][u8 type]`, raw structs | removed with `protocol.hpp` |
| 2 | 12-byte header: magic `SRDP`, version, type, flags, payload length; 16 messages | current |

Master and worker must carry the same version. The header and the first three fields of REJECT and
GOODBYE are frozen for every later version.
