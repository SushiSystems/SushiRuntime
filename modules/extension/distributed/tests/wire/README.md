# distributed tests: wire

The frame, the codec of every message and the offload policy.
The tests are part of `sr_distributed_tests`; [the page of the program](../README.md) says
how to run it.

## test_distributed.cpp

The tests in this file need no hardware. They cover what `test_wire_format.cpp` does not pin about
the codec, the in-process transport in both directions, and the offload policy.
`test_wire_format.cpp` holds the golden bytes, the truncations and the range checks of every
message; `test_buffer_registry.cpp` holds the registry.

### `SeededDispatchesSurviveAWholeFrame`

The golden test pins one dispatch. This one encodes 200 dispatches drawn from seed 20261010 with
`append_frame`, reads the header back and decodes the payload. Every wire field must come back
equal: the operation, the name, whose length runs from 0 to `K_MAX_DISPATCH_NAME_BYTES`, the
parameter count, which runs from 0 to `K_MAX_DISPATCH_PARAMETERS`, and every parameter, zero past
the count.

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
`APeerThatAnnouncesVersionTwoIsRefusedWithProtocolMismatch` of that file decodes a header whose
version field is 2, gets `VersionMismatch` with the version read back, and expects
check_protocol_version() to return `ErrorCode::ProtocolMismatch` naming v2 and v3.

### `SeededMutationsDecodeOrFailInsideThePayload`

`every_sample` encodes one well-formed payload per message type. Each is damaged 500 times by
`mutated`: one byte replaced, the tail cut, up to 8 bytes appended, or four bytes in a row set to
0xFF. Every decode must return, and the offset it reports must lie inside the damaged payload.

### `SeededHeadersPassOnlyInsideTheirRanges`

4000 random 12-byte headers are decoded. Three in four carry the magic and version 3 so that the
type, flag and length checks run. A header that passes must name one of the sixteen types, carry a
payload length of at most 1 MiB, and carry the last-chunk flag only on `BUFFER_CHUNK`.

## test_wire_format.cpp

The file pins protocol version 3 byte for byte and needs no hardware. Every expected frame is a
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

No message of version 3 carries a bool, so the bool check runs on `FieldReader::read_bool` directly.
The enumeration checks replace one byte of a golden payload and expect `OutOfRange` at that byte's
offset. `REJECT`, `GOODBYE`, `TASK_RESULT`, `BUFFER_CHUNK` and `BUFFER_ABORT` carry the
enumerations. A `TASK_DISPATCH` of version 3 carries none, so its check is on the one bounded byte
it has: a parameter count of 9, at offset 18 of the golden payload, behind the task id, the
operation and the 6-byte name field. The header checks cover a wrong magic, another version, an unknown type, a reserved flag,
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
