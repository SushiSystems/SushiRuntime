# distributed tests: link

What carries a frame between two machines: the socket, the handshake and the transports.
The tests are part of `sr_distributed_tests`; [the page of the program](../README.md) says
how to run it.

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

## test_handshake.cpp

The tests in this file need no hardware and no network. They pin the two primitives the protocol v3
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
