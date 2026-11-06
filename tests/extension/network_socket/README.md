# network_socket tests

The tests of the `network_socket` module, mirroring `modules/extension/network_socket/`.
`tests/extension/network_socket/CMakeLists.txt` builds them into `sr_network_socket_test_objects`, linked into `sr_extension_tests`. `sr
test` runs them with the rest; `sr run sr_extension_tests -- --gtest_filter='<pattern>'` runs
one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_socket.cpp

The file tests `Socket` and `Listener`. It needs no hardware and no network beyond
loopback, so every test is medium. Every listener binds port 0 and the test reads the port back, so two
runs never compete for a port. No test sleeps: each waits inside a socket call with a 10 s limit that
loopback never reaches, or with a 20 ms limit for the two calls that must time out.

Three fixtures open the sockets: `Medium_ListeningSocketTest` binds an IPv4 listener,
`Medium_ConnectedSocketTest` adds a connected client and server pair, and `Medium_IPv6SocketTest` does the
same on `::1` and is reported as skipped, with the OS error, on a machine where `::1` cannot be bound.

| Test | What it proves |
| --- | --- |
| `ALargeWriteArrivesWholeThroughAnExactRead` | The payload is 4 MiB and 3 bytes, more than the OS buffers of a loopback connection hold, so the write has to wait for the reader and the read has to collect many pieces. A thread writes while the test thread reads. |
| `AReplyArrivesWholeThroughAnExactRead` | A 5-byte reply from the accepted end arrives whole at the client. |
| `PeerEndpointNamesTheClient` | The accepted end names the client by the loopback host and a non-zero port. |
| `AnIdleReadTimesOut` | `read_exact` with nothing to read returns `Timeout` and no bytes. |
| `AnIdleAcceptTimesOut` | `accept` with no client returns `Timeout` and leaves the socket closed. |
| `ShutdownEndsAWaitingRead` | `shutdown()` runs on the test thread while another thread is in `read_exact`, or about to enter it. Both orders end the read with `Closed`, which is what lets a transport stop its reader thread. |
| `AWriteToAClosedPeerReturnsClosedWithTheOSCause` | The regression test of finding DIST-01. The accepted end closes, the client reads until it sees the end of the stream, then writes until a write fails. On Linux a write to a reset connection raises SIGPIPE unless the send call suppresses it, and the signal would end the test program; the test passes only when the write returns `Closed` with the OS error and its text. |
| `AWriteAfterTheFailureReturnsClosedWithNoOSError` | The write after the failed one returns `Closed` with no OS error and no bytes, because the socket refuses to write again once a write has failed. |
| `BindInUseNamesPortAndCause` | A second listener binds the port of the first. The result must name the `bind` call, carry the OS error and its text, and `describe()` must print the port, so the log line of finding DIST-17 can be built from it. |
| `LocalhostResolvesToTheIPv4Loopback` | `resolve_addresses` lists `127.0.0.1` for `localhost`. |
| `ConnectingByNameReachesTheListener` | A connect by the name `localhost` to a listener bound to `127.0.0.1` succeeds and names the listener's port. Where the OS lists `::1` first, the connect is refused there and moves on to the next address. On Windows that refusal takes about two seconds. |
| `AListenerReportsItsLoopbackHost` | A listener bound to `::1` reports `::1` as its host. |
| `TheClientNamesItsPeer` | The client of an IPv6 connection names `::1` and the listener's port. |
| `BytesCrossTheConnection` | 64 bytes written on one end of an IPv6 connection arrive on the other. |

## compile_network_socket_log_category.cpp and compile_socket_option_report.cpp

Each file includes one header alone, `network_socket_log_category.hpp` and `socket_option_report.hpp`, so
the header carries every include it needs. The first holds a constant, which has no behaviour of its own
to test. The second logs a refused `TCP_NODELAY`, which the OS never refuses on loopback; its only caller
is the connection setup, and no test can make that call fail.
