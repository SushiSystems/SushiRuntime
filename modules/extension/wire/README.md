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
