# transport tests

The tests of the `transport` module, mirroring `modules/extension/transport/`.
`tests/extension/transport/CMakeLists.txt` builds them into `sr_transport_tests`. `sr test` runs
them with the rest; `sr run sr_transport_tests -- --gtest_filter='<pattern>'` runs one program
alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_endpoint.cpp

The file tests `Endpoint`: parsing, the message of a refusal and formatting back. It needs no
hardware and opens no socket.

| Test | What it proves |
| --- | --- |
| `ParseTable` | The table holds each accepted form (`host:port`, `[ipv6]:port`, port 0) and one text per refusal status. An IPv6 address without brackets is refused, because its last group would read as a port. |

## test_buffer_handle.cpp

The file tests GlobalBufferID and BufferHandle: the invalid id 0, equality and the defaults.

| Test | What it proves |
| --- | --- |
| `IDZeroIsInvalid` | Id 0 is invalid and ids compare by value. |
| `DefaultsNameNoBuffer` | Checks the defaults of a handle. |

## test_message_type.cpp

The file tests MessageType: sixteen types from 1, their names, and the bytes that name none.

| Test | What it proves |
| --- | --- |
| `SixteenTypesFromOne` | The types run from 1 to 16 and only those bytes name a type. |
| `NamesAreTheWireNames` | Checks the name of a type and the name of a byte that names none. |

## test_transport.cpp

The file tests the vocabulary of transport.hpp: a result that holds no failure, and peer ids.

| Test | What it proves |
| --- | --- |
| `ADefaultResultIsOk` | A default result is Ok with no message. |
| `PeersCompareByValue` | Peer ids compare by value and the send statuses differ. |
