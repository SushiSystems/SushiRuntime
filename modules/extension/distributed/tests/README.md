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
