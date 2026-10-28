# wire tests

The tests of the `wire` module, mirroring `modules/extension/wire/`.
`tests/extension/wire/CMakeLists.txt` builds them into `sr_wire_test_objects`, linked into `sr_extension_tests`. `sr test` runs them with the
rest; `sr run sr_extension_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_wire_frame.cpp

The file pins the frame header of protocol version 4 byte for byte and needs no hardware. Every
expected header is a literal byte array typed into the test.

| Test | What it proves |
| --- | --- |
| `FrameHeaderGolden` | The 12-byte header is the magic `S R D P`, a 16-bit version, the type byte, the flags byte and a 32-bit payload length, all little-endian. The test also decodes a `BUFFER_CHUNK` header that carries the last-chunk flag and compares a whole `PING` frame built by `append_frame`. |
| `MagicReadsAsOversizedV1Length` | A version 1 peer reads the first four bytes of a frame as a length. The magic reads as 0x50445253, about 1.35 GB, which is above version 1's 256 MiB cap, so that peer drops the frame. |
| `APeerThatAnnouncesVersionTwoIsRefusedWithProtocolMismatch` | A header whose version field is 2 decodes to `VersionMismatch` with the version read back, and `check_protocol_version()` returns `ErrorCode::ProtocolMismatch` naming v2 and v4. |
| `APeerThatAnnouncesVersionThreeIsRefusedWithProtocolMismatch` | The same for a header whose version field is 3, the version before this one: `VersionMismatch`, then `ProtocolMismatch` naming v3 and v4. |

## test_wire_messages.cpp

The file pins protocol version 4 byte for byte and needs no hardware. Every expected frame is a
literal byte array typed into the test, never the output of the encoder under test, so a change to
a field's width, order or byte order fails here until the golden bytes and
`K_PROTOCOL_VERSION` change together.

| Test | What it proves |
| --- | --- |
| `GoldenBytes_<Message>` | One test for each of the sixteen messages. It encodes a message with fixed field values and compares the payload with the literal bytes, then decodes those bytes and compares every field. |

### `EveryTruncationFailsCleanly`

Each strict prefix of each golden payload is copied into an allocation of exactly its size, so a
read past the end is visible to ASan, and must decode to an error. `BUFFER_CHUNK` is the one
exception: its data runs to the end of the payload, so a cut inside the data is a shorter chunk. The
second loop covers that case at frame level, where the header's payload length exceeds the bytes
that arrived.

### `RejectsOutOfRangeEnumAndBool`

No message of version 4 carries a bool, so the bool check runs on `FieldReader::read_bool` directly.
The enumeration checks replace one byte of a golden payload and expect `OutOfRange` at that byte's
offset. `REJECT`, `GOODBYE`, `TASK_RESULT`, `BUFFER_CHUNK` and `BUFFER_ABORT` carry the
enumerations, and `HELLO` carries two: the operating system and the device kind. A `TASK_DISPATCH` of version 4 carries none, so its check is on the one bounded byte
it has: a parameter count of 9, at offset 18 of the golden payload, behind the task id, the
operation and the 6-byte name field. The header checks cover a wrong magic, another version, an unknown type, a reserved flag,
the last-chunk flag on a frame that is not a chunk, and a payload length one byte above 1 MiB.

| Test | What it proves |
| --- | --- |
| `ListCountBoundedByRemainingBytes` | A list count is compared with the bytes that remain before any `reserve`. The test sends a count of 2^32 - 1, a count one above what fits, and a count whose product with the element size wraps 32 bits, and checks that the destination vector's capacity is still 0 afterwards. |
| `HelloWithTwoDevicesAndTwoLabelsDecodes` | A `HELLO` with two devices and two labels, one empty name and one empty value among them, encodes and decodes with every field and its order intact. |
| `HelloListCountsAreBoundedByRemainingBytes` | A device count of 2^32 - 1, a device count one above what the bytes hold and a label count of 2^30 each fail with `OutOfRange` at the count, and the destination vector's capacity stays 0. |
| `HelloRefusesAnUnknownOperatingSystemAndDeviceKind` | An operating system byte of 3 and a device kind byte of 4 fail with `OutOfRange` at their offsets. |
| `HelloRefusesAStringAbove255Bytes` | A device name or a label key of 256 bytes does not encode. |
| `RejectsTrailingBytes` | A payload with one byte after its last field fails with `TrailingBytes`. |
| `StringLengthBoundedAt255` | A string of 256 bytes fails to encode, and `append_frame` cuts the buffer back to the size it had on entry. A payload that declares a 256-byte string fails to decode. |

## test_message_router.cpp

| Test | What it proves |
| --- | --- |
| `RouterDeliversByType` | `MessageRouter` calls the handler set for a type and reports `Unhandled` for a type with no handler and for a byte that names no message. |

## test_wire_codec.cpp

The tests in this file need no hardware. They cover what `test_wire_messages.cpp` does not pin about
the codec: seeded round trips and seeded damage to every decoder.

### `SeededDispatchesSurviveAWholeFrame`

The golden test pins one dispatch. This one encodes 200 dispatches drawn from seed 20261010 with
`append_frame`, reads the header back and decodes the payload. Every wire field must come back
equal: the operation, the name, whose length runs from 0 to `K_MAX_DISPATCH_NAME_BYTES`, the
parameter count, which runs from 0 to `K_MAX_DISPATCH_PARAMETERS`, every parameter, zero past
the count, and the device index.

| Test | What it proves |
| --- | --- |
| Malformed wire input | A frame's payload is decoded before its sender is trusted with anything else, so a decoder must refuse garbage without reading past the payload. The `Medium_WireCodecFuzz` suite feeds seeded damage to the decoders; under the ASan lane an out-of-bounds read fails the run. |

### `TaskResultRefusesACountItCannotHold`

A `TASK_RESULT` that claims 1000 outputs and carries none must fail at the count, offset 9, with
`OutOfRange`, and leave the output list empty. `ListCountBoundedByRemainingBytes` of
`test_message_type.cpp` pins the same rule for `PROOF` and `TASK_DISPATCH`.
`APeerThatAnnouncesVersionTwoIsRefusedWithProtocolMismatch` of that file decodes a header whose
version field is 2, gets `VersionMismatch` with the version read back, and expects
check_protocol_version() to return `ErrorCode::ProtocolMismatch` naming v2 and v4.

| Test | What it proves |
| --- | --- |
| `SeededMutationsDecodeOrFailInsideThePayload` | `every_sample` encodes one well-formed payload per message type. Each is damaged 500 times by `mutated`: one byte replaced, the tail cut, up to 8 bytes appended, or four bytes in a row set to 0xFF. Every decode must return, and the offset it reports must lie inside the damaged payload. |
| `SeededHeadersPassOnlyInsideTheirRanges` | 4000 random 12-byte headers are decoded. Three in four carry the magic and version 4 so that the type, flag and length checks run. A header that passes must name one of the sixteen types, carry a payload length of at most 1 MiB, and carry the last-chunk flag only on `BUFFER_CHUNK`. |
