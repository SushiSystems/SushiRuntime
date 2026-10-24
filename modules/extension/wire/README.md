# wire

The module holds the bytes of the protocol: the little-endian field codec, the 12-byte frame header
and its version check, one struct per message with its encoder and decoder, and the router that
hands a frame to the handler of its type. It is an internal module: its headers reach the modules
that name it and the tests, and are never installed. It is built under `SR_ENABLE_DISTRIBUTED`, and
its names are in `SushiRuntime::Distributed::Wire`.

It depends on `core` and `transport`.

## Public entry points

| Header | Holds |
| --- | --- |
| `wire_codec.hpp` | `FieldWriter`, `FieldReader`, `CodecStatus`, `DecodeResult` |
| `wire_frame.hpp` | `FrameHeader`, the header's encoder and decoder, `check_protocol_version()` |
| `wire_messages.hpp` | one message struct per `MessageType` |
| `message_router.hpp` | `MessageRouter`, `RouteStatus` |

## Protocol versions

`K_PROTOCOL_VERSION` in `wire_frame.hpp` is 4. The header and the first three fields of `REJECT` and
`GOODBYE` are frozen for every version, so a peer of another version is answered with a `REJECT` of
reason `Version` that names both versions: this build's in `sender_version`, the peer's in its text.

| Version | What the code does with it | How it differs from version 4 |
| --- | --- | --- |
| 4 | reads and writes | the current version |
| 3 | rejects with `ProtocolMismatch` naming both versions | `HELLO` ends at the node name; `TASK_DISPATCH` ends at the output handles |
| 2 and 1 | rejects with `ProtocolMismatch` naming both versions | earlier layouts; see `modules/extension/distributed/README.md` |

## Messages that gained fields in version 4

| Message | Fields after the version 3 fields |
| --- | --- |
| `HELLO` | the operating system (1 byte: 0 unknown, 1 Linux, 2 Windows); the devices (a 32-bit count, then per device the kind in 1 byte, the name as a string, the memory in a 64-bit integer); the labels (a 32-bit count, then per label the key and the value, each a string) |
| `TASK_DISPATCH` | the index of the worker's device that runs the kernel (32-bit); the master sends 0 until placement fills it |

The device kind is 0 CPU, 1 GPU, 2 accelerator or 3 other. A string is a 16-bit length and its bytes,
at most 255. A decoder checks a device count against 11 bytes per device and a label count against 4
bytes per label before it reserves. A decoded `HELLO` holds views into the payload for the names, the
keys and the values, as it does for the build id and the node name.
