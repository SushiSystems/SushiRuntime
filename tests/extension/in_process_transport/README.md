# in_process_transport tests

The tests of the `in_process_transport` module, mirroring `modules/extension/in_process_transport/`.
`tests/extension/in_process_transport/CMakeLists.txt` builds them into
`sr_in_process_transport_test_objects`, linked into `sr_extension_tests`. `sr test` runs them with
the rest; `sr run sr_extension_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_in_process_transport.cpp

The file needs no hardware. `InProcessTransport` also runs the conformance suite of
`tests/extension/distributed/test_tcp_transport.cpp` as its reference.

### `InProcessTransportDeliversBothDirections`

A master endpoint and a worker endpoint each start with a recording `TransportEvents`. The test
waits on the `on_peer_connected` event of both sides, sends one control frame each way and checks
that each side sees the frame under the `PeerID` its own `on_peer_connected` named, with the type,
flags 0 and the payload it was sent.

## test_in_process_transport_log_category.cpp

| Test | What it proves |
| --- | --- |
| `TheCategoryIsNamedAfterTheModule` | `K_IN_PROCESS_TRANSPORT_LOG_CATEGORY` names the module `in_process_transport` in the project `sushiruntime`, at compile time. |
