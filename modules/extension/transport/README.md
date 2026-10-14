# transport

The module holds the vocabulary every link of a cluster speaks: the endpoint a peer listens on, the
type of each message, the cluster-wide handle of a buffer, and `Transport`, the interface a master
or a worker passes frames through. It is built with the distributed layer, under
`SR_ENABLE_DISTRIBUTED`, and every name it declares is in `SushiRuntime::Distributed`. Its headers
are installed: a program that names an endpoint or writes its own transport includes them.

It depends on `core`.

## Public entry points

| Header | Holds |
| --- | --- |
| `endpoint.hpp` | `Endpoint`, `EndpointParseResult`, `EndpointParseStatus` |
| `message_type.hpp` | `MessageType`, `message_type_name()` |
| `buffer_handle.hpp` | `GlobalBufferID`, `BufferHandle` |
| `transport.hpp` | `Transport`, `TransportEvents`, `FrameSource`, `PeerID`, `PeerLoss`, `SendStatus`, `TransportResult` |

## transport.hpp

### The transport contract

A transport moves frames of protocol version 3. It checks each frame's 12-byte header and knows
nothing about what a payload means; the handshake runs above it, so both transports exercise the
same session code.

**Peers.** A transport numbers its connections from 1 and never reuses a number. A worker that
reconnects is a new `PeerID` on the master, and each connection to the master is a new `PeerID` on
the worker. There is no fixed id for the master and no peer count on the interface.

**Start and stop.** `start(events)` is called once. A master binds and listens inside it, so a
port that is taken comes back as `BindFailed` with the endpoint and the OS reason in the message.
On `Ok` a master's message is the endpoint it listens on. A worker returns `Ok` with an empty
message and connects on its own thread. A second `start()` returns `AlreadyStarted`.
`stop()` is `noexcept`, blocks until every transport thread has ended and makes no event once it
returns; it closes peers without `on_peer_lost()`. A stopped transport does not start again.

**Events.** For one peer: `on_peer_connected()`, then `on_frame()` in the order the peer sent, then
`on_peer_lost()` exactly once, never two at a time. Events of different peers may overlap. The
payload span is valid during `on_frame()` only. An event may call `send()`, `send_stream()` and
`close_peer()`, also for its own peer; it must not call `start()` or `stop()`. An exception that
leaves `on_frame()` is caught by the transport, which closes that peer with `HandlerFailed` and
keeps the process alive. The transport does not log a loss; the receiver does, because only the
receiver knows whether the peer was a registered worker.

**Sending.** `send()` copies a control frame into the peer's queue and returns; it never waits for
a socket. `send_stream()` hands over a `FrameSource`, which the peer's writer asks for one frame at
a time. The writer sends every queued control frame, then one stream frame, taking the peer's
streams in turn, so a control frame waits behind at most one chunk. Control frames to one peer keep
their order, and so do the frames of one stream; a control frame queued after a stream may arrive
before the stream's frames. A result that must follow its output bytes is therefore the last frame
of the same stream. `QueueFull` means the control queue is at its bound and nothing was queued.

**Closing.** `close_peer()` returns at once. Sends to the peer return `UnknownPeer` from that
point, streams are dropped, queued control frames are written for a bounded time, and
`on_peer_lost()` reports `ClosedLocally`.

Over TCP the writer gets `close_linger` (1 s) to write those frames. A writer that has already
waited that long inside one socket write when `close_peer()` is called has had its time:
`PeerConnection::close()` shuts the socket itself, the reader reports the loss at once and the
queued frames are dropped. A write that has waited for less runs on until it ends or
`write_idle_timeout` passes, and in the second case the loss is reported as `WriteStalled`. The
in-process transport has no such wait: its pump never blocks in a send, `Outbox::close()` drops
the streams, and the next round of the pump reports the loss.

**A worker that cannot connect.** `on_connect_abandoned()` is called once when a worker-side
transport stops trying: the time allowed for the first connection ran out, or the master was lost
and reconnecting is off.

### Exposure

`exposure()` is `Loopback` for an in-process fabric and for a TCP endpoint whose bind address or
master host is a loopback address, and `Network` otherwise. `TCPMasterOptions::bind` defaults to
`127.0.0.1`.
