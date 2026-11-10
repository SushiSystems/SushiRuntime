# offload_coordination tests

The tests of the `offload_coordination` module, mirroring `modules/extension/offload_coordination/`.
`tests/extension/offload_coordination/CMakeLists.txt` builds them into
`sr_extension_tests`. `sr test` runs them with the rest; `sr run
sr_extension_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_offload_dispatcher.cpp

The file tests `OffloadDispatcher`
over a real `BufferRegistry`, `WorkerDirectory` and `PendingTasks`. The transport is the master
threadless endpoint of an `InProcessFabric`, with a threadless worker endpoint whose `RecordingTransportEvents` keep each
dispatch the master sent. Both endpoints post their pump rounds to one `SeededExecutor` and read a
`ManualClock`; a test steps the executor, then reads the dispatches after a `PING` queued behind them
has arrived, since control frames to one peer keep their order. The policy is `InMemoryOffloadPolicy`, which picks the
first worker offered and counts its calls, and the sink is `InMemoryOffloadSink`. One test runs on a
master whose control queue holds no frame, so every send answers `QueueFull`. Each worker registers through `accept_hello` and
`accept_proof` under the empty secret; every test registers one, named `worker`, and one test adds
a second, named `second`. A buffer is a `std::array<float, 16>` whose address is
shared; no byte of it is read, so the file needs no device and starts no thread, and every test is
small.

| Test | What it proves |
| --- | --- |
| `AttemptCapDeclinesWithoutClearingOffloadable` | With `attempts` at `K_MAXIMUM_OFFLOAD_ATTEMPTS` the dispatcher returns false before it asks the policy, and `offloadable` and the count stay as they were. `complete_local` sets the count to 0 and the next `try_offload` dispatches. This pins finding N3: the cap holds for one run, not for good. |

### `RefusedSendUndoesThePendingEntry`

The master's control queue holds no frame, so the transport answers `QueueFull`. The pending table is
empty, the worker's inflight count is 0, `try_offload` returned false and `send_queue_full` and
`declined` are 1. The output keeps the version the frame named, one above the version before the
call.

| Test | What it proves |
| --- | --- |
| `StoppedCoordinatorDeclines` | `DistributedCoordinator::stop()` closes the pending table, then the directory. After the first close the policy is asked once, `insert` refuses and the worker's inflight count goes back to 0. After the second the policy is not asked. No frame reaches the transport in either state and `offloadable` stays set. |
| `DynamicNodeResolvesItsLiveLists` | A node with `live_reads` and `live_writes` gets no payload from `prepare`. Its dispatch names the buffers of the live lists; after the two lists swap, the next dispatch names them the other way round, though `io_reads_` and `io_writes_` did not change. `complete_local` then raises the version of the buffer in `live_writes` and leaves the other alone, which pins finding D3. |

### `PrepareWarnsOnceForLocalOnlyOps`

One `prepare` over an offloadable node, a node whose operation no worker has, a node with an
unshared output, an untagged node and a null slot logs one WARN, captured through
`set_log_sink`. Its text is `2 of 3 op-tagged nodes will run locally: 1 no live worker for
the op, 1 buffer not shared, 0 buffer written twice (first: op N)`, where N is the id of the
operation no worker has. A second `prepare` over the offloadable node alone logs nothing.

| Test | What it proves |
| --- | --- |
| `LocalNodeBumpsOnlyItsSharedWrites` | A node that stays local and writes a shared buffer keeps that buffer's handle, and `complete_local` raises its version. A node that writes no shared buffer gets no `OffloadState`, so its completion returns after one null check and takes no lock. |

### `NodeThatWritesOneBufferTwiceStaysLocal`

A fixed-buffer node and a double-buffered node each list one buffer twice among their writes.
Both stay local, and the one WARN counts them under the third reason, `2 buffer written twice`. A
node that reads and writes the same buffer names it once as an output and stays offloadable. The
local run of the first node still raises the version of the buffer it wrote.

| Test | What it proves |
| --- | --- |
| `Regression_EachPlanPlacesByItsOwnRules` | Two nodes stand for two plans: the first is prepared under a set whose rule names `worker`, the second, after it, under a set whose rule names `second`, both at position 0. Offered in turn twice, each node is dispatched to the worker its own set names and the policy is never asked. |

## test_worker_directory.cpp

The file tests `WorkerDirectory` over the real `LeastLoadedOffloadPolicy`. It needs no hardware and no
transport: the test plays the worker's side of the handshake with fixed nonces and a secret of the
bytes 0 to 31, computes proofs with `compute_proof` of `handshake.hpp`, and passes every time in as a
fixed point. `Small_WorkerDirectory` holds memory and one mutex; `Medium_WorkerDirectory` holds the
tests that start a second thread that waits for a wake. One promise is one test, so the table lists each
group by the names it holds.

| Group | Tests | What they prove |
| --- | --- | --- |
| Admission | `Small_WorkerDirectory.AdmitsNothingFromAPeerItNeverAdded`, `Small_WorkerDirectory.AdmitsOnlyHelloAndGoodbyeFromAPeerThatOwesHello`, `Small_WorkerDirectory.AdmitsNoWorkerMessageFromAPeerThatOwesProof`, `Small_WorkerDirectory.AdmitsEveryTypeButProofFromARegisteredWorker`, `Small_WorkerDirectory.AdmitsNothingFromARemovedPeer` | A peer the directory never added gets no message type through `admits`. A peer that owes `HELLO` gets `HELLO` and `GOODBYE`; one that owes `PROOF` gets those and `PROOF`, so a `BUFFER_REQUEST` or `BUFFER_CHUNK` before the proof is dropped unread (finding DIST-03). A registered worker gets every type but `PROOF`, and a removed peer gets none. |
| Handshake deadline | `Small_WorkerDirectory.HandshakeOverdueListsNobodyAtTheLimitAndThePeerOneMillisecondAfter`, `Small_WorkerDirectory.HandshakeOverdueCountsFromTheConnectOfEachPeer`, `Small_WorkerDirectory.HandshakeOverdueIgnoresTheHelloASlowPeerKeepsSending`, `Small_WorkerDirectory.HandshakeOverdueNeverListsARegisteredWorker`, `Small_WorkerDirectory.HandshakeOverdueListsEachPeerOnce`, `Small_WorkerDirectory.RemovePeerReportsAnUnregisteredPeerAsKnownWithItsNameAndEndpoint`, `Small_WorkerDirectory.RemovePeerOfAPeerAlreadyRemovedFindsNothing` | Finding DIST-04. A peer that has not registered is listed one millisecond after the limit, counted from its connect and not from a later `HELLO`, and once. A registered worker is never listed. `remove_peer` reports an unregistered peer with its name and endpoint, and a peer already removed as unknown. |
| Waiting for workers | `Small_WorkerDirectory.WaitForZeroWorkersIsReachedAtOnce`, `Small_WorkerDirectory.WaitForAWorkerNobodyRegisteredTimesOut`, `Small_WorkerDirectory.WaitForWorkersIsReachedAtOnceOnceTheCountIsAlive`, `Medium_WorkerDirectory.WaitForWorkersWakesOnRegistration`, `Medium_WorkerDirectory.CloseWakesAWaiterWhoseCountWasNotReached`, `Small_WorkerDirectory.ClosedDirectoryReachesNoCountItHeldWhenItClosed`, `Small_WorkerDirectory.ClosedDirectoryReachesNoCountEvenZero` | Finding DIST-10. The two tests that wake a waiter thread are medium; the rest ask with a wait of no time and are small. A thread waits with a limit of 24 hours while the test registers a worker or calls `close()`; the limit is never reached, so a wake that never comes hangs the test until the program timeout. A closed directory reaches no count, zero included. |
| PROOF and HELLO | `Small_WorkerDirectory.HelloReplyCarriesTheMasterProofOfTheNonces`, `Small_WorkerDirectory.ProofMadeUnderAnotherSecretFailsAuthenticationAndRegistersNobody`, `Small_WorkerDirectory.RefusedProofLeavesThePeerOutOfTheWorkerMessages`, `Small_WorkerDirectory.RefusedProofSpendsItsNoncesSoTheRightProofComesTooLate`, `Small_WorkerDirectory.MasterProofSentBackDoesNotPassAsTheWorkerProof`, `Small_WorkerDirectory.ProofUnderAnEmptyKeyFailsAuthentication`, `Small_WorkerDirectory.HelloFromAnUnknownPeerAnswersUnknownPeer`, `Small_WorkerDirectory.ProofFromAnUnknownPeerAnswersUnknownPeer`, `Small_WorkerDirectory.SecondHelloAnswersTheMasterProofOfItsOwnNonces`, `Small_WorkerDirectory.ProofForTheNoncesOfTheFirstHelloFailsAfterASecondHello`, `Small_WorkerDirectory.ProofForTheNoncesOfTheSecondHelloRegistersTheWorker`, `Small_WorkerDirectory.ProofFromARegisteredWorkerAnswersOutOfOrderAndChangesNothing` | A proof made under another secret, under an empty key, or the master proof sent back is refused with `AuthenticationFailed`, and a refused proof spends its nonces. A second `HELLO` answers the master proof of its own nonces, and only a `PROOF` for those nonces registers the peer. Unknown peers answer `UnknownPeer`. A `PROOF` from a registered worker is `OutOfOrder`. |
| Silent workers | `Small_WorkerDirectory.TakeSilentLeavesAWorkerThatIsAtTheLimit`, `Small_WorkerDirectory.TakeSilentListsOnlyTheWorkerWithoutAFrameWithinTheLimit`, `Small_WorkerDirectory.TakeSilentStopsCountingTheWorkerAsAlive`, `Small_WorkerDirectory.TakeSilentListsAWorkerOnce`, `Small_WorkerDirectory.TakenWorkerLeavesTheWorkerList`, `Small_WorkerDirectory.KernelsOfATakenWorkerStopCountingAndTheOthersStay`, `Small_WorkerDirectory.SupportsNoOperationNoWorkerSent`, `Small_WorkerDirectory.PickSkipsATakenWorker`, `Small_WorkerDirectory.LateFrameDoesNotBringATakenWorkerBack`, `Small_WorkerDirectory.RemovePeerReportsATakenWorkerAsRegistered`, `Small_WorkerDirectory.RemovePeerDoesNotUncountATakenWorkerAgain`, `Small_WorkerDirectory.EverySilentWorkerTakenLeavesNothingToPick` | A worker silent for longer than the liveness limit is listed once and stops counting at that moment: the alive count drops, it leaves `list_workers`, its operations stop counting toward `supports`, and `pick` skips it. A late frame does not bring it back, and `remove_peer` does not lower the count a second time. |
| Picks | `Small_WorkerDirectory.PickWithNoWorkerReturnsNothing`, `Small_WorkerDirectory.PickKeepsATaskLocalBelowTheByteThresholdOfThePolicy`, `Small_WorkerDirectory.PickCountsTheTaskAgainstThePickedWorker`, `Small_WorkerDirectory.PickSkipsAWorkerAtItsJobLimit`, `Small_WorkerDirectory.PickReturnsNothingWhenEveryWorkerIsAtItsJobLimit`, `Small_WorkerDirectory.ReleaseInflightFreesOneSlotAndASecondReleaseLowersNothing`, `Small_WorkerDirectory.PickOnAClosedDirectoryReturnsNothing`, `Small_WorkerDirectory.PickOffersATaskToTheWorkerThatHoldsItsKernel`, `Small_WorkerDirectory.PickDoesNotFallBackToAWorkerWithoutTheKernelWhenTheHolderIsFull`, `Small_WorkerDirectory.PickOffersNoWorkerATaskWithNoOperation` | Every pick runs the real `LeastLoadedOffloadPolicy` with a threshold of 512 bytes, so the tests assert the worker picked and never a call record. A task below the threshold stays local. A worker at its `max_jobs` is skipped, and when every worker is full nothing is picked. One `release_inflight` frees one slot and a second lowers nothing. Only a worker that holds the kernel is offered the task, and a full holder is not replaced by a worker without it. Each test builds its node with `Internal::Graph::Node`, because `WorkerDirectory::pick` takes one. |
| Snapshots | `Small_WorkerDirectory.SnapshotListsOnlyAPeerThatFinishedTheHandshake`, `Small_WorkerDirectory.SnapshotReportsWhatTheWorkerSent`, `Small_WorkerDirectory.SnapshotCarriesTheFactsOfTheWorkersHello` | A peer that has not finished the handshake is no worker in the snapshot. A worker's entry holds its name, endpoint, inflight count, and the round trip, free memory, queue depth and cache size it sent, and the facts of its `HELLO`; `ms_since_last_frame` is the time from its last frame to the snapshot. |
| Facts | `Small_WorkerDirectory.TargetNamingTheMachineLabelAndDeviceOfAProvenHelloPicksItsWorker`, `Small_WorkerDirectory.TargetNeedingMoreDeviceMemoryThanTheHelloReportedIsAbsent`, `Small_WorkerDirectory.HelloThatHasNotProvedTheSecretReplacesNoFacts`, `Small_WorkerDirectory.ProvenHelloOfTheSameNameReplacesTheFactsOfTheLostWorker` | The facts from a `HELLO` are read through `pick_target`, the only public reader. A target that names the machine, its `zone` label and a GPU picks the worker of a proven `HELLO`, and one that needs more device memory than the `HELLO` reported finds none. A `HELLO` under the same name that has not proved the secret replaces nothing; a proven one replaces the facts of a worker already lost. |
| Identity | `Small_WorkerDirectory.IdentityOfAnUnknownPeerIsEmpty`, `Small_WorkerDirectory.IdentityOfAPeerThatOwesProofIsEmpty`, `Small_WorkerDirectory.IdentityCarriesTheBuildOfTheLastHello`, `Small_WorkerDirectory.IdentityCarriesTheNameAndEndpointOfARegisteredWorker`, `Small_WorkerDirectory.IdentityIsEmptyOnceThePeerIsRemoved` | `identity` returns nothing for a stranger, for a peer that owes `PROOF` and for a removed peer. A registered worker has the name and endpoint of its connect and the build id of its last `HELLO`. |
| Repeated HELLO | `Small_WorkerDirectory.RepeatedHelloFromARegisteredWorkerAnswersAlreadyRegistered`, `Small_WorkerDirectory.RepeatedHelloKeepsTheWorkerCountedAndItsKernelsSupported`, `Small_WorkerDirectory.RepeatedHelloDoesNotReopenTheHandshake`, `Small_WorkerDirectory.RepeatedHelloKeepsATaskThatIsOut`, `Small_WorkerDirectory.RepeatedHelloLeavesTheIdentityTheWorkerRegisteredWith` | A registered worker that lost its `WELCOME` says `HELLO` again: the answer is `AlreadyRegistered`, the worker stays counted with its kernels, the handshake does not reopen, a task that is out stays counted, and the identity keeps what it registered with. |
| close() | `Small_WorkerDirectory.CloseLeavesNoWorkerAlive`, `Small_WorkerDirectory.CloseEndsSupportForEveryOperation`, `Small_WorkerDirectory.CloseEmptiesTheWorkerList`, `Small_WorkerDirectory.CloseEmptiesTheSnapshot`, `Small_WorkerDirectory.ClosedDirectoryRegistersNoPeer`, `Small_WorkerDirectory.RemovePeerAfterCloseReportsTheWorkerAsRegistered`, `Small_WorkerDirectory.RemovePeerAfterCloseDoesNotUncountTheWorkerTwice`, `Small_WorkerDirectory.SecondCloseDoesNotUncountTheWorkerTwice` | Finding QC-36. After `close()` the alive count is 0, no operation is supported, `list_workers` and `snapshot` are empty, and a peer that then sends a valid `HELLO` and `PROOF` gets `OutOfOrder`. `remove_peer` still reports a worker as registered, since the coordinator counts the loss from that answer, and neither it nor a second `close()` lowers the count again. |

## test_offload_handles.cpp

The file tests OffloadHandles: the inputs come first, the outputs after them.

| Test | What it proves |
| --- | --- |
| `Small_OffloadHandles.InputsThenOutputs` | The two views split the handles at the input count. |

## test_task_description.cpp

The file tests describe_task(): the name, the operation and the parameters up to the last non-zero
one. The header is pure, so every test is small.

| Test | What it proves |
| --- | --- |
| `Small_TaskDescription.OnlyTheParametersUpToTheLastNonZeroTravel` | Trailing zero parameters are not sent and a zero inside the list is; a task with no parameter sends none. |
| `Small_TaskDescriptionFields.TheNameAndTheOperationTravel` | The description carries the task's name and operation. |

## test_rule_placement.cpp

The tests run `RulePlacement` over a directory where no worker registers, on a master named
`master` with the label `zone=a`, at fixed points of the steady clock. A target that names a
worker is covered end to end in `tests/extension/distributed/README.md`.

| Test | What it proves |
| --- | --- |
| `Small_RulePlacement.RuleForTheMasterRunsLocally` | A rule whose label names the master gives `RunLocally`; a rule for another machine does not name the master. |
| `Small_RulePlacement.LocalRunsAnAbsentTargetOnTheMaster` | An absent target under `Local` gives `RunLocally`. |
| `Small_RulePlacement.WaitEndsAtTheBoundFromTheFirstOffer` | Under `Wait` the end stays first offer plus bound across offers, and an offer at that end gives `RunLocally`. |
| `Small_RulePlacement.FailNamesTheRuleAndTheTarget` | Under `Fail` the error is `InvalidState` and names the rule, else its position, and the target. |
| `Small_RulePlacementDevice.Regression_ADeviceOnlyRuleTheMasterLacksIsNotTheMasters` | On a master with one CPU, a rule for a GPU or for device 1 does not name the master, and a rule for a CPU does. |
| `Small_RulePlacement.Regression_AHugeWaitBoundWaitsUntilTheLatestTime` | A `Wait` bound of `milliseconds::max()` waits until `steady_clock::time_point::max()` instead of overflowing the sum. |
| `Small_RulePlacement.FailRefusesANodeThatCannotTravel` | Under `Fail` a node that cannot travel gets `InvalidArgument` or `InvalidState` by cause, and the message names the rule. |
| `Small_RulePlacementUnreachable.RunsANodeThatCannotTravelOnTheMaster` | Under `Local` and under `Wait` a node that cannot travel runs on the master at once and is not held. |

## test_held_nodes.cpp

The tests hold real nodes that no graph runs, at fixed points of the steady clock.

| Test | What it proves |
| --- | --- |
| `Small_HeldNodes.FirstOfferIsKeptUntilForgotten` | A node's first offer stays until `forget()`. |
| `Small_HeldNodes.TakeExpiredReturnsOnlyTheEndedWaits` | `take_expired()` returns the waits that ended and leaves the rest. |
| `Small_HeldNodes.EarliestFollowsTheRemainingHolds` | `earliest()` is the end of the soonest remaining wait, or the clock's maximum when nothing is held. |
| `Small_HeldNodes.TakeAllReturnsTheRestAndEmptiesTheTable` | `take_all()` returns the rest of the held nodes and empties the table. |
| `Small_HeldNodes.CloseRefusesALaterHold` | After `close()` a hold is refused. |
| `Small_HeldNodes.DrainReturnsTheHeldNodes` | `drain()` returns the held node and empties the table. |
| `Small_HeldNodes.DrainForgetsTheFirstOffers` | After `drain()` the next `first_offer()` of a returned node records the new time. |
| `Small_HeldNodes.HoldAfterAReleaseIsStale` | A hold whose decision came before a `take_all()` is refused as `Stale` and keeps nothing; one made after it is held. |

## test_task_resubmission.cpp

The test hands `reoffer_nodes()` a real node that no graph runs and a `RefusingOffloadSink`, whose
`resubmit()` throws, at fixed points of the steady clock.

| Test | What it proves |
| --- | --- |
| `Small_TaskResubmission.Regression_ARefusedReofferForgetsTheFirstOffer` | A node whose first offer `HeldNodes` recorded is refused by the sink once: `reoffer_nodes()` releases its scheduling reference, and the next `first_offer()` records the new time instead of the old one. |

## compile_offload_coordination_log_category.cpp

The file includes `offload_coordination_log_category.hpp` alone, so the header carries every include it
needs. The header holds a constant, which has no behaviour of its own to test.
