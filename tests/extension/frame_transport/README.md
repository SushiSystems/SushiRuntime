# frame_transport tests

The tests of the `frame_transport` module, mirroring `modules/extension/frame_transport/`.
`tests/extension/frame_transport/CMakeLists.txt` builds them into `sr_frame_transport_tests`. `sr
test` runs them with the rest; `sr run sr_frame_transport_tests -- --gtest_filter='<pattern>'` runs
one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_outbox.cpp

The file covers `Outbox` with no transport and no thread.

| Test | What it proves |
| --- | --- |
| `ControlComesBeforeStream` | `Outbox::pop()` returns two control frames that were pushed after a stream before the stream's first frame, destroys the source after its final frame and reports `Timeout` when nothing is queued. |
| `StreamsTakeTurns` | Two streams of 3 and 2 frames come out as first, second, first, second, first. |
| `QueueFullAtCapacity` | An outbox of capacity 2 returns `QueueFull` for the third control frame and `Queued` again after one `pop()`. A payload of 1 MiB plus one byte returns `TooLong` whatever the queue holds. |
| `CloseKeepsControlAndDropsStreams` | `close()` destroys the queued stream at once and refuses later pushes with `UnknownPeer`. The control frame queued before it still comes out of `pop()`, and the next `pop()` returns `Closed`. |

## test_frame_sending.cpp

The file tests send_message(): one control frame of the message's type, whose payload decodes back.

| Test | What it proves |
| --- | --- |
| `AMessageLeavesAsOneFrameOfItsType` | Keeps the last frame it was asked to send. */ class LastFrameTransport final : public Transport { public: TransportResult start(TransportEvents&) override { return TransportResult{}; } void stop() noexcept override {} SendStatus send(PeerID peer, MessageType type, span<const std::byte> payload) override { last_peer = peer; last_type = type; last_payload.assign(payload.begin(), payload.end()); ++sends; return SendStatus::Queued; } SendStatus send_stream(PeerID, std::unique_ptr<FrameSource>) override { return SendStatus::Stopped; } void close_peer(PeerID) noexcept override {} TransportExposure exposure() const noexcept override { return TransportExposure::Loopback; } TransportStatistics statistics() const noexcept override { return TransportStatistics{}; } PeerID last_peer{0}; /**< The peer of the last frame. */ MessageType last_type = MessageType::Hello; /**< The type of the last frame. */ std::vector<std::byte> last_payload; /**< The payload of the last frame. */ int sends = 0; /**< The calls of send(). */ }; } /** @brief Tests that a PING leaves as one frame of its type whose payload carries its token. |
