# in_process_transport tests

The tests of the `in_process_transport` module, mirroring `modules/extension/in_process_transport/`.
`tests/extension/in_process_transport/CMakeLists.txt` builds them into
`sr_in_process_transport_test_objects`, linked into `sr_extension_tests`. `sr test` runs them with
the rest; `sr run sr_extension_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_in_process_transport.cpp

The file runs the `Transport` contract on a master and a worker endpoint of one `InProcessFabric`. Each
endpoint runs a pump thread, so every test is medium. An `EventRecorder` implements `TransportEvents`,
copies every event into a list and lets a test wait until the list satisfies a condition; a test may set
a reaction, which runs inside the event on the pump thread, outside the recorder's lock. The tests wait
on recorded events with a 10 s limit and never on a sleep. The same promises over TCP are in
`tests/extension/distributed/README.md`.

| Test | What it proves |
| --- | --- |
| `Medium_TransportContract.DeliversWorkerFramesInOrder` | The worker queues 500 control frames that carry a counter. The master sees the counters in order. |
| `DeliversMasterFramesInOrder` | The master queues 500 control frames that carry a counter. The worker sees the counters in order. |
| `StatisticsCountEveryFrameWithItsHeader` | The worker's sent bytes and the master's received bytes cover the 12-byte header and the 4-byte payload of each of 500 frames. |
| `QueuedFramesArriveBeforeTheLossOfTheirPeer` | The worker queues 100 frames and calls `close_peer()`. The master records the connection, the 100 frames in order and one `ClosedByPeer` loss, 102 events in all. |
| `TheClosingSideHearsOneLocalLossThenAbandons` | The side that calls `close_peer()` records one `ClosedLocally` loss and then one `on_connect_abandoned()`, since its reconnect is off. |
| `ASendToAClosedPeerReturnsUnknownPeer` | A send to the closed peer returns `UnknownPeer` on both ends. |
| `StopReportsNoLoss` | `stop()` makes no `on_peer_lost` event. |
| `NoEventIsMadeAfterStopReturns` | While the worker floods the master with heartbeats, the master stops; the master's event list then stops growing although the worker keeps sending. |
| `AnEventMayAnswerTheFramesItReceives` | The master answers each of 64 frames from inside the callback with a `PONG` carrying the same counter; no send is refused, and the worker sees the counters in order. |
| `AnEventMaySendFromTheConnectionCallback` | The master sends a `PING` from the callback that announces the worker; the worker receives it. |
| `AControlQueueOfNoFramesRefusesEverySend` | A master whose `control_queue_frames` is 0 refuses a send to a connected worker as `QueueFull`. The `Outbox` under it holds at least one frame, so the bound is kept by the endpoint. |
| `ASendBeforeStartReturnsStopped` | A transport that was never started refuses a send as `Stopped`. |
| `ASendToAnUnknownPeerReturnsUnknownPeer` | A started transport refuses a send to a peer it never saw as `UnknownPeer`. |
| `ASecondStartReturnsAlreadyStarted` | A second `start()` returns `AlreadyStarted` with a message. |
| `ASendAfterStopReturnsStopped` | After `stop()`, called twice, a send returns `Stopped`. |
| `AStoppedTransportCannotStartAgain` | A `start()` after `stop()` returns `AlreadyStarted` and makes no event. |
| `AStreamOfferedAfterStopReturnsStopped` | `send_stream()` on a stopped transport returns `Stopped`. |

## compile_in_process_transport_log_category.cpp

The file includes `in_process_transport_log_category.hpp` alone, so the header carries every include it
needs. The header holds a constant, which has no behaviour of its own to test.
