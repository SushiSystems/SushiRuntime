# transport tests

The tests of the `transport` module, mirroring `modules/extension/transport/`.
`tests/extension/transport/CMakeLists.txt` builds them into `sr_transport_test_objects`, linked into `sr_extension_tests`. `sr test` runs
them with the rest; `sr run sr_extension_tests -- --gtest_filter='<pattern>'` runs one program
alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_endpoint.cpp

The file tests `Endpoint`: parsing, the message of a refusal and formatting back. It is pure string work, so
every test is small.

| Test | What it proves |
| --- | --- |
| `ParseTable` | The table holds each accepted form (`host:port`, `[ipv6]:port`, port 0) and one text per refusal status. An IPv6 address without brackets is refused, because its last group would read as a port. |
| `ParseFailureSaysWhatWasWrongWithWhichText` | The refusal message quotes the text and names both accepted forms. |
| `Small_EndpointFormat.FormatIsTheHostPortForm` | The log form is `host:port`, with an IPv6 host in brackets. |
| `Small_EndpointFormat.FormatParsesBack` | The log form of each endpoint parses back to the same host and port. |

## test_buffer_handle.cpp

The file tests `GlobalBufferID`: the invalid id 0.

| Test | What it proves |
| --- | --- |
| `OnlyIDZeroIsInvalid` | Id 0 is invalid and a non-zero id is valid. |

## test_message_type.cpp

The file tests `MessageType`: sixteen types from 1 and the bytes that name none.

| Test | What it proves |
| --- | --- |
| `Small_MessageTypeByte.IsAMessageTypeOnlyFromOneToSixteen` | Bytes 1 and 16 name a type; bytes 0 and 17 name none. |
| `Small_MessageTypeUnknownByte.ANameIsUnknown` | `message_type_name` of a byte outside the enumeration is `UNKNOWN`. |

## test_transport.cpp

The file tests the vocabulary of `transport.hpp`.

| Test | What it proves |
| --- | --- |
| `Small_TransportResult.IsOkOnlyForOk` | `is_ok()` is true for `Ok` and false for every other start status. |
