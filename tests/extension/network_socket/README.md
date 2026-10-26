# network_socket tests

The tests of the `network_socket` module, mirroring `modules/extension/network_socket/`.
`tests/extension/network_socket/CMakeLists.txt` builds them into `sr_network_socket_tests`. `sr
test` runs them with the rest; `sr run sr_network_socket_tests -- --gtest_filter='<pattern>'` runs
one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_socket.cpp

The file tests `Socket` and `Listener`. It needs no hardware and no network beyond
loopback. Every listener binds port 0 and the test reads the port back, so two runs never compete
for a port. No test sleeps: each waits inside a socket call with a 10 s limit that loopback never
reaches.

| Test | What it proves |
| --- | --- |
| `connect_pair` | `connect_pair` connects a client to a listener and accepts the other end. The connect finishes against the listen queue, so one thread can do both steps in that order. |
| `ExactReadAndFullWriteRoundTrip` | The payload is 4 MiB and 3 bytes, more than the OS buffers of a loopback connection hold, so the write has to wait for the reader and the read has to collect many pieces. A thread writes while the test thread reads. |
| `ShutdownEndsAWaitingRead` | `shutdown()` runs on the test thread while another thread is in `read_exact`, or about to enter it. Both orders end the read with `Closed`, which is what lets a transport stop its reader thread. |

### `WriteToClosedPeerReturnsError`

The regression test of finding DIST-01. The accepted end closes, the client reads until it sees the
end of the stream, then writes until a write fails. On Linux a write to a reset connection raises
SIGPIPE unless the send call suppresses it, and the signal would end the test program; the test
passes only when the write returns `Closed` with the OS error. The write after that must return
`Closed` with no OS error, because the socket refuses to write again once a write has failed.

| Test | What it proves |
| --- | --- |
| `BindInUseNamesPortAndCause` | A second listener binds the port of the first. The result must name the `bind` call, carry the OS error and its text, and `describe()` must print the port, so the log line of finding DIST-17 can be built from it. |
| `LocalhostResolves` | The test resolves `localhost`, then connects by that name to a listener bound to `127.0.0.1`. Where the OS lists `::1` first, the connect is refused there and moves on to the next address. On Windows that refusal takes about two seconds. |
| `Ipv6LoopbackConnects` | The test is reported as skipped, with the OS error, on a machine where `::1` cannot be bound. |

## test_network_socket_log_category.cpp

| Test | What it proves |
| --- | --- |
| `TheCategoryIsNamedAfterTheModule` | `K_NETWORK_SOCKET_LOG_CATEGORY` names the module `network_socket` in the project `sushiruntime`, at compile time. |

## test_socket_option_report.cpp

| Test | What it proves |
| --- | --- |
| `Medium_Socket.LogsAFailedNoDelayOption` | The OS does not refuse `TCP_NODELAY` on loopback, so the test calls `Detail::report_no_delay_failure` directly under a `LogCapture` and asserts one `WARN` in the `network_socket` module that names the peer and the option. The two call sites are not covered by a refusal of the real call. |
