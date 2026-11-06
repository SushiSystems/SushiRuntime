# wire tests

The tests of the `wire` module, mirroring `modules/extension/wire/`.
`tests/extension/wire/CMakeLists.txt` builds them into `sr_wire_test_objects`, linked into `sr_extension_tests`. `sr test` runs them with the
rest; `sr run sr_extension_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_wire_frame.cpp

The file pins the frame header of protocol version 4 byte for byte and needs no hardware, so every test is
small. Every expected header is a literal byte array typed into the test.

| Test | What it proves |
| --- | --- |
| `FrameHeaderGolden` | The 12-byte header is the magic `S R D P`, a 16-bit version, the type byte, the flags byte and a 32-bit payload length, all little-endian. |
| `TheLastChunkFlagDecodes` | A `BUFFER_CHUNK` header that carries the last-chunk flag decodes with its type, flag and length. |
| `PingFrameGolden` | A whole `PING` frame built by `append_frame` equals the literal bytes. |
| `MagicReadsAsALittleEndianNumber` | The first four bytes of a frame, read as a little-endian number, are 0x50445253. |
| `TheVersionOfThisBuildIsAccepted` | `check_protocol_version()` accepts `K_PROTOCOL_VERSION`. |
| `Small_WireOlderPeer.IsRefusedWithProtocolMismatch` | A header whose version field is 2 or 3 decodes to `VersionMismatch` with the version read back, and `check_protocol_version()` returns `ErrorCode::ProtocolMismatch` naming that version and this build's. |

## test_wire_messages.cpp

The file pins protocol version 4 byte for byte and needs no hardware, so every test is small. Every
expected frame is a literal byte array typed into the test, never the output of the encoder under test, so
a change to a field's width, order or byte order fails here until the golden bytes and
`K_PROTOCOL_VERSION` change together.

| Test | What it proves |
| --- | --- |
| `GoldenBytes_<Message>` | One test for each of the sixteen messages. It encodes a message with fixed field values and compares the payload with the literal bytes, then decodes those bytes and compares every field. |
| `HelloWithTwoDevicesAndTwoLabelsDecodes` | A `HELLO` with two devices and two labels, one empty name and one empty value among them, encodes and decodes with every field and its order intact. |
| `Small_WireTruncation.EveryPayloadPrefixFailsCleanly` | Each strict prefix of each golden payload is copied into an allocation of exactly its size, so a read past the end is visible to ASan, and must decode to an error. `BUFFER_CHUNK` is the one exception: its data runs to the end of the payload, so a cut inside the data is a shorter chunk. |
| `Small_WireTruncation.EveryFramePrefixIsShort` | The frame level of the same promise: a prefix shorter than the header is `Truncated`, and a longer one is a header whose payload length exceeds the bytes that arrived. |
| `ABoolOutsideZeroAndOneIsRefused` | No message of version 4 carries a bool, so the check runs on `FieldReader::read_bool` directly: 2 is `OutOfRange` at its offset. |
| `Small_WireEnumRange.IsRefusedAtItsOffset` | One byte of a golden payload is replaced by a value outside its enumeration, and the decode returns `OutOfRange` at that byte's offset. `REJECT`, `GOODBYE`, `TASK_RESULT`, `BUFFER_CHUNK` and `BUFFER_ABORT` carry the enumerations, and `HELLO` carries two: the operating system and the device kind. A `TASK_DISPATCH` of version 4 carries none, so its check is on the one bounded byte it has: a parameter count of 9, at offset 18 of the golden payload, behind the task id, the operation and the 6-byte name field. |
| `Small_WireHeaderRange.GetsTheStatusOfItsField` | A wrong magic is `BadMagic`, an unknown type `UnknownType`, and a reserved flag or the last-chunk flag on a frame that is not a chunk `ReservedFlags`. |
| `APayloadOfExactlyTheFrameLimitIsAccepted` | A header that announces a payload of 1 MiB decodes. |
| `APayloadOneByteOverTheFrameLimitIsTooLong` | A header that announces a payload of 1 MiB plus one byte is `PayloadTooLong`. |
| `Small_WireListCount.FailsBeforeAnyReserve` | A list count is compared with the bytes that remain before any `reserve`. The rows send a count of 2^32 - 1, a count one above what fits, and a count whose product with the element size wraps 32 bits, for the operations of `PROOF`, the inputs of `TASK_DISPATCH`, the devices and labels of `HELLO`, and the outputs of `TASK_RESULT`. Each fails with `OutOfRange` at the count, and the destination vector's capacity is still 0. |
| `RejectsTrailingBytes` | A payload with one byte after its last field fails with `TrailingBytes`. |
| `Small_WireLongString.DoesNotEncode` | A string of 256 bytes in a `GOODBYE` text, a `HELLO` device name or a `HELLO` label key does not encode. |
| `AnEncodeRefusalAppendsNoFrame` | `append_frame` cuts the buffer back to the size it had on entry when the payload does not encode. |
| `AStringAbove255BytesDoesNotDecode` | A payload that declares a 256-byte string fails to decode at the length field. |

## test_message_router.cpp

| Test | What it proves |
| --- | --- |
| `RouterDeliversByType` | `MessageRouter` calls the handler set for a type and reports `Unhandled` for a type with no handler and for a byte that names no message. |

## test_wire_codec.cpp

The tests in this file need no hardware, so they are small. They cover what `test_wire_messages.cpp` does
not pin about the codec: seeded round trips and seeded damage to every decoder. A decoder must refuse
garbage without reading past the payload, because a frame's payload is decoded before its sender is
trusted with anything else; under the ASan lane an out-of-bounds read fails the run.

| Test | What it proves |
| --- | --- |
| `Small_WireCodecRoundTrip.SeededDispatchSurvivesAWholeFrame` | The golden test pins one dispatch. This one encodes the dispatch of each of 200 seeds from 20261010 with `append_frame`, reads the header back and decodes the payload. Every wire field must come back equal: the operation, the name, whose length runs from 0 to `K_MAX_DISPATCH_NAME_BYTES`, the parameter count, which runs from 0 to `K_MAX_DISPATCH_PARAMETERS`, every parameter, zero past the count, and the device index. |
| `Small_WireCodecMutation.SeededMutationsDecodeOrFailInsideThePayload` | One well-formed payload per message type is damaged 500 times by `mutated`: one byte replaced, the tail cut, up to 8 bytes appended, or four bytes in a row set to 0xFF. Every decode must return, and the offset it reports must lie inside the damaged payload. |
| `Small_WireCodecFuzz.SeededHeadersPassOnlyInsideTheirRanges` | 4000 random 12-byte headers are decoded. Three in four carry the magic and version 4 so that the type, flag and length checks run. A header that passes must name one of the sixteen types, carry a payload length of at most 1 MiB, and carry the last-chunk flag only on `BUFFER_CHUNK`. |
