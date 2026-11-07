# frame_transport tests

The tests of the `frame_transport` module, mirroring `modules/extension/frame_transport/`.
`tests/extension/frame_transport/CMakeLists.txt` builds them into `sr_frame_transport_test_objects`, linked into `sr_extension_tests`. `sr
test` runs them with the rest; `sr run sr_extension_tests -- --gtest_filter='<pattern>'` runs
one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_outbox.cpp

The file covers `Outbox` with no transport and no thread, so every test is small. Its streams are real
pull streams from `BufferExchange` over host buffers, one 4 KiB chunk per frame, and a frame is told
apart by the buffer id and the offset it carries.

| Test | What it proves |
| --- | --- |
| `ControlFramesComeBeforeAnyFrameOfAStream` | Two control frames pushed after a stream come out of `pop()` in order before the stream's first frame. |
| `PopOnAnEmptyOutboxTimesOut` | `pop()` reports `Timeout` when nothing is queued. |
| `AStreamLeavesTheOutboxWithItsFinalFrame` | The stream stays queued after its first frame and is gone once its final frame is popped. |
| `StreamsTakeTurns` | Two streams of 3 and 2 frames come out as first, second, first, second, first. |
| `TheControlQueueRefusesAFrameAtItsCapacity` | An outbox of capacity 2 returns `QueueFull` for the third control frame and keeps a depth of 2. |
| `APopMakesRoomInTheControlQueue` | After one `pop()` the full queue accepts a frame again. |
| `AnOversizedPayloadIsTooLongWhateverTheQueueHolds` | A payload of 1 MiB plus one byte returns `TooLong` on a full queue. |
| `APayloadOfExactlyTheFrameLimitIsNotTooLong` | A payload of exactly 1 MiB is refused as `QueueFull` on a full queue, not as `TooLong`. |
| `CloseDropsTheQueuedStreams` | `close()` removes the queued stream at once. |
| `CloseRefusesLaterPushes` | After `close()` a control push and a stream push return `UnknownPeer`. |
| `ControlQueuedBeforeCloseStillComesOut` | The control frame queued before `close()` still comes out of `pop()`, and the next `pop()` returns `Closed`. |

## test_frame_sending.cpp

The file tests send_message(): one control frame of the message's type, whose payload decodes back.
`Medium_FrameSending` joins the master endpoint and a worker endpoint of one `InProcessFabric`; the
worker's `RecordingTransportEvents` keep what arrives. It starts threads, so every test is medium.

| Test | What it proves |
| --- | --- |
| `Medium_FrameSending.AMessageLeavesAsOneFrameOfItsType` | A PING sent with a scratch buffer arrives at the worker as one frame of its type whose payload carries its token. |
| `Medium_FrameSending.TheFormWithoutABufferQueuesTheSameFrame` | The form that owns its buffer queues a frame whose token decodes to the same value. |
| `Medium_FrameSending.AnUnknownPeerIsReportedAsTheTransportSaidIt` | A peer the master never saw comes back as `UnknownPeer`, the status of `Transport::send()`. |
