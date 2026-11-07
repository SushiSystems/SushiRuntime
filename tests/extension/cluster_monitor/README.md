# cluster_monitor tests

The tests of the `cluster_monitor` module, mirroring `modules/extension/cluster_monitor/`.
`tests/extension/cluster_monitor/CMakeLists.txt` builds them into `sr_cluster_monitor_test_objects`, linked into
`sr_extension_tests`. `sr test` runs them with the rest; `sr run sr_extension_tests --
--gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## compile_cluster_monitor_log_category.cpp

The file compiles `cluster_monitor_log_category.hpp` on its own, so the header carries every include it needs.

## test_cluster_monitor_step.cpp

The file tests the pure step. It needs no thread, no transport and no clock: every time is a fixed point
passed in.

| Test | What it proves |
| --- | --- |
| `APassThatFoundNothingDoesNothing` | No finding gives no action and no re-offer. |
| `ASilentWorkerGetsAGoodbyeAndALateHandshakeOnlyAClose` | The silent workers are listed for GOODBYE and close, the late handshakes for a close alone. |
| `EveryReapedTaskIsCountedUnderItsCause` | The three causes have their own counts, and a reap asks for the held nodes to be offered again. |
| `OneWorkerGetsOneWarningPerPass` | Two tasks of one worker give one warning; the counts still hold both. |
| `AWorkerIsWarnedAgainOnlyAfterASecond` | A warning window of one second per worker. |
| `ThePeriodIsTheConfiguredOneWhileTheTimeoutsAllowIt`, `AQuarterOfTheLivenessTimeoutCapsThePeriod`, `ATighterTaskDeadlineCapsThePeriodAndZeroDoesNot`, `ThePeriodNeverFallsBelowOneMillisecond` | The period rule of `pass_period()`. |
| `TheNextPassIsTheEarliestOfThePeriodTheHoldAndTheDeadline` | The wake time of `next_pass_at()`. |
| `EachCauseHasItsOwnWords` | The three causes read differently in a log line. |

## test_cluster_monitor.cpp

The file tests `ClusterMonitor` over the real directory, table and holds, a `RecordingTransport` and a
`ManualClock`. A worker that registered needs a full handshake, so the pass with a PING to an alive worker
is checked in the cluster tests of `distributed`.

| Test | What it proves |
| --- | --- |
| `Small_ClusterMonitor.AnEmptyPassSendsAndClosesNothing` | A pass over empty bricks sends nothing, closes nothing and counts nothing. |
| `Small_ClusterMonitor.APeerPastTheHandshakeLimitIsClosed` | The peer is closed when the clock passes the handshake limit and not before. |
| `Small_ClusterMonitor.AnUnacknowledgedTaskIsTakenBackAndCounted` | The task is handed back to the sink and counted as unacknowledged once the clock passes the limit. |
| `Small_ClusterMonitor.TheLivenessSetterChangesTheNextPass` | A shorter liveness timeout is the limit of the next pass. |
| `Small_ClusterMonitor.ThePeriodFollowsTheTightestTimeout` | `period()` follows the setters. |
| `Medium_ClusterMonitorThread.TheThreadClosesAPeerPastTheHandshakeLimitAndStops` | The started thread repeats the pass on the machine clock and `stop()` joins it. |

## test_unsent_frame_log.cpp

The file tests `Detail::log_unsent_frame`, which the monitor calls for each `PING` and `GOODBYE`.

| Test | What it proves |
| --- | --- |
| `Small_UnsentFrameLog.AQueuedFrameLogsNothing` | A frame the transport queued writes no record. |
| `Small_UnsentFrameLog.ARefusedFrameLogsOneDebugLine` | A refused frame writes one `DEBUG` in `cluster_monitor` with the frame, the peer and the reason. |
