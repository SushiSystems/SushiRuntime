# distributed tests

The tests of the `distributed` module, built into one program, `sr_distributed_tests`, which
`modules/extension/distributed/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_distributed_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_distributed.cpp

The tests in this file need no hardware. They cover what `test_wire_format.cpp` does not pin about
the codec, the in-process transport in both directions, and the offload policy.
`test_wire_format.cpp` holds the golden bytes, the truncations and the range checks of every
message; `test_buffer_registry.cpp` holds the registry.

### `SeededDispatchesSurviveAWholeFrame`

The golden test pins one dispatch. This one encodes 200 dispatches drawn from seed 20261010 with
`append_frame`, reads the header back and decodes the payload. Every wire field must come back
equal, and `metadata.name` must not, because the name stays off the wire. A parameter is zero one
time in four, so the encoder's cut of trailing zero parameters is exercised with and without a tail.

### `InProcessTransportDeliversBothDirections`

A master endpoint and a worker endpoint each start with a recording `TransportEvents`. The test
waits on the `on_peer_connected` event of both sides, sends one control frame each way and checks
that each side sees the frame under the `PeerID` its own `on_peer_connected` named, with the type,
flags 0 and the payload it was sent.

### Offload policy tests

The policy tests call `pick_worker()` of `LeastLoadedOffloadPolicy` with synthetic `WorkerLoad`
and no cluster. They cover the byte gate, the round-trip and backlog terms of the estimate, the
free-memory gate, and the worker that is not alive.

### `live_worker`, `pick`

`live_worker` builds the `WorkerLoad` of one live worker. `pick` asks a default policy to place a
task of a given size on a list of workers.

### Malformed wire input

A frame's payload is decoded before its sender is trusted with anything else, so a decoder must
refuse garbage without reading past the payload. The `Unit_DistributedProtocolFuzz` suite feeds
seeded damage to the decoders; under the ASan lane an out-of-bounds read fails the run.

### `TaskResultRefusesACountItCannotHold`

A `TASK_RESULT` that claims 1000 outputs and carries none must fail at the count, offset 9, with
`OutOfRange`, and leave the output list empty. `ListCountBoundedByRemainingBytes` of
`test_wire_format.cpp` pins the same rule for `PROOF` and `TASK_DISPATCH`.

### `SeededMutationsDecodeOrFailInsideThePayload`

`every_sample` encodes one well-formed payload per message type. Each is damaged 500 times by
`mutated`: one byte replaced, the tail cut, up to 8 bytes appended, or four bytes in a row set to
0xFF. Every decode must return, and the offset it reports must lie inside the damaged payload.

### `SeededHeadersPassOnlyInsideTheirRanges`

4000 random 12-byte headers are decoded. Three in four carry the magic and version 2 so that the
type, flag and length checks run. A header that passes must name one of the sixteen types, carry a
payload length of at most 1 MiB, and carry the last-chunk flag only on `BUFFER_CHUNK`.

### Cases removed with protocol v1

| Old test | Why it is gone |
| --- | --- |
| `ProtocolRoundTrip` | `GoldenBytes_TaskDispatch` pins the fixed case; the seeded round trip replaces it |
| `BufferPushRoundTrip` | `BufferPushMsg` no longer exists; `GoldenBytes_BufferChunk` pins its successor |
| `BufferRegistryVersioning` | It expected a second `register_buffer` to raise the version, which it no longer does; `test_buffer_registry.cpp` pins the registry |
| `GetBlobRejectsOverflowingLength`, `GetBlobRejectsOversizedLength`, `BufferPushSurvivesOverflowingBlobLength` | No v2 message carries a length-prefixed blob: chunk data runs to the end of the frame |
| `BufferPushSurvivesTruncatedStream`, `DeserializeEmptyPayloadIsSafe` | `EveryTruncationFailsCleanly` decodes every prefix of every message, the empty one included |

## test_distributed_e2e.cpp

The file runs offload end to end on a real SYCL device, with a master and its workers in one
process. Most tests use a real `WorkerAgent` on the in-process fabric; the tests that need a worker
to misbehave use `ScriptedWorker`. Every test skips when SYCL reports no device.

Float buffers hold whole numbers below 2^24, which float addition keeps exact, so `K_TOLERANCE` is
0 and a comparison is an equality.

### `USMFloats`, `LocalWorker`, `BackgroundRun`

`USMFloats` owns one shared USM allocation and frees it. `LocalWorker` owns a worker's
`RuntimeContext` and its `WorkerAgent`. `BackgroundRun` calls `execute()` on its own thread; a
test waits for it at most `K_WAIT` (10 s), so a run that never returns fails that test and the
suite goes on. A run that did not return is detached, not joined.

### `vec_add_kernel`, `add_vec_add`

The kernel takes `span<const BufferView>` and reads the element count from the output's
`size_bytes`. Each registry counts its kernel's runs in an atomic the test owns, which is how a
test tells a remote run from the local fallback. `add_vec_add` adds the op-tagged task with a
fallback lambda that computes the same sum.

### `ScriptedWorker`

A worker endpoint that implements `TransportEvents` itself. It sends `HELLO`, answers `CHALLENGE`
with a `PROOF` under the empty secret, and answers `PING` with `PONG`, so the master counts it
alive. It records each `TASK_DISPATCH` and wakes the test through a condition variable. What it
does next is one of three scripts: `Accept` sends `TASK_ACCEPTED` and holds the task, `Fail` also
sends `TASK_RESULT` with `KernelFailed`, `FallSilent` sends nothing from then on. Its transport
does not reconnect. `send_stream` hands the master a `ScriptedFrames` source, because a frame that
carries the last-chunk flag can only travel in a stream.

### `StubTransport`

An `Transport` that carries nothing: `exposure()` and `start()` return what the test set, and
`start_calls()` counts the starts.

### `eventually`

The master's `stale_writebacks` counter has no event to wait on. `eventually` re-reads a condition,
yielding between reads, until it holds or `K_WAIT` passes. No test sleeps.

### `OffloadVectorAddRoundTrip`, `FanOutAcrossTwoWorkers`, `OffloadOverTCPLoopback`

One task, or two on two workers, goes through the whole chain: `prepare`, dispatch, the buffer
pull, the kernel on the worker, the output chunks and the completion. The fan-out test asserts the
total of the two workers' run counters, not a split: which worker gets which task depends on
timing. The TCP test binds `TCPMasterOptions` defaults (127.0.0.1, port 0) and reads the port from
`local_endpoint()` after `attach`.

### `AutoVersionBumpAcrossLocalWriteThenOffloadedRead`

A local task with no operation writes a shared buffer and an offloaded task reads it. The
buffer's version must rise, and the worker must compute from the values the local task wrote. The
local writer carries an `OffloadState` with `offloadable == false`: it holds the handles the
master bumps when the node completes.

### `WorkerDeathFallsBackToLocal`

A `FallSilent` worker takes the dispatch and stops answering. With a liveness timeout of 300 ms
the master closes it and resubmits the task, which finishes locally. Until the dispatch the worker
answers pings, so it cannot be closed early on a slow machine.

### `StaleWritebackIsFenced`

An `Accept` worker holds the dispatched task. The test first streams an output chunk at the
dispatched version minus one, filled with 999, and waits for `stale_writebacks` to reach 1; the
output must still hold 7. It then streams a chunk at the dispatched version, filled with 42,
followed by a succeeded `TASK_RESULT`. The run must return with 42 in the output, one chunk
received, one refused and nothing resubmitted.

### `LostBufferPushTimesOutInsteadOfHanging`

The fault policy drops every `BUFFER_CHUNK` from master to worker, so no input arrives. With a
pull idle timeout of 200 ms the worker reports `InputUnavailable`; the master resubmits, and after
the attempt cap the task runs locally. The kernel must not have run on the worker.

### `FluentClusterOffload`, `FluentDistributedStateParity`, `evolve_distributed_state`

The fluent surface builds the same offload. A `State` swaps its two allocations every step, so
its handles are resolved at each dispatch; `evolve_distributed_state` runs 4 and 5 steps, which
leave the result in different allocations, and both must match `seed[i] + steps * (i % 7)`.

### `ClusterDetachAndDisplacement`, `ClusterHandleOutlivesRuntime`

`detach()` leaves `offload()` null and a second call does nothing. A later `attach()` displaces
the installed master, and the displaced handle's `detach()` leaves the new one installed. A handle
that outlives its context still answers `alive_workers()` and `offloaded()`.

### `StopResubmitsPendingNodes`

An `Accept` worker holds the only task while `execute()` waits. The test calls `stop()` on the
installed coordinator. The run must return with the local result, `redispatched` must be 1 and
`pending_tasks` 0.

### `TryOffloadAfterStopRunsLocally`

The graph is compiled while a real worker is registered, so its node is offloadable. The
coordinator is stopped before the run. The worker's kernel must not run, `offloaded` stays 0,
`declined` rises and the result is the local one.

### `DynamicStateBumpsTheLiveBufferAfterSwap`

A distributed `State` runs two steps with no worker attached, so both steps complete locally: the
first writes one allocation, the second the other. Both versions must rise, and by the same
amount. A master that bumped the list recorded at compile time would raise one version twice and
leave the other.

### `AttachStartFailureLeavesNothingInstalled`

`attach` on a `StubTransport` whose `start()` returns `BindFailed` must throw
`SushiException` carrying the transport's message, call `start()` once and leave `offload()`
null. A second `attach` on the same context must then succeed.

### `AttemptCapDoesNotOutliveTheRun`

A `Fail` worker fails every dispatch. One compiled graph runs twice, with `rearm()` before each
run. In each run the worker must receive `K_MAXIMUM_OFFLOAD_ATTEMPTS` dispatches before the task
runs locally, so the second run proves the cap of the first did not pin the node. `declined`
rises by one per run and `redispatched` by the cap.

### `RefusesEmptySecretOnNetworkExposure`

On a `StubTransport` that reports `Network`, `attach` with an empty secret must throw before
`start()` is called. With a 32-byte secret the same transport attaches.

### `RegisteredTaskRunsWithAndWithoutAWorker`, `run_registered_sum`

`add_registered_task` adds the vector-add with no lambda. Run with no cluster, the master's own
registry runs the kernel once. Run with a worker, the worker's registry runs it once and the
master's not at all. Both outputs must be equal.

### `AttachOnAStartedTransportLeavesTheFirstMasterServing`

Finding QC-35. One master is attached on an in-process transport, then a second context attaches
on the same transport object. The transport answers `AlreadyStarted`, so the second `attach` must
throw and leave its context without a coordinator. A worker started after the throw must still
register with the first master within 10 s, which it cannot when the failed attach stopped the
transport.

### `DetachedClusterReportsNoWorkers`

Finding QC-36. Two real workers register, and the master supports the vector-add. After
`detach()`, `wait_for_workers(2, 100 ms)` must be false, `alive_workers()` 0 and `supports()`
false, and a copy of the handle must read both gauges as 0 and an empty worker list. The 100 ms
is the limit of a wait that must fail, so the test does not depend on how long it takes.

### `AttachIsRefusedAndDetachDeferredDuringARun`

Finding QC-84. An `Accept` worker holds the only task while `execute()` waits on another thread,
so a run is in flight. `attach` on that context must throw `invalid_state` before it calls
`start()` on the new transport, and the installed coordinator, its worker and its pending task
must be as they were. `detach()` must then stop the master, which returns the task and ends the
run with the local result, and leave the coordinator installed. A second `detach()` after the run
must remove it.

### `MovedFromHandleStillNamesTheMaster`

Finding QC-120. A handle is move-constructed and move-assigned from. The sources must still answer
`alive_workers()` and `offloaded()`. `detach()` through the first source must leave `offload()`
null and close `wait_for_workers(0, 0 ms)` on the other two handles, which shows that all three
name one master.

### `RefusesATimeoutBelowOneMillisecond`

Finding QC-38. `attach` with `liveness_timeout` of 0 and with `handshake_timeout` of 0 must each
throw a message that names the option and its value, before `start()` is called. `attach` with a
`task_deadline` of 0 and both timeouts at 1 ms must succeed. `Cluster::liveness_timeout(0 ms)` must
throw, and 1 ms and `task_deadline(0 ms)` must not.

### `WorkerAgentSealsItsRegistry`

Finding QC-37. A registry is open until a `WorkerAgent` is built with it and sealed from then on,
before `start()`. `register_kernel` must then throw and add nothing.

### The per-run offloaded count

No test of this file reads `RunReport`. That `tasks_offloaded_to_network` counts the dispatches
of one run is pinned by `OffloadedCountIsOfThisRun` in
`modules/surface/api/tests/test_run_driver.cpp`, with a coordinator that only counts.

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

The file is a soak test in the style of Jepsen. It runs an offload graph under a seeded fault
policy and a nemesis thread, which between them drop, repeat, park, reorder and partition frames.
It asserts determinism under faults: whatever the schedule, every output must end equal to a
fault-free run (`out == 2 * in`). It also asserts liveness: `execute()` must return.

The runtime is an offload accelerator with one authoritative writer, the master. For that model
the determinism check stands in for a linearizability check and costs far less.

### `run_one_scenario`

Two workers register before any fault is injected, so the scenario exercises recovery at task
time. The master runs with a liveness timeout of 250 ms and a task deadline of 300 ms, the workers
with a heartbeat of 40 ms and a pull idle timeout of 120 ms.

The graph runs on a `BackgroundRun` with a limit of 30 s. When the limit passes, the scenario
stops the coordinator, which returns every node it holds, and reports the hang.

Before the outputs are read, the nemesis heals the fabric, both workers stop and the cluster
detaches. From then on no thread writes an output.

### `seeded_faults`, `Nemesis`

`seeded_faults` drops 8 frames in 100, repeats 4 and parks 8. The fabric calls a policy from
several threads with no lock held, so the policy locks its own random source. `Nemesis` installs
the policy and runs a thread that flushes parked frames in a shuffled order every 25 ms and, one
pass in eight, cuts one worker's link for 60 ms. The two intervals pace the faults; the thread
waits on a condition variable, so `heal()` ends it at once.

### `RegistrationSurvivesMessageLoss`, `drop_first_of`

`drop_first_of` drops the first frame of each listed type. The test loses the first `HELLO`, the
first `CHALLENGE` and the first `PROOF`. The worker sends `HELLO` again with a fresh nonce after
each heartbeat interval, so it must register; the policy must have dropped exactly three frames,
and the task that follows must run on the worker.

### `RegistrationSurvivesALostWelcome`

The same run with only the first `WELCOME` lost. The master has registered the worker by then and
the worker has not heard it. The test asserts that the task still runs on the worker.

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

No message of version 2 carries a bool, so the bool check runs on `FieldReader::read_bool` directly.
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

### `BumpRaisesTheVersionAndSkipsAGoneID`

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

### `UnregisterReturnsTheID`

`unregister_buffer` returns the id of a shared address and an invalid id for an address that was
never shared or was already released. Lookups by address and by id then find nothing, and `read`
and `write` report `Gone`. The same address registered again gets a new id.

## test_pending_tasks.cpp

The file tests `PendingTasks` of `modules/extension/distributed/source/pending_tasks.hpp`, the table
that owns an offloaded node from `insert` until one removal returns it. It needs no hardware, no
transport and no clock: every time is a fixed point passed in, and a node is a bare `Internal::Graph::Node`
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

### `SnapshotReportsWhatTheWorkerSent`

One registered worker and one peer that only connected. After a `PONG`, a `HEARTBEAT`, a frame
and a pick, the snapshot holds one entry, the worker's, with its id, name, endpoint, an inflight
count of 1 and the round trip, free memory, queue depth and cache size it sent. Its
`ms_since_last_frame` is 600, the time from its last frame to the snapshot.

### `IdentityCarriesTheBuildOfTheLastHello`

`WorkerDirectory::identity()` returns nothing for a stranger and for a peer that still owes
`PROOF`. A peer that said `HELLO` twice and then proved itself has the name, the endpoint and the
build id of the second `HELLO`. A third `HELLO`, sent after registration, changes none of them,
and `remove_peer` ends the identity.

### `HelloFromARegisteredWorkerRegistersNothingTwice`

A registered worker with one task out says `HELLO` again, as a worker does when its `WELCOME` was
lost. The answer is `AlreadyRegistered`. The alive count, the kernels, the inflight count and the
build id stay as they were, and the directory still refuses a `PROOF` from that peer.

### `CloseRetiresEveryWorker`

Finding QC-36. Two workers are registered and `close()` is called. The alive count must be 0, no
operation supported, `list_workers` and `snapshot` empty, and `wait_for_workers` false for a
count of 2 and for a count of 0. A third peer that then sends a valid `HELLO` and `PROOF` gets
`OutOfOrder`. `remove_peer` on a worker still reports it as registered, since the coordinator
counts the loss from that answer, and neither it nor a second `close()` lowers the count again.

## test_transport_conformance.cpp

The file pins the `Transport` contract as `transport.hpp` writes it and needs no hardware. The
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

### `GenericLambdaRegistersAsAKernel`

`register_kernel` has one overload, so a lambda whose four parameters are all `auto` registers
without a cast. It fills its output with 0x5a, and the four bytes come back in one chunk.

### `KernelThatThrowsFailsTheTaskAndTheWorkerLives`

A kernel that throws produces `KernelFailed` with no outputs and no chunk. The next dispatch runs.

### `OutputStreamEndsWithTheResult`

The transport keeps the stream. Until it is pulled no `TASK_RESULT` exists, the task is still held
for `HEARTBEAT`, and a `BUFFER_RELEASE` cannot free the output. Pulled, the stream gives chunks of
4, 4 and 2 bytes, the last one flagged, then `TASK_RESULT`; its destructor returns the pin.

### `DisconnectEndsAnOpenInputPull`

The executor runs with no pull idle timeout, so a pull ends only when something ends it. A task
asks for its input and `discard_queued()` is called before any chunk arrives. The task ends as
`InputUnavailable`, its kernel never runs and the cache holds no byte of the abandoned pull. A
task of the next connection then runs on the same exec thread.

### `HandshakeChecksTheMasterBeforeProving`

`HELLO` carries the nonce, the name and the build id. No `PROOF` leaves before a `CHALLENGE` whose
proof verifies; the `PROOF` then carries the worker's proof, its operations and its capacity, and
`WELCOME` registers the link.

### `WrongMasterProofEndsTheLink`

A `CHALLENGE` proved with another secret ends the link with `RejectedAuthentication`, sends no
`PROOF` and closes the peer. The next connection is closed without a `HELLO`.

### `EndedLinkStopsItsTransport`

A link that ends as `RejectedAuthentication` stops its transport from its heartbeat thread, once,
so the connector opens no further connection. A link that runs stops nothing.

### `PingGetsNoPongBeforeRegistration`

Between a good `CHALLENGE` and its `WELCOME` the link answers no `PING`. After `WELCOME` the same
`PING` gets one `PONG` with its token.

### `LateChallengeForAForgottenHelloIsIgnored`

The link remembers the nonces of its last four `HELLO`s. After a fifth `HELLO`, a `CHALLENGE`
proved against the first nonce reports `LinkChange::None`: no `PROOF` leaves and the peer stays
open, because the proof may answer a `HELLO` the link has forgotten.

### `IdleHeartbeatCarriesTheNextTaskID`

After task 41 finished and nothing is held, the heartbeat thread sends `oldest_held_task_id` 42.
Before the first task the executor reports 0.

### `SilentMasterIsLost`

`WELCOME` carries a liveness timeout of 40 ms and no frame follows. The link closes the peer, and
the loss that follows reports `Disconnected` and leaves the link running.

### `NewSessionClearsTheCache`

A reconnect to the same `session_id` reports `Registered` and the cache and the seen ids stay. A
reconnect to another session reports `RegisteredNewSession`, on which the test clears both, as the
agent does.

### `LossAfterGoodbyeIsTheWorkersOwn`

A registered link says goodbye. The loss that follows and an abandoned connect both report
`LinkChange::None`, the end stays `Running`, and a connection that opens afterwards is closed
without a `HELLO`. `stop()` then ends the link as `Stopped`.

### `SealedRegistryRefusesRegistration`, `EmptyKernelIsRefused`

Finding QC-37. `Unit_KernelRegistryTest` needs no queue: its kernel returns a default event and is
never called. After `seal()`, called twice, `register_kernel` must throw `invalid_state` for a
new operation and for one already registered, and the registry must hold what it held. A
default-constructed `Kernel` must throw `invalid_access` on an open registry and on a sealed one,
and must register nothing.

## test_worker_main.cpp

The parser tests pass an `EnvironmentReader` over a table, so no test changes the environment of
the test process. The five exit-code tests of `Integration_WorkerMainTest` call `serve_worker()` of
`source/worker_session.hpp` over an in-process fabric, through `ServedWorker`, which serves on a
thread of its own with an empty secret and reconnecting off. `FakeMaster` is a `TransportEvents`
on the master endpoint that plays the frames of one script: reject for authentication, reject for
version, register, or register and close. A sixth test runs `run_worker()` itself over TCP
loopback. Each of the six builds a runtime, and each wait for an exit code is bounded at 60 s. No
test raises a signal.

### `MasterAloneGivesTheDefaults`, `EveryFlagIsRead`

`--master` alone leaves every other setting at the default of the flag table. The second test gives
every flag, half as `--flag value` and half as `--flag=value`, and reads each field back.

### `EnvironmentFillsWhatNoFlagGives`, `FlagBeatsEnvironmentBeatsDefault`, `EmptyVariableCountsAsUnset`

With no flag, each of the eight variables sets its field. With `--max-jobs 3` and
`SR_WORKER_MAX_JOBS` set to text that is not a number, the flag wins and the variable is never
parsed. A variable set to nothing leaves the default.

### `HelpListsEveryFlagAndVariable`, `VersionNeedsNoMaster`

`--help` and `--version` are answered without `--master`. The usage text names all twelve flags and
all eight variables.

### `EachRefusalNamesWhatWasWrong`, `NameAbove255BytesIsRefused`

Nineteen command lines the parser refuses: no master, a flag without its value, an endpoint
without a port, port 0, an unbracketed IPv6 address in `SR_MASTER`, an unknown flag, a stray word,
a value given to `--no-reconnect`, and a number that is zero where one is needed, too large,
negative, fractional, empty or not a number. Each message is one line, and a refused value's line
starts with the flag or the variable it came from.

### `ReadsASetVariableAndReportsAnUnsetOne`

`read_environment("PATH")` returns text; a name nobody sets returns nothing.

### `RequestSetsTheFlagAndANewObjectClearsIt`

A `StopSignal` starts clear, `request()` sets the flag, and the next `StopSignal` starts clear
again.

### `UnreadableSecretFileExitsOne`, `ShortSecretFileExitsOne`, `UnknownLogLevelExitsOne`, `BadFlagExitsOne`

Exit code 1. Each test captures the error stream and finds the file name, `31 bytes` and
`at least 32`, the five level names, or the parser's line. All four return before a runtime is
built.

### `VersionPrintsTheProtocolAndExitsZero`

`--version` prints `protocol v2` and the build, and returns 0.

### `SignalStopsWithExitZero`

Exit code 0. The fake master welcomes the worker and waits for its first `HEARTBEAT`, which a
worker sends only once it is registered. The test then calls `StopSignal::request()`, which sets
the flag the first signal of a worker process sets. `serve_worker()` returns `StoppedBySignal`
and the master receives `GOODBYE`.

### `AuthenticationRejectExitsThree`, `VersionRejectExitsFour`

The fake master answers `HELLO` with `REJECT(authentication)` or `REJECT(version)`. The worker
returns 3 or 4 and does not reconnect.

### `MasterNotReachedExitsTwo`

Exit code 2. The fabric has no master endpoint and the worker's connect timeout is 50 ms.

### `MasterLostWithoutReconnectExitsFive`

Exit code 5. The fake master sends `WELCOME` and closes the connection; the worker runs with
reconnecting off.

### `RunWorkerOverTCPReturnsTheCodeOfItsEnd`

The one test of `run_worker(options, kernels)` with a connection. A fake master listens with
`TCPTransport::make_master()` on a port the OS chooses, welcomes the worker and closes. The worker,
on a second thread with reconnecting off, returns 5.

## test_offload_dispatcher.cpp

The file tests `OffloadDispatcher` of `modules/extension/distributed/source/offload_dispatcher.hpp`
over a real `BufferRegistry`, `WorkerDirectory` and `PendingTasks`. The transport is a fake that
records each `send()` and answers with a status the test sets; the policy is a fake that picks the
first worker offered and counts its calls. The one worker registers through `accept_hello` and
`accept_proof` under the empty secret. A buffer is a `std::array<float, 16>` whose address is
shared; no byte of it is read, so the file needs no device and no thread.

### `AttemptCapDeclinesWithoutClearingOffloadable`

With `attempts` at `K_MAXIMUM_OFFLOAD_ATTEMPTS` the dispatcher returns false before it asks the
policy, and `offloadable` and the count stay as they were. `complete_local` sets the count to 0 and
the next `try_offload` dispatches. This pins finding N3: the cap holds for one run, not for good.

### `RefusedSendUndoesThePendingEntry`

The transport answers `QueueFull`. The frame was offered once, the pending table is empty, the
worker's inflight count is 0, `try_offload` returned false and `send_queue_full` and `declined` are
1. The output keeps the version the frame named, one above the version before the call.

### `StoppedCoordinatorDeclines`

`DistributedCoordinator::stop()` closes the pending table, then the directory. After the first
close the policy is asked once, `insert` refuses and the worker's inflight count goes back to 0.
After the second the policy is not asked. No frame reaches the transport in either state and
`offloadable` stays set.

### `DynamicNodeResolvesItsLiveLists`

A node with `live_reads` and `live_writes` gets no payload from `prepare`. Its dispatch names the
buffers of the live lists; after the two lists swap, the next dispatch names them the other way
round, though `io_reads_` and `io_writes_` did not change. `complete_local` then raises the
version of the buffer in `live_writes` and leaves the other alone, which pins finding D3.

### `PrepareWarnsOnceForLocalOnlyOps`

One `prepare` over an offloadable node, a node whose operation no worker has, a node with an
unshared output, an untagged node and a null slot logs one WARN, captured through
`set_log_sink`. Its text is `2 of 3 op-tagged nodes will run locally: 1 no live worker for
the op, 1 buffer not shared, 0 buffer written twice (first: op N)`, where N is the id of the
operation no worker has. A second `prepare` over the offloadable node alone logs nothing.

### `LocalNodeBumpsOnlyItsSharedWrites`

A node that stays local and writes a shared buffer keeps that buffer's handle, and
`complete_local` raises its version. A node that writes no shared buffer gets no `OffloadState`,
so its completion returns after one null check and takes no lock.

### `NodeThatWritesOneBufferTwiceStaysLocal`

A fixed-buffer node and a double-buffered node each list one buffer twice among their writes.
Both stay local, and the one WARN counts them under the third reason, `2 buffer written twice`. A
node that reads and writes the same buffer names it once as an output and stays offloadable. The
local run of the first node still raises the version of the buffer it wrote.

## test_distributed_tcp_faults.cpp

The file runs a master and a worker in one process over TCP on `127.0.0.1` and breaks the link
between them. The master is a `Cluster` on a `RuntimeContext` and binds port 0; the test reads the
port back after `attach`. The worker is a `WorkerAgent` on a second context with one kernel, which
writes each input byte combined with 0x5a to the output. Every test needs a SYCL device and is
reported as skipped without one. Every wait is on a condition and bounded at 60 s: a socket read,
`wait_for_workers`, `wait_for_end`, a future, or `eventually`, which reads a statistics counter
every 2 ms. The file includes the private headers `socket.hpp`, `wire_frame.hpp` and
`wire_messages.hpp` by quoted name.

### `tcp_fault_proxy.hpp`

`TCPFaultProxy` listens on a free loopback port and forwards each accepted connection to the
master, with one pump thread per direction. `set_fault` gives a direction one of five faults:
`Pass`, `Stall` (hold every byte and the end of the stream), `Cut` (close every connection and
refuse new ones), `Throttle` (a number of bytes per second) and `Truncate` (forward a number of
bytes, close, then act as `Cut`). Each fault is one rule in `K_FAULT_RULES`, and the pump asks the
table. The proxy uses `Socket` and `Listener` of the module, so it holds no platform check.
`retarget` sends later connections to another master.

### `StrayConnectionTakesNoSlot`

Finding DIST-04. A connection that never sends HELLO stays open while a worker registers. The
master closes it after the 500 ms handshake timeout without sending it a byte, and it never counts
as a worker.

### `GarbageBytesClosedMasterLives`, `V1FrameRejected`, `OversizedLengthClosesPeerNotProcess`

Findings DIST-15 and DIST-16. A raw socket writes 12 bytes: the text `GET / HTTP/1`, a version 1
frame (a 32-bit length and a type byte), or a version 2 header that announces 1 MiB plus one byte.
The master closes the connection with no reply and `peers_closed_malformed` reaches 1. A worker
then registers, and in the first and the third test a task runs on it.

### `WrongSecretRejected`

Finding DIST-03. A worker with another secret ends with `RejectedAuthentication`: it checks the
master's proof first and sends no PROOF, so the master counts nothing for it. A raw peer then
answers CHALLENGE with a proof of 0x44 bytes, and `handshake_rejected_auth` is 1.

### `VersionMismatchLoggedOncePerConnection`

Finding DIST-06. Two connections each send two headers of version 3. A sink installed with
`set_log_sink` counts the ERROR lines that hold `protocol v3`: two after the master is
detached, one for each connection, and `handshake_rejected_version` is 2.

### `MasterKilledWorkerSurvivesAndReconnects`

Findings DIST-01 and DIST-07. The proxy cuts the link and the master is detached. A second master
attaches on another port and the proxy is retargeted. The worker registers with it, counts one
reconnect, and a task runs on it.

### `WorkerCutMidChunkTaskFinishesLocally`

The proxy closes the link 100 000 bytes into a 1 MiB input sent in 64 KiB chunks, and the worker
does not reconnect. The kernel never runs, the master takes the task back and the local fallback
writes the output. `peers_lost` and `redispatched` are both 1 as soon as the run returns, because
the master raises them before it hands the node back.

### `StalledLinkReapedThenWorkerRejoins`

Finding DIST-05. With a 300 ms liveness timeout the proxy holds every byte until the master has
reaped the worker. The test raises the timeout to 60 s and lets the bytes pass; the worker
registers again on a new connection and runs a task.

### `LinkStalledMidTransferLostAtLivenessClose`

A 32 MiB input is throttled to 8 MiB/s and the proxy stalls the link once 1 MiB has passed. The
master's write idle timeout is 10 minutes, its liveness timeout 2 s and its task deadline 0, so
only the liveness close can end the wait. By then the writer has waited in one socket write for
longer than the connection's 1 s linger, so `close()` shuts the socket: the peer is lost, the
task is resubmitted and the local fallback writes the output.

### `ThrottledTransferKeepsWorkerAlive`

Finding DIST-05. An 8 MiB input crosses at 4 MiB/s under a 500 ms liveness timeout, which takes
about 2 s; the test checks that it took at least 1.5 s. No worker is lost and no task is reaped,
because every chunk and every heartbeat counts as a sign of life.

### `ManyChunkBufferCrossesIntact`

Finding DIST-12. 1 MiB goes to the worker and 1 MiB comes back at `chunk_bytes` 4 KiB: 256 chunks
each way, no abort and no stale writeback.

### `DispatchReturnsWhilePeerStalled`

Finding DIST-14. While the proxy holds every byte, a graph with one offloadable node and one
native node is started. The native node runs, `offloaded` reaches 1 and the task stays pending;
the run ends on the worker once the bytes pass.
