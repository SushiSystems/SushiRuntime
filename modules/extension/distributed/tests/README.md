# distributed tests

The tests of the `distributed` module, built into one program, `sr_distributed_tests`, which
`modules/extension/distributed/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_distributed_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_distributed.cpp

The tests of the distributed layer in this file need no hardware. They cover wire-protocol
serialization, the in-process transport switchboard that simulates a cluster on one machine, and the
registries and policy on the master. An integration test,
`modules/extension/distributed/tests/test_distributed_e2e.cpp`, covers end-to-end kernel offload.

### `BufferRegistryVersioning`

Registering the same pointer again raises the version and keeps the existing id. The version is how
a stale handle is detected when a USM address is reused.

### Offload policy tests

The offload policy tests drive `pick_worker()` of `LeastLoadedOffloadPolicy` with synthetic
`WorkerStats`. They validate the cost model (round-trip time, transfer and queue backlog) and its
gates without a live cluster.

### `live_worker`

`live_worker` creates synthetic `WorkerStats` for the policy tests.

### Malformed wire input

The distributed layer sits on an unauthenticated trust boundary. Every deserialize path must survive
garbage without a crash, without reading past the end, and without allocating an amount an attacker
chose. The tests of the `Unit_DistributedProtocolFuzz` suite exercise the surfaces where findings
C1, C3 and H7 of `docs/archive/slop/CODE_AND_ARCHITECTURE_REVIEW.md` were: a blob length prefix that
is oversized or overflows, a truncated stream, and an empty payload.

### `GetBlobRejectsOverflowingLength`

A blob length prefix so large that `pos_ + bytes` wraps around must be rejected by the bounds check,
which compares `bytes > size - pos` and performs no addition. Otherwise the call would yield a span
about 2^64 bytes long. This is the regression behind finding C3.

### `GetBlobRejectsOversizedLength`

A length prefix that is larger than what remains, without overflowing, is still out of bounds and
must fail cleanly.

### `BufferPushSurvivesOverflowingBlobLength`

`BufferPushMsg::deserialize` on a hostile oversized length must not crash and must not attempt an
allocation of 2^64 bytes. It should return an empty payload.

### `BufferPushSurvivesTruncatedStream`

A truncated stream, with a handle and no blob, deserializes to an empty payload and does not read
past the end.

### `TaskResultSurvivesTruncatedOutputs`

A `TaskResultMsg` that claims more outputs than the buffer can hold must stop at the first failed
extraction and must not loop past the end.

### `DeserializeEmptyPayloadIsSafe`

Deserializing an empty payload does nothing and yields empty fields. It never reads out of bounds.

## test_distributed_e2e.cpp

The file runs end-to-end offload on a real SYCL device. A master and one or more workers in the same
process exercise the full chain: `compile()` offload stamping, the dispatch branch, the buffer pull,
the remote kernel, the result writeback, the successor release and the graph completion. An atomic
counter in the kernel proves that the work ran on the worker and not on the local fallback.

### `register_state_step`

`register_state_step` registers the State-step kernel `next[i] = cur[i] + (i % 7)`.

The same rule is given as the fluent local fallback, so an offloaded run must match the closed form
`seed[i] + steps*(i % 7)` whichever physical buffer the double-buffer swap leaves the result in.
`ins[0]` is the current field of the step and `outs[0]` its next field; the runtime re-points both
on every step.

### `evolve_distributed_state`

`evolve_distributed_state` evolves a distributed `State<int>` for `steps` steps on an in-process
cluster with one worker and returns its final field.

It builds the cluster with `Distributed::Cluster::attach` on a fluent runtime (`State::distributed`,
`Graph::add(op, State, ...)`). The per-step rule `next[i] = cur[i] + (i % 7)` is registered
identically on the worker, so the caller can check the offloaded result against a closed form. The
helper throws if no SYCL device is present or the worker fails to register, and the caller turns
that into a clean skip.

### `OffloadVectorAddRoundTrip`

The test wires up the in-process cluster with one worker. It constructs both endpoints first so that
they register on the fabric, then brings the master up before the worker. In the other order the
worker's HELLO would reach a master that is not serving yet.

### `FanOutAcrossTwoWorkers`

Per-worker run counters prove which worker ran which task. A short sleep keeps both tasks in flight
so that the least-loaded policy spreads them.

The test asserts the total and not a per-worker split. Both tasks must have run on workers, but
which worker gets which task is a scheduling race. Requiring exactly one each is flaky: under load
the first task can finish before the second is dispatched, and both then go to the same idle worker.

### `WorkerDeathFallsBackToLocal`

The fake worker advertises `VEC_ADD`, heartbeats until it receives a dispatch, and then goes silent
for good: it never sends `TASK_RESULT`, as a worker that crashed in the middle of a task would.

The key assertion is implicit. The `execute()` call must return and not deadlock, although the
worker the task went to never answered.

### `LostBufferPushTimesOutInsteadOfHanging`

The fault policy drops every buffer the master tries to push to the worker, so the worker's input
pull can never complete.

The graph runs on a side thread, so a regression that hangs for good fails the test by timeout and
does not block the whole suite.

### `FluentDistributedStateParity`

The test checks transparent offload of a double-buffered `State` across even and odd step counts.

A `State` rotates its two allocations every step, so the compiled plan cannot bake its offload
handles. The runtime re-points the live pointer lists on every step and the coordinator resolves
handles at dispatch. An even and an odd number of steps leave the logical result in different
physical buffers, and no other test has that property, so this one is the sharpest check that the
live-IO indirection follows the swap. Both runs must match the closed form `seed[i] + steps*(i %
7)`, and both must have offloaded.

### `ClusterDetachAndDisplacement`

The test attaches three masters to one bare context, each on its own fabric with no worker.
`detach()` must leave `offload()` null, and a second `detach()` on the same handle must do nothing.
A third `attach()` displaces the second master; the second handle's `detach()` must then leave the
third installed, because a displaced handle no longer points at the context.

### `ClusterHandleOutlivesRuntime`

The test destroys the context while two copies of a handle are alive, then calls `detach()` and
reads counters. Under ASan this is the check that a handle never follows a pointer into a destroyed
context and that the master outlives the engine that held its address.

## test_socket.cpp

The file tests `Endpoint` parsing and the private socket seam, `Socket` and `Listener` of
`modules/extension/distributed/source/socket.hpp`. It needs no hardware and no network beyond
loopback. Every listener binds port 0 and the test reads the port back, so two runs never compete
for a port. No test sleeps: each waits inside a socket call with a 10 s limit that loopback never
reaches.

### `connect_pair`

`connect_pair` connects a client to a listener and accepts the other end. The connect finishes
against the listen queue, so one thread can do both steps in that order.

### `ParseTable`

The table holds each accepted form (`host:port`, `[ipv6]:port`, port 0) and one text per refusal
status. An IPv6 address without brackets is refused, because its last group would read as a port.

### `ExactReadAndFullWriteRoundTrip`

The payload is 4 MiB and 3 bytes, more than the OS buffers of a loopback connection hold, so the
write has to wait for the reader and the read has to collect many pieces. A thread writes while the
test thread reads.

### `ShutdownEndsAWaitingRead`

`shutdown()` runs on the test thread while another thread is in `read_exact`, or about to enter it.
Both orders end the read with `Closed`, which is what lets a transport stop its reader thread.

### `WriteToClosedPeerReturnsError`

The regression test of finding DIST-01. The accepted end closes, the client reads until it sees the
end of the stream, then writes until a write fails. On Linux a write to a reset connection raises
SIGPIPE unless the send call suppresses it, and the signal would end the test program; the test
passes only when the write returns `Closed` with the OS error. The write after that must return
`Closed` with no OS error, because the socket refuses to write again once a write has failed.

### `BindInUseNamesPortAndCause`

A second listener binds the port of the first. The result must name the `bind` call, carry the OS
error and its text, and `describe()` must print the port, so the log line of finding DIST-17 can be
built from it.

### `LocalhostResolves`

The test resolves `localhost`, then connects by that name to a listener bound to `127.0.0.1`. Where
the OS lists `::1` first, the connect is refused there and moves on to the next address. On Windows
that refusal takes about two seconds.

### `Ipv6LoopbackConnects`

The test is reported as skipped, with the OS error, on a machine where `::1` cannot be bound.

## test_distributed_jepsen.cpp

The file is a soak test in the style of Jepsen. It runs an offload graph under a seeded random fault
policy, the nemesis, which drops, duplicates, holds, reorders and partitions messages. It asserts
the safety property the offload model reduces to, determinism under faults: whatever the fault
schedule, the authoritative buffers must end byte-identical to a fault-free run (every `y == 2*x`).
It also asserts liveness: `execute()` must return.

The runtime is an offload accelerator with a single authoritative writer, the master. For that model
the determinism check is the practical equivalent of a linearizability check for a database. It
costs far less, and it is exactly the contract the runtime promises.

### `run_one_scenario`

The cluster assembles cleanly before any fault is injected, so the test exercises recovery at task
time and not the loss of a registration.

The nemesis is a per-message fault policy with a seed. The fabric mutex serializes the calls to it,
so one shared random number generator is safe.

The graph runs on a side thread, so a liveness regression fails by timeout and does not block the
whole suite.

The scenario tears the nemesis down and lets every in-flight delivery settle before it reads the
results and frees the memory.

## test_handshake.cpp

The tests in this file need no hardware and no network. They pin the two primitives the protocol v2
handshake is built on, SHA-256 and HMAC-SHA256, to the vectors their standards publish, then the
proofs built from them, then `SharedSecret`. The file includes the module's private headers
`sha256.hpp`, `hmac_sha256.hpp` and `handshake.hpp` by quoted name.

### `Sha256Fips180Vectors`

The expected digests are the literal values of the FIPS 180-4 example messages: the empty string,
"abc", the 448-bit and the 896-bit message, and one million times 'a'. None is computed by the
test. `PiecewiseUpdateMatchesOneShot` feeds the million bytes in pieces of 127, which never line up
with the 64-byte block, and then hashes a second message with the same object.

### `HmacRfc4231Vectors`

Test cases 1, 2, 3, 4, 6 and 7 of RFC 4231. Case 5 truncates the code to 128 bits, which the
handshake never does. Cases 6 and 7 use a 131-byte key, longer than a block, so they cover the path
that hashes the key first.

### `ProofsMatchAnIndependentImplementation`

The two expected proofs come from Python's `hmac` module for the same secret (bytes 0 to 31), the
same nonces (32 times 0x11 for the worker, 32 times 0x22 for the master) and the labels
`SRDP2 master` and `SRDP2 worker`. The test fails when the label, the order of the nonces or the
key changes, which is a change to the wire protocol.

### `ProofsDifferByRole`, `WrongSecretFails`, `FreshNonceDefeatsReplay`

Every nonce in these tests is a fixed value, so each run checks the same bytes. The first shows
that a proof sent back under the other role is refused, the second that a proof made under another
secret or under an empty key is refused, and the third that a recorded proof fails once either
side contributes a new nonce.

### `PlatformNonceSourceDoesNotRepeat`

The one test that reads `std::random_device`. It asks `make_nonce()` twice and expects two different
values, neither of them all zeros. Two honest draws of 256 bits collide with probability 2^-256, so
a failure means the source is broken.

### `ScratchFile`

`ScratchFile` writes a file into the working directory of the test program, which is inside the
build tree, and removes it when the test ends. The `SharedSecret` tests read their secrets from
such files: 31 bytes must be refused with the file name and the length in the message, 32 bytes
must be accepted byte for byte, and a missing file must be refused with its path in the message.

## test_wire_format.cpp

The file pins protocol version 2 byte for byte and needs no hardware. Every expected frame is a
literal byte array typed into the test, never the output of the encoder under test, so a change to
a field's width, order or byte order fails here until the golden bytes and
`K_PROTOCOL_VERSION` change together.

### `GoldenBytes_<Message>`

One test for each of the sixteen messages. It encodes a message with fixed field values and compares
the payload with the literal bytes, then decodes those bytes and compares every field.

### `FrameHeaderGolden`

The 12-byte header is the magic `S R D P`, a 16-bit version, the type byte, the flags byte and a
32-bit payload length, all little-endian. The test also decodes a `BUFFER_CHUNK` header that carries
the last-chunk flag and compares a whole `PING` frame built by `append_frame`.

### `MagicReadsAsOversizedV1Length`

A version 1 peer reads the first four bytes of a frame as a length. The magic reads as 0x50445253,
about 1.35 GB, which is above version 1's 256 MiB cap, so that peer drops the frame.

### `EveryTruncationFailsCleanly`

Each strict prefix of each golden payload is copied into an allocation of exactly its size, so a
read past the end is visible to ASan, and must decode to an error. `BUFFER_CHUNK` is the one
exception: its data runs to the end of the payload, so a cut inside the data is a shorter chunk. The
second loop covers that case at frame level, where the header's payload length exceeds the bytes
that arrived.

### `RejectsOutOfRangeEnumAndBool`

No message of version 2 carries a bool, so the bool check runs on `FieldReader::get_bool` directly.
The enumeration checks replace one byte of a golden payload and expect `OutOfRange` at that byte's
offset. The header checks cover a wrong magic, another version, an unknown type, a reserved flag,
the last-chunk flag on a frame that is not a chunk, and a payload length one byte above 1 MiB.

### `ListCountBoundedByRemainingBytes`

A list count is compared with the bytes that remain before any `reserve`. The test sends a count of
2^32 - 1, a count one above what fits, and a count whose product with the element size wraps 32
bits, and checks that the destination vector's capacity is still 0 afterwards.

### `RejectsTrailingBytes`

A payload with one byte after its last field fails with `TrailingBytes`.

### `StringLengthBoundedAt255`

A string of 256 bytes fails to encode, and `append_frame` cuts the buffer back to the size it had
on entry. A payload that declares a 256-byte string fails to decode.

### `RouterDeliversByType`

`MessageRouter` calls the handler set for a type and reports `Unhandled` for a type with no handler
and for a byte that names no message.

## test_buffer_exchange.cpp

The tests drive `BufferExchange` over a `BufferRegistry` with no socket and no thread. Buffers are
host vectors registered by pointer, each followed by one guard byte that a write past the end would
change. A pull is read the way a peer's writer reads it: one `OutboundFrame`, reused for every
`next()`. Every exchange uses the smallest chunk, 4 KiB. The file includes the module's private
headers `buffer_exchange.hpp`, `buffer_registry.hpp`, `wire_frame.hpp` and `wire_messages.hpp` by
quoted name.

### `ChunkedRoundTrip`

Runs at 0, 1, chunk - 1, chunk, chunk + 1 and 3 chunks + 7 bytes. It pulls a registered buffer,
decodes each frame with `BufferChunkMessage::decode`, checks the prefix fields and the offset, and
compares the reassembled bytes with the source. The same chunks then go back as task-output chunks
into a second buffer, which must end up equal to the source, guard byte included. `finish_task`
answers true once and false the second time, because the first call forgets the task.

### `EveryStreamEndsWithALastChunk`

At each of the six sizes only the final frame carries the last-chunk flag and only it comes back as
`FinalFrame`. A 0-byte buffer is one empty chunk with the flag. The call after the final frame
returns `Finished` and leaves the frame's payload as it was.

### `ForgedVersionFenced`

Pins the second half of the output fence (finding DIST-03). A chunk one version above the buffer's
and one a version below are both `Stale` and change no byte. After an honest first chunk the
registry's version is raised, as a reaper does before it resubmits, and the task's second chunk is
`Stale` as well. `stale_writebacks` counts three.

### `GapDiscardsTransfer`

After piece 0, piece 2 is a `Gap`. Piece 1, which was the expected one, is then a `Gap` too: the
transfer's progress is gone. A transfer that starts again at offset 0 runs to `Completed`.

### `DuplicateChunkDiscardsTransfer`

Piece 1 sent twice is a `Gap` the second time, and piece 2 after it is refused as well.

### `ReleaseDuringStreamAborts`

Pins finding N1. The buffer is unregistered and its allocation freed between two `next()` calls. The
stream's next frame is `BUFFER_ABORT` with reason `Gone`, and it is the last. The registry copied
nothing for it, so an ASan run reports no read of the freed allocation; without ASan the test only
proves the abort. A pull of an id that was never shared is one `BUFFER_ABORT` frame.

### `StaleVersionAbortsThePull`

The buffer's version is raised between two `next()` calls: the stream ends with `BUFFER_ABORT`,
reason `Stale`. A request that names a version the buffer has already left gets that frame alone.

### `MalformedChunkIsRefusedBeforeAnyWrite`

A chunk with the pull-reply purpose, one that declares a size other than the buffer's, one that
runs past the end, and one flagged last before the end are each `Malformed`. The buffer keeps its
bytes.

### `ForgottenTaskStartsOver`

After `forget_task` the task's next chunk is a `Gap`. A chunk that continues a transfer from another
connection than the one that started it is a `Gap` as well.

## test_buffer_registry.cpp

The tests cover `BufferRegistry` alone, on host vectors registered by pointer, with one thread. No
test here proves that `unregister_buffer` waits for a copy in progress: that needs a way to stop a
thread inside `read()`, which the registry does not offer.

### `RegisteringASharedAddressKeepsItsHandle`

The first id is 1 and the first version is 1. Registering the same address again returns the same id
and the same version; a second address gets id 2.

### `BumpRaisesTheVersionAndSkipsAGoneId`

`bump` writes the raised version into each handle and leaves a handle whose id is gone as it was.
`bump_addresses` raises the version of a shared address and ignores an address that is not shared.

### `ResolveAndRefreshFollowTheCurrentVersion`

`resolve` returns handles in the order of the addresses and false when one is not shared. `refresh`
rewrites each handle's version and returns false once an id has been unregistered.

### `ReadAtAnOffsetCopiesExactlyTheChunk`

A 16-byte read at offset 10 lands in the middle of a guarded destination; the guard bytes on both
sides stay. A read that ends on the buffer's last byte succeeds. One byte further, an offset beyond
the size, and an offset of 2^64 - 1 are `OutOfRange`; another version is `Stale`; an unknown id is
`Gone`. None of the refused reads changes the destination. A 0-byte read at the end only checks.

### `WriteAtAnOldVersionIsRefused`

After `bump`, a write at the version the task was dispatched with is `Stale`, as is one at a version
the buffer has not reached, and neither changes a byte. A write at the current version lands at its
offset only and leaves the version as it was.

### `UnregisterReturnsTheId`

`unregister_buffer` returns the id of a shared address and an invalid id for an address that was
never shared or was already released. Lookups by address and by id then find nothing, and `read`
and `write` report `Gone`. The same address registered again gets a new id.

## test_pending_tasks.cpp

The file tests `PendingTasks` of `modules/extension/distributed/source/pending_tasks.hpp`, the table
that owns an offloaded node from `insert` until one removal returns it. It needs no hardware, no
transport and no clock: every time is a fixed point passed in, and a node is a bare `Graph::Node`
from `make_sushi` that the table never reads.

### `InsertAfterCloseRefused`, `DrainReturnsEveryNode`

After `close()` an insert returns false and the caller keeps its node; an id already in the table is
refused too. `drain` then hands back each node that was inserted, once, whether or not the task was
acknowledged, and a second `drain` finds nothing. Together they pin findings D1 and D2: a stopping
coordinator takes no new node and returns the ones it holds.

### `TakeIsExactlyOnce`, `ForeignWorkerCannotTake`

`take` returns the node, the worker and the output handles as dispatched, and the same call a second
time returns nothing, as do the three other removals. A worker that does not own the task can
neither take it, acknowledge it nor pass the output fence for it.

### `LateResultLeavesInflightAlone`

The regression test of finding DIST-20. A result for an id that was never dispatched, or for a task
`take_for_worker` already returned, gets nothing back, so the caller has no task to lower a count
for.

### `UnacknowledgedTaskReapedWithDeadlineZero`

Finding D6, the lost dispatch. With a task deadline of 0, a task with no `TASK_ACCEPTED` is reaped
one millisecond after the acknowledgement limit of 5000 ms, with the cause `Unacknowledged`, and not
at the limit itself. The acknowledged task beside it is never reaped, because a deadline of 0 puts
no bound on the kernel. `HeartbeatLeavesAnUnacknowledgedTaskToItsOwnLimit` shows that heartbeats do
not count against a task the worker has not accepted.

### `AcknowledgedTaskBelowOldestHeldReaped`

Finding D6, the lost result. A heartbeat whose oldest held id is above an acknowledged task counts
one miss; the task is reaped as `LostResult` at the second miss in a row. A heartbeat that lists the
task again clears the count, which the test shows with task 2: it misses, is listed, and then needs
two more misses. A task of another worker is untouched.

### `ChunkRestartsTheDeadline`, `TakeForWorkerForgetsItsChunkTime`

The task deadline runs from the later of the dispatch and the worker's last chunk, per worker.
A worker's chunk time is dropped with its tasks, so a task dispatched to it afterwards is measured
from its own dispatch.

### `TwoRemovalsOfOneTaskHaveOneWinner`

In each of 200 rounds one thread calls `take` and another `take_for_worker` for the same task. Both
wait at a `StartGate`, which spins until the two have arrived, so the calls overlap as closely as
two threads can. Whichever order the scheduler picks, exactly one of them gets the node.

## test_worker_directory.cpp

The file tests `WorkerDirectory` of `modules/extension/distributed/source/worker_directory.hpp`. It
needs no hardware and no transport: the test plays the worker's side of the handshake with fixed
nonces and a secret of the bytes 0 to 31, computes proofs with `compute_proof` of `handshake.hpp`,
and passes every time in as a fixed point.

### `RecordingPolicy`

`RecordingPolicy` is the offload policy of the fixture. It records the workers of each call and
returns the first, a fixed id, or nothing, as the test sets it.

### `UnregisteredPeerMessagesDropped`

The regression test of finding DIST-03. The test lists the message types `admits` passes for a peer
at each step: none for an unknown peer, `HELLO` and `GOODBYE` after the connect, those and `PROOF`
after `HELLO`, and every type but the two handshake frames once the proof holds. A `BUFFER_REQUEST`
or `BUFFER_CHUNK` from a peer that has not proved the secret is therefore dropped unread.

### `HandshakeDeadlineClosesPeer`

Finding DIST-04. A peer that has not registered is listed one millisecond after the limit, counted
from its connect, and once. Sending `HELLO` again does not move the limit, and a registered worker
is never listed.

### `WaitForWorkersWakesOnRegistration`

Finding DIST-10. A thread waits for one worker with a limit of 24 hours while the test registers
one, and a second thread waits for two while the test closes the directory. The limit is never
reached, so a registration or a `close()` that wakes nobody hangs the test until the program's
timeout; no outcome depends on which thread runs first.

### `WrongProofIsRejected`

A proof made under another secret, the master's own proof sent back, and a proof made under an
empty key are each refused with `AuthenticationFailed`, and the peer is not a worker afterwards. A
refused proof spends its nonces: the right proof for them is then `OutOfOrder`.

### `SecondHelloRestartsTheHandshake`

A second `HELLO` from a peer that owes `PROOF` gets a proof for the new nonces, and only a `PROOF`
for the new nonces registers it. `HELLO` and `PROOF` from a registered worker are `OutOfOrder` and
change nothing.

### `SilentWorkerIsTakenOnce`

A worker silent for longer than the liveness limit is listed once and stops counting at that
moment: the alive count drops, its operations no longer count toward `supports`, and `pick` offers
only the other worker. The `remove_peer` that follows does not lower the count a second time, and
a late frame does not bring the worker back.

### `PickSkipsWorkersAtTheirJobLimit`

Two workers send limits of 1 and 2 in `PROOF`. The policy sees both, then only the second, then
nobody, and at that point it is not called at all. One `release_inflight` frees one slot. An answer
that names a worker the policy was not offered counts as no pick.

## test_transport_conformance.cpp

The file pins the `ITransport` contract as `i_transport.hpp` writes it and needs no hardware. The
suite `Unit_TransportConformanceTest` is typed over two rigs, so each test runs once on an
`InProcessFabric` and once over TCP on `127.0.0.1` with port 0. The tests wait on recorded events
with a 10 s limit and never on a sleep. `Unit_OutboxTest` covers `Outbox` with no transport.

### `EventRecorder`

`EventRecorder` implements `TransportEvents`, copies every event into a list and lets a test wait
until the list satisfies a condition. A test may set a reaction, which runs inside the event on the
transport's thread, outside the recorder's lock.

### `DeliversInOrderPerPeer`

Each end queues 500 control frames that carry a counter. Each receiver must see the counters in
order, and the byte counters must cover 16 bytes for each frame.

### `PeerLostOnceAfterLastFrame`

The worker queues 100 frames and calls `close_peer()`. The master must record the connection, the
100 frames in order and one `ClosedByPeer` loss, 102 events in all, counted after both transports
have stopped. The worker must record one `ClosedLocally` loss and then one `on_connect_abandoned()`,
since its reconnect is off. A send to the closed peer returns `UnknownPeer` on both ends.

### `NoCallbackAfterStop`

A thread floods the master with frames while the test stops the master. The number of events the
master recorded when `stop()` returned must not grow afterwards, and `stop()` must report no loss.

### `SendFromCallbackAllowed`

The master sends from `on_peer_connected()` and echoes every frame from inside `on_frame()`; the
worker calls `close_peer()` from inside the event of the last echo. Every send must return `Queued`
and the echoes must arrive in order, which fails if a transport holds a lock across an event.

### `SendAfterStopReturnsStopped`

`send()` returns `Stopped` before `start()` and after `stop()`, and `UnknownPeer` in between for an
id that names nobody. `send_stream()` on a stopped transport destroys its source before it returns.
A second `start()` returns `AlreadyStarted`, also after `stop()`, and a second `stop()` does
nothing.

### `ControlFrameOvertakesStream`

A stream of 8 chunks holds its second chunk until the test has queued a control frame. The control
frame must arrive after at most 2 chunks, and the chunks must arrive in order with the last-chunk
flag on the eighth only.

### `GatedSource`

`GatedSource` is the stream of that test. Its wait stands in for a writer that is busy with one
chunk; a production `FrameSource` never waits.

### `ControlComesBeforeStream`

`Outbox::pop()` returns two control frames that were pushed after a stream before the stream's first
frame, destroys the source after its final frame and reports `Timeout` when nothing is queued.

### `StreamsTakeTurns`

Two streams of 3 and 2 frames come out as first, second, first, second, first.

### `QueueFullAtCapacity`

An outbox of capacity 2 returns `QueueFull` for the third control frame and `Queued` again after one
`pop()`. A payload of 1 MiB plus one byte returns `TooLong` whatever the queue holds.

### `CloseKeepsControlAndDropsStreams`

`close()` destroys the queued stream at once and refuses later pushes with `UnknownPeer`. The
control frame queued before it still comes out of `pop()`, and the next `pop()` returns `Closed`.

## test_worker_bricks.cpp

The tests in this file open no socket. A fake transport records every frame a brick sends and every
peer it closes, and either pulls an output stream dry on the calling thread or keeps it until the
test releases it. A fake allocator gives the cache host memory, counts it and can be told to refuse.
The handshake tests use a fixed nonce and a fixed secret. Every wait is on a condition, bounded at
30 s so that a broken brick fails a test. The executor tests build a default `sycl::queue`; their
kernels write through the views and return a finished event. The file includes the private headers
of the four bricks by quoted name.

### `ReleaseEvicts`

`BUFFER_RELEASE` frees an unpinned buffer at once and counts an eviction. A pinned buffer keeps its
memory until its last unpin.

### `BudgetEvictsLeastRecentlyUsedUnpinned`

Three buffers of 100 bytes fill a 300-byte budget and the first is read again. A fourth buffer makes
the cache free exactly the second, which is the one used longest ago.

### `PinnedEntryIsNotEvicted`

With the oldest buffer pinned, the budget evicts the newer unpinned one. With every buffer pinned,
`pin_output` answers `AllocationFailed` and frees nothing.

### `GapInAPulledBufferDiscardsIt`

A chunk at offset 8 after a chunk that ended at 4 ends the transfer: `wait_input` answers `Aborted`
and the memory is returned. The same buffer is then pulled again in three chunks and arrives whole.

### `DuplicateDispatchRunsOnce`

The same dispatch delivered twice through `DispatchDeduplicator` reaches the executor once: one
kernel call, one `TASK_ACCEPTED`, one `TASK_RESULT`. A deduplicator of capacity 2 forgets its oldest
id when a third arrives.

### `OutputAllocationFailureFailsTheTask`

With the allocator refusing, a task with one output ends in a `TASK_RESULT` with
`OutputAllocationFailed`, its kernel never runs and no chunk is sent. The next task, with memory
available, succeeds on the same exec thread.

### `KernelSeesBufferSizes`

The kernel reads `size_bytes` of its input and its output views: 12 and 20. The input comes from a
`BUFFER_REQUEST` the test answers with two chunks, and the output leaves in chunks of 8 bytes.

### `LegacyKernelSignatureStillRuns`

A kernel registered through the deprecated pointer overload runs and its bytes come back in the
output chunk. The deprecation warning is silenced around that one call.

### `KernelThatThrowsFailsTheTaskAndTheWorkerLives`

A kernel that throws produces `KernelFailed` with no outputs and no chunk. The next dispatch runs.

### `OutputStreamEndsWithTheResult`

The transport keeps the stream. Until it is pulled no `TASK_RESULT` exists, the task is still held
for `HEARTBEAT`, and a `BUFFER_RELEASE` cannot free the output. Pulled, the stream gives chunks of
4, 4 and 2 bytes, the last one flagged, then `TASK_RESULT`; its destructor returns the pin.

### `HandshakeChecksTheMasterBeforeProving`

`HELLO` carries the nonce, the name and the build id. No `PROOF` leaves before a `CHALLENGE` whose
proof verifies; the `PROOF` then carries the worker's proof, its operations and its capacity, and
`WELCOME` registers the link.

### `WrongMasterProofEndsTheLink`

A `CHALLENGE` proved with another secret ends the link with `RejectedAuthentication`, sends no
`PROOF` and closes the peer. The next connection is closed without a `HELLO`.

### `IdleHeartbeatCarriesTheNextTaskId`

After task 41 finished and nothing is held, the heartbeat thread sends `oldest_held_task_id` 42.
Before the first task the executor reports 0.

### `SilentMasterIsLost`

`WELCOME` carries a liveness timeout of 40 ms and no frame follows. The link closes the peer, and
the loss that follows reports `Disconnected` and leaves the link running.

### `NewSessionClearsTheCache`

A reconnect to the same `session_id` reports `Registered` and the cache and the seen ids stay. A
reconnect to another session reports `RegisteredNewSession`, on which the test clears both, as the
agent does.
