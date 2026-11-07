# master_link tests

The tests of the `master_link` module, mirroring `modules/extension/master_link/`.
`tests/extension/master_link/CMakeLists.txt` builds them into `sr_master_link_test_objects`, linked into
`sr_extension_tests`. `sr test` runs them with the rest; `sr run sr_extension_tests --
--gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## compile_master_link_log_category.cpp

The file compiles `master_link_log_category.hpp` on its own, so the header carries every include it needs.

## test_link_tick.cpp

The file tests the pure step with fixed points of time.

| Test | What it proves |
| --- | --- |
| `AnIdleLinkWaitsForAChange` | Nothing is due and no wake is asked for. |
| `ARegisteredLinkWakesAtTheEarlierOfTheHeartbeatAndTheWatch`, `ADueHeartbeatIsSentAndTheNextIsAnIntervalAway` | The heartbeat schedule, with and without a liveness watch. |
| `ASilentMasterIsClosedAtTheLivenessTimeout`, `AClosingLinkTakesNoSecondAction` | The watch closes once, at the timeout, and the link then waits for the loss. |
| `AHandshakeWithNoWelcomeRestarts` | `HELLO` is sent again after one interval, from `HelloSent` and from `ProofSent`. |
| `AnEndedLinkStopsItsTransportOnce`, `ALeavingLinkDoesNothing` | An ended link stops its transport once; a leaving one is left alone. |

## test_master_link.cpp

The file tests `MasterLink` over a worker `InProcessTransport` endpoint on an `ExecutorDrive` and a `ManualClock`: the test
calls the `on_*` events and `step()`, settles the fabric, and reads the frames sent and the
connections closed at the master end (`TestSupport::ExecutorFabric`).

| Test | What it proves |
| --- | --- |
| `Small_MasterLink.AConnectionOpensTheHandshakeWithHello` | `HELLO` carries the name and the build id, and the link is not registered. |
| `Small_MasterLink.TheWorkerProvesItselfOnlyAfterTheMasterProved` | `PROOF` follows a good `CHALLENGE`, carries the worker proof, the ops, the job bound and the cache budget, and `WELCOME` registers. |
| `Small_MasterLink.AMasterProvingAnotherKeyEndsTheLink` | A `CHALLENGE` proved with another key ends the link as `RejectedAuthentication`, sends no `PROOF` and closes the peer. |
| `Small_MasterLink.AnEndedLinkStopsItsTransport`, `AConnectionAfterTheEndIsClosed` | An ended link stops its transport, which then refuses a send and connects no more, and a later connection gets no `HELLO` and is seen closed at the master end. |
| `Small_MasterLink.PingGetsNoPongBeforeRegistration`, `ThePongCarriesTheFreeMemoryOfTheProbe` | A `PING` before `WELCOME` gets no `PONG`; after it the `PONG` carries the token and the probe's free memory. |
| `Small_MasterLink.ALateChallengeForAForgottenHelloIsIgnored`, `AChallengeForAnEarlierHelloIsIgnored` | A `CHALLENGE` for a forgotten or an earlier `HELLO` is ignored and does not end the link. |
| `Small_MasterLink.ARegisteredLinkSendsAHeartbeatEveryInterval` | The `HEARTBEAT` carries what the probe returns and is sent once per interval. |
| `Small_MasterLink.ASilentMasterIsClosedAndTheLinkMayReconnect`, `AFrameFromTheMasterRestartsTheSilence` | The master is closed after the liveness timeout of silence, a frame restarts the count, and the lost link does not end. |
| `Small_MasterLink.ANewSessionIsToldFromAKnownOne` | The same session after a reconnect is `Registered`, another is `RegisteredNewSession`, and the reconnects are counted. |
| `Small_MasterLink.ALossAfterGoodbyeIsTheWorkersOwn` | After `say_goodbye()` a loss ends nothing, and `stop()` ends the link as `Stopped`. |
| `Small_MasterLink.ARejectForTheSecretOrTheVersionEndsTheLink`, `AnAbandonedConnectEndsByWhetherTheLinkEverRegistered`, `AFactAbove255BytesIsNamed` | The ends a `REJECT`, an abandoned connect and an oversized fact give. |
| `Medium_MasterLinkThread.TheThreadSendsHeartbeatsUntilStopped` | The started thread sends `HEARTBEAT`s on the machine clock and `stop()` joins it. |
