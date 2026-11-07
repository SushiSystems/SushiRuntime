# profiling tests

The tests of the `profiling` module, mirroring `modules/surface/profiling/`. The module's test list builds them into
`sr_surface_tests` and the benchmark into its own program. `sr test` runs them with the rest;
`sr run sr_surface_tests -- --gtest_filter='<pattern>'` runs one program alone. The tests link SushiProfiler.

`register_profiler_parts.hpp` is the one door a host uses, so every test that drives the runtime through a profiler session, an observer
or a zone is an aspect of it and lives in the folder `test_register_profiler_parts/`, one file per aspect. That includes the tests of the
runtime's zones in the plan build, the run, the allocator and the lock wait, and of the observers every runtime fans its runs out to,
since reading a zone needs a SushiProfiler session and this is the one test folder that has it.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_register_profiler_parts/session_pipeline.hpp

`SessionPipeline` is a runtime and a graph of three kernel nodes named `load`, `scale` and `store`, with operations, and costs on the first
two (the second a stated zero of floating-point work). `InferencePipeline` is a stack of four layers of a matmul-like and a relu node
over weights no node writes, the shape of an inference pass. `record_periods()` registers the runtime, starts a session with one warm-up
and a number of active periods of the timeline `run.step`, marks the timeline once per period, calls the test's function for each, and
returns the record; `record_periods_without_source()` does the same with no source, so no observer is added to the runtime.

A session test marks periods around blocking runs, so every span of a run is queued before the period closes and the consumer's
`linger_ms` of 0 loses nothing. The one test that waits for the consumer thread is
`AStepWhoseWorkFinishedAfterTheLingerIsCountedInTheDrops`, which holds a step's work for 2.5 s, past the 2 s maximum delay the source asks the consumer to keep a period open.

## test_register_profiler_parts/observed_pipeline.hpp

`ObservedPipeline` is the same three-node graph in the namespace `ObserverTests`, for the tests that add observers and read observations
with no session.


## test_span_loss_tally.cpp

SpanLossTally, the count of lost device spans by cause and its throttled report.

| Test | What it proves |
| --- | --- |
| `Small_SpanLossTally.NamesEachCauseWithItsCountInOneLine` | A report names each cause with spans lost, a run's drop counts included, and leaves out a cause with none. |
| `Small_SpanLossTally.SaysNothingWhenNoSpanWasLost` | No report comes when nothing was lost. |
| `Small_SpanLossTally.ReportsAtMostOncePerInterval` | At most one report comes per steady interval, and the counts held back arrive after it. |
| `Small_SpanLossTally.AReportClearsTheCountsItNamed` | A report clears the counts it named, so the next one names only what came after. |


## test_run_queue.cpp

RunQueue, the bounded hand-over of observations from the runs' threads to the consumer.

| Test | What it proves |
| --- | --- |
| `Small_RunQueue.TakesEveryObservationOldestFirst` | A queue that holds some observations hands them over oldest first. |
| `Small_RunQueue.DropsTheOldestWhenFull` | A full queue drops the oldest observations and keeps the newest. |
| `Small_RunQueue.CountsTheRecordsOfTheObservationsItDropped` | The overflow count is the records of the dropped observations, and taking it starts it again. |
| `Small_RunQueue.ACapacityOfZeroHoldsOne` | A queue of capacity zero still holds one observation. |
| `Small_RunQueue.DiscardDropsWhatWasNeverCollected` | Discard() drops what was never taken and says how many that was. |
| `Small_RunQueue.ADiscardIsNotAnOverflow` | A discard at the end of a session is not an overflow. |
| `Medium_RunQueue.SeveralThreadsMayPushWhileOneTakes` | Several threads may push while one takes, with nothing lost within the capacity. |


## test_run_feed.cpp

RunFeed, the observer that puts a runtime's runs on the source's queue.

| Test | What it proves |
| --- | --- |
| `Small_RunFeed.QueuesEachObservationTaggedWithItsRuntime` | Feeds of one queue queue each observation they hear tagged with their own runtime. |
| `Small_RunFeed.AFeedKeepsItsQueueAlive` | A feed keeps its queue alive, so an observation that arrives after the owner let go is queued. |


## test_clock_conversion.cpp

The conversion of a device's event clock to the steady clock through its pairing.

| Test | What it proves |
| --- | --- |
| `Small_ClockConversion.AnEventTimeMapsByItsDistanceFromThePairedInstant` | An event time maps to the steady reading offset by its distance from the paired instant. |
| `Small_ClockConversion.ATimeOneNanosecondPastTheOriginReadsOne` | The event time one nanosecond past the steady origin reads 1, not 0. |
| `Small_ClockConversion.ATimeBeforeTheOriginReadsZero` | A time before the steady clock's origin reads 0 and does not wrap. |
| `Small_ClockConversion.AnEventClockFarAheadOfTheSteadyClockConverts` | The pairing's event clock may be far larger than the steady one, as a GPU counter is. |
| `Small_ClockConversion.SaturatesTheUncertaintyAt32Bits` | An uncertainty that fits 32 bits is kept and one that does not saturates. |


## test_device_track_name.cpp

The functions that name the tracks of the device source.

| Test | What it proves |
| --- | --- |
| `Small_DeviceTrackName.NamesTheNodeTheIndexAndTheDevice` | The track names the node, the device index and the device. |
| `Small_DeviceTrackName.TwoDevicesOfOneNameGetDifferentTracks` | Two devices of one name on one node get different tracks. |
| `Small_DeviceTrackName.TwoNodesWithTheSameDeviceGetDifferentTracks` | Two nodes with the same device get different tracks. |
| `Small_DeviceTrackName.TheHostTrackNamesTheNode` | The host track names the node. |
| `Small_DeviceTrackName.TheHostTrackDiffersFromADeviceTrack` | The host track is not the track of a device called host, whatever its index. |
| `Small_DeviceTrackName.ADeviceTheListHoldsIsNamedByItsName` | A device the list holds is named by its own name. |
| `Small_DeviceTrackName.AnIndexTheListLacksNamesAGenericDevice` | An index the list lacks names a device called "device". |
| `Small_DeviceTrackNameOfARuntime.TheRuntimeOverloadAgreesWithThePartsOverload` | The runtime overload builds the same string from the runtime's own node and device. |
| `Small_DeviceTrackNameOfARuntime.AnIndexTheRuntimeLacksNamesAGenericDevice` | An index the runtime does not have names a device called "device". |


## test_device_source.cpp

DeviceSource over SushiProfiler's own SourceContext, SpanSink and SpanTable, with no device.

| Test | What it proves |
| --- | --- |
| `Small_DeviceSource.ConvertsEventTimesThroughThePairing` | Event times go through the device's pairing onto the record's clock. |
| `Small_DeviceSource.SetsNodeInvocationAndOperationAttributes` | The integer attributes every span carries: node, invocation and operation. |
| `Small_DeviceSource.ASpanNamesTheLayerThatRanIt` | A span names the layer that ran it: the scheduler by default, the chain when the run took it. |
| `Small_DeviceSource.OmitsAWidthThatIsUnknown` | A launch width and a live width that are unknown are left out, and a known one is written. |
| `Small_DeviceSource.OmitsAnOperationThatNamesNone` | An operation of 0 names none and is left out. |
| `Small_DeviceSource.WritesTheCostOfANodeThatStatesOne` | A node that stated a cost writes flops, bytes and its dtype. |
| `Small_DeviceSource.WritesAStatedZeroCost` | A stated zero cost is written as zeros, with no dtype when none was named. |
| `Small_DeviceSource.OmitsAnAbsentCost` | A node that stated no cost writes no cost attribute. |
| `Small_DeviceSource.PutsAHostNodeOnTheHostTrack` | A host node goes on the runtime's host track on the steady clock, with no uncertainty. |
| `Small_DeviceSource.ASpanOfALaterPeriodWaitsForIt` | A span that starts in a later period waits for it. |
| `Small_DeviceSource.ASpanOfAClosedPeriodIsDroppedLate` | A span that started before the period is dropped late and not added. |
| `Small_DeviceSource.ForwardsTheDropsOfARun` | The drops a run carries reach the sink as late spans, and a run with none forwards none. |
| `Small_DeviceSource.AMissingPairingDropsTheSpan` | A record of a device with no pairing is dropped and counted. |
| `Small_DeviceSourceQueue.TheQueuesOverflowIsCounted` | The records a full queue dropped are counted too. |
| `Small_DeviceSource.ARecordWithNoRowIsNamedByItsIndex` | A record with no table row is named by its index. |
| `Small_DeviceSource.AnUnknownRuntimeIsSkipped` | An observation of a runtime the source was not given is skipped. |
| `Small_DeviceSource.TwoGraphsWithEqualNodeIndicesDifferByGraph` | Two graphs whose nodes share registration indices are told apart by their labels. |
| `Small_DeviceSource.WritesTheOriginOfANodeThatStatesOne` | A node that states an origin has it written as the origin attribute. |
| `Small_DeviceSource.OmitsTheOriginWhenEmpty` | An empty origin writes no origin attribute. |
| `Small_DeviceSource.OmitsTheGraphWhenTheLabelIsEmpty` | An empty graph label writes no graph attribute. |
| `Small_DeviceSource.TheLabelOfATableIsInternedOnceForAllItsRuns` | The label of one table is interned once however many runs of it arrive. |
| `Small_DeviceSourceRuntimes.TwoRuntimesGetTracksNamedByTheirNodes` | Two runtimes get tracks named by their own nodes. |
| `Small_DeviceSource.EndStopsTheDelivery` | Begin() starts the delivery and end() stops it. |
| `Small_DeviceSource.EndDropsWhatWasNeverCollected` | End() drops the observations that were never collected. |
| `Small_DeviceSourceStart.BeginReturnsTheRefusalOfTheAttachment` | A source whose delivery cannot start returns why. |
| `Small_DeviceSource.ReportsTheMaxDelayItWasGiven` | The source reports the max delay it was given. |
| `Small_DeviceSource.TheDefaultMaxDelayIsWithinTheProfilersLimit` | The default max delay is positive and within the limit the profiler accepts. |


## test_in_memory_feed_attachment.cpp

An `InMemoryFeedAttachment` records each `attach()` and `detach()` and starts nothing.

| Test | What it proves |
| --- | --- |
| `RecordsEachCallInArrivalOrder` | Attach and detach calls are kept in the order they are made. |
| `AttachAcceptsWhenNoRefusalIsSet` | `attach()` returns an empty refusal until one is set. |
| `AttachReturnsTheRefusalItWasSet` | `attach()` returns the refusal set, on every call. |

## test_runtime_environment.cpp

The facts the runtime states about its machine, its devices and its build.

| Test | What it proves |
| --- | --- |
| `Small_RuntimeEnvironmentOfARuntime.StatesTheNumberOfDevices` | The record states how many devices the runtime drives. |
| `Small_RuntimeEnvironmentOfARuntime.StatesEveryDeviceOfTheRuntime` | The record states every device the runtime drives with its class, backend and driver. |
| `Small_RuntimeEnvironmentFacts.StatesTheVersionAndTheWorkers` | The runtime states its version and its worker count under its node's name. |
| `Small_RuntimeEnvironmentRebalancer.StatesTheRebalancer` | The runtime spells the state of its rebalancer on or off. |
| `Small_RuntimeEnvironmentFacts.StatesADeviceWithItsDriver` | A device the runtime reports is stated with its description and its driver. |
| `Small_RuntimeEnvironmentFacts.StatesTheBuildTypeAsABuildFact` | The library's own build type is a build fact, so two records of different builds do not compare. |
| `Small_RuntimeEnvironmentOfARuntime.ABuildFactIsNotAnEnvironmentFact` | A build fact is not stated as an environment fact. |


## compile_in_memory_profiled_runtime.cpp

Compiles `in_memory_profiled_runtime.hpp` on its own; the adapter's behaviour is tested in
`test_profiled_runtime.cpp`.

## test_profiled_runtime.cpp

The `ProfiledRuntime` contract, held by the in-memory adapter small and by the adapter over the program's runtime at medium.

| Test | What it proves |
| --- | --- |
| `Small_InMemoryProfiledRuntime.KeepsEachDeviceAtItsIndexWithItsDriver` | The in-memory runtime keeps its devices at their indices with their drivers. |
| `Small_InMemoryProfiledRuntime.ADeviceWithNoStoredDriverReadsUnknown` | A driver nobody set reads "unknown". |
| `Small_InMemoryProfiledRuntime.AResetHandleRemovesOnlyItsObserver` | A reset handle takes its observer out and leaves the others. |
| `Small_InMemoryProfiledRuntime.AnObserverIsStoodForByAHandle` | The in-memory runtime hands out a handle that stands for its observer. |
| `Small_ContextProfiledRuntime.KeepsEachDeviceAtItsIndexWithItsDriver` | The runtime adapter keeps its devices at their indices with a driver each. |
| `Small_ContextProfiledRuntime.StatesWhatTheRuntimeStates` | The runtime adapter states the node, the devices and the workers the runtime states. |
| `Small_ContextProfiledRuntime.AnObserverIsStoodForByAHandle` | The runtime adapter hands out a handle that stands for its observer. |


## test_profiling_log_category.cpp

The profiling module logs under a category named after its folder.

| Test | What it proves |
| --- | --- |
| `Small_ProfilingLogCategory.TheDeviceSourceLogsUnderTheModulesCategory` | The device source logs its loss report under the module's category in the runtime's project. |


## test_register_profiler_parts/test_registration.cpp

How the runtime registers its source and contributor, and how it adds and removes its observers.

| Test | What it proves |
| --- | --- |
| `Small_RegisterProfilerParts.RefusesASecondRegistration` | The registry refuses a second registration of the same source name. |
| `Small_RegisterProfilerParts.TheRegistryMakesTheDeviceSource` | The registered source is the one the registry knows by the runtime's source name. |
| `Medium_RegisterProfilerParts.ASessionAddsTheObserverWhenItsSourceBegins` | A session adds the observer when its source begins, so a run reaches the source with its records. |
| `Medium_RegisterProfilerParts.ASessionRemovesTheObserverWhenItsSourceEnds` | A session removes the observer when its source ends, so a run after it reaches nothing. |
| `Small_RegisterProfilerPartsOnAHostRuntime.TheDestructorRemovesItsOwnObserverOnly` | The destructor removes only the observer it added, so another observer keeps its runs. |
| `Small_RegisterProfilerPartsOnAHostRuntime.AFurtherRuntimeAttachesBeforeASession` | A second runtime may feed the same source when it attaches before a session starts. |
| `Small_RegisterProfilerPartsOnAHostRuntime.AFurtherRuntimeIsRefusedDuringASession` | A further runtime is refused while a session runs. |
| `Small_RegisterProfilerPartsOnAHostRuntime.RefusedPartsAttachNothing` | Parts whose registration was refused attach nothing. |


## test_register_profiler_parts/test_run_observer.cpp

The observers a runtime fans its finished runs out to, and the device timing they request.

| Test | What it proves |
| --- | --- |
| `Small_RunObserver.SeesEachBlockingRunOnce` | An observer sees each blocking run once. |
| `Small_RunObserver.EachObservedRunHoldsTheNodesOfThePlan` | Each observed run holds the nodes the plan holds and one well-ordered record per dispatch. |
| `Small_RunObserver.TwoObserversBothReceiveEveryRun` | Two observers both receive every run. |
| `Small_RunObserver.RemovingOneLeavesTheOther` | Removing one observer leaves the other receiving runs, and the removed one hears no later run. |
| `Small_RunObserver.ADestroyedHandleRemovesItsObserver` | A handle that goes out of scope removes its observer. |
| `Small_RunObserver.AHandleThatOutlivesItsRuntimeRemovesNothing` | A handle that outlives its runtime removes nothing and does not crash. |
| `Small_RunObserver.AnObserverHearsOnlyItsOwnRuntime` | An observer of one runtime is not called by the runs of another. |
| `Small_RunObserver.SeesAnAsyncStepWhenItsWorkFinishesNotWhenItIsWaitedFor` | An async step is observed when its device work finishes, not when it is waited for. |
| `Small_RunObserver.AnAsyncStepWithNoObserverStillCompletes` | An async step of a runtime with no observer reports to nobody and still completes. |
| `Small_RunObserver.TheFirstRunAfterAnAddOfAPlanBuiltBeforeCarriesRecords` | The first run after an add carries full records even though its plan was built before the add. |
| `Small_RunObserver.AddingAnObserverCostsNoRebuild` | Adding an observer costs no plan rebuild, for the first observer and for later ones. |
| `Small_RunObserver.RemovingObserversCostsNoRebuild` | Removing an observer, the last one included, costs no plan rebuild. |
| `Small_RunObserver.RunsAfterTheLastRemovalReachNoObserver` | The runs after the last observer is removed reach no observer. |
| `Small_RunObserver.AnObserverAddedMidRunGetsNoRecordsOfTheCurrentOne` | The run in flight when an observer is added carries no records, as it chose its timing before. |
| `Small_RunObserver.AnObserverAddedMidRunStillGetsTheTableOfTheCurrentOne` | The run in flight when an observer is added still hands over the table of its plan. |
| `Small_RunObserver.AnObserverAddedMidRunTimesTheNextRun` | The next run after an observer was added midway is timed, with no rebuild. |
| `Small_RunObserver.ARunWithNoObserverMakesNoObservation` | A run after the only observer was removed makes no observation and is unchanged. |
| `Medium_RunObserver.ARunOnTheChainPathCarriesTheChainLayer` | A run on the chain path is observed with the chain layer and its records. |
| `Small_RunObserver.AMultiStepRunIsOneObservationWithEveryStep` | A multi-step run is one observation holding every step's records. |


## test_register_profiler_parts/test_collect_profiling.cpp

What a run's observation holds: the node table, the records, the pairings and the drops.

| Test | What it proves |
| --- | --- |
| `Small_CollectProfiling.TheTableAndTheRecordsJoinByNodeIndex` | Every record joins a table row by its node index, and the rows are the nodes the caller named. |
| `Small_CollectProfiling.TheTableCarriesTheOperationAndTheCostTheCallerStated` | The table rows carry the operation and the cost the caller stated, and no cost where none was. |
| `Medium_CollectProfiling.KernelNodesClaimTheirDevicesPairing` | Each kernel record of a graph names a device the pairing of which is present. |
| `Medium_CollectProfiling.APlanRunOnTheChainFillsTheSameTable` | The table of a plan on the chain path holds the same rows as on the worker pool. |
| `Small_CollectProfiling.CarriesNoDropWhenNoneWasLost` | A run that lost no dispatch carries zero drops. |
| `Small_CollectProfiling.ALossFreeRunLogsNothing` | A run that lost no dispatch logs nothing. |
| `Small_CollectProfiling.CarriesTheDropCountsOfTheRun` | A run longer than its ledger was sized for carries the drops, and a drop plus a record is a dispatch. |
| `Small_CollectProfiling.AnOverflowedRunLogsOneWarn` | A run which overflowed its ledger logs one WARN line for the run, not one per dispatch. |
| `Small_CollectProfiling.TheRunAfterAnOverflowLosesNothing` | The run after an overflow sizes its ledger for the demand and loses nothing. |
| `Medium_CollectProfiling.TaskBodiesOnTheWorkerPoolClaimTheirDevicesPairing` | A plan of task bodies alone, run on the worker pool, pairs the device its records name. |
| `Medium_CollectProfiling.TaskBodiesOnTheChainClaimTheirDevicesPairing` | A plan of task bodies alone, run as a chain, pairs the device its records name. |


## test_register_profiler_parts/test_graph_identity.cpp

What tells one graph's runs from another's in a profile: its label, its id and its nodes' origin.

| Test | What it proves |
| --- | --- |
| `Small_GraphLabel.TheTableCarriesTheLabelOfItsGraph` | The label of a graph, static or dynamic, is the label of the table its runs are observed with, and none is empty. |
| `Small_GraphLabel.ALabelChangeAppliesAtTheNextBuildAndForcesNone` | A changed label reaches the table at the next build and forces no rebuild of its own. |
| `Small_GraphLabel.ADynamicGraphCarriesItsLabel` | A dynamic graph takes a label too. |
| `Small_NodeOptions.OriginIsCopiedAtRecordTime` | The origin a caller states is copied when the node is recorded, so its storage may go. |
| `Small_NodeOptions.ANodeThatStatesNoOriginHasNone` | A node that states no origin has an empty one. |
| `Small_GraphId.ARunCarriesTheIdOfItsGraph` | A run carries the id of its graph, static or dynamic, and no graph shares or has the id 0. |
| `Small_GraphId.AnAsyncStepCarriesTheIdOfItsGraph` | An async step is observed under the id of the graph that started it. |
| `Small_GraphId.AMovedFromGraphReportsZero` | A graph keeps its id across a move and the moved-from one reports 0. |
| `Small_GraphId.NoGraphHasIdZero` | Every graph takes its own id and none takes 0. |


## test_register_profiler_parts/test_runtime_session.cpp

A SushiProfiler session over a runtime end to end: its zones, its device spans and their joins.

| Test | What it proves |
| --- | --- |
| `Medium_RuntimeSession.TheFirstPeriodHoldsThePlanBuildAndTheRunZones` | The first period holds the plan zones the first run builds and the run zones. |
| `Medium_RuntimeSession.LaterPeriodsHoldTheRunZonesAndNoPlanBuild` | The later periods hold the run zones and no plan build, since the plan is cached. |
| `Medium_RuntimeSession.EveryPeriodHoldsOneSpanPerNodeDispatch` | Every period holds one span per node dispatch and no span was dropped. |
| `Medium_RuntimeSession.JoinsEverySpanToANodeName` | Every span joins a node name, its operation and its registration index. |
| `Medium_RuntimeSession.PutsEachSpanInThePeriodHoldingItsStart` | Each span is in the period whose steady span holds its start. |
| `Medium_RuntimeSession.ResolvesEverySubmitThreadToAnEnclosingZone` | Every span's submitting thread resolves to the thread whose submit zone encloses the span's queue time. |
| `Medium_RuntimeSession.EverySpanNamesTheSchedulerLayer` | Every span names the scheduler layer when its run took the worker pool. |
| `Medium_RuntimeSession.EverySpanLiesOnTheDevicesTrack` | Every span lies on the runtime's device track. |
| `Medium_RuntimeSession.ARunAsyncStepWaitedForLateStillLandsInItsPeriod` | A run_async step waited for after its period closed still lands in the period it ran in. |
| `Medium_RuntimeSession.ASpanThatFinishesWithinTheMaxDelayLandsInItsPeriodAndIsNotLate` | A step finishing after its period ended, within the source's maximum delay, lands in its period and is not late. |
| `Medium_RuntimeSession.AStepWhoseWorkFinishedAfterTheLingerIsCountedInTheDrops` | A step whose own work finishes after its period closed and the maximum delay passed is counted in the drops. |
| `Medium_RuntimeSession.AnInferenceShapedGraphOnTheCpuDeviceRecordsEveryNode` | An inference-shaped graph on the CPU device records every node of every pass. |


## test_register_profiler_parts/test_runtime_session_devices.cpp

A session over a GPU and over two devices, the cases that need hardware the CPU device cannot stand in for.

| Test | What it proves |
| --- | --- |
| `Large_RuntimeSessionOnAGpu.SpansOnTheGpuHaveAPairingUncertaintyBelowTheirDuration` | The longest span of a period on the GPU lasts longer than the uncertainty of its device's pairing. |
| `Large_RuntimeSessionOnTwoDevices.TwoDevicesGetOneTrackAndOnePairingEach` | With two devices each gets its own track and its spans are all placed. |


## test_register_profiler_parts/test_runtime_session_stress.cpp

Two runtimes feeding one source from several threads while the consumer collects.

| Test | What it proves |
| --- | --- |
| `Medium_RuntimeSessionStress.TwoRuntimesFeedOneSourceFromSeveralThreadsWhileTheConsumerCollects` | Two runtimes feeding one source from several threads while the consumer drains fail no run and leave only well-formed spans on the runtimes' tracks. |


## test_register_profiler_parts/test_device_timing_window.cpp

Device timing follows the observers a session adds, and the capture window decides what it keeps.

| Test | What it proves |
| --- | --- |
| `Medium_DeviceTimingWindow.TheRecordHoldsOnlyThePeriodsOfTheWindow` | With a window of one warm-up and two active periods the record holds exactly those three periods. |
| `Medium_DeviceTimingWindow.SpansExistOnlyForTheRecordedPeriods` | Spans exist only for the recorded periods: three nodes in each, none from the periods after the window. |
| `Medium_DeviceTimingWindow.CostsNoRebuild` | A session costs no plan rebuild, when it starts, in its window or when it ends. |
| `Medium_DeviceTimingWindow.ARecordMadeWithoutTheSourceHoldsNoDeviceSpans` | A runtime with no session records no device spans in a record made without its source. |
| `Medium_DeviceTimingWindow.APausedSessionReceivesNoDeviceSpans` | A paused session receives no device span, and the periods after the resume do. |
| `Medium_DeviceTimingWindow.AnUngatedObserverIsTimedWhileTheSessionIsPaused` | An observer the session does not gate is timed whether the session is paused or not. |
| `Medium_DeviceTimingWindow.PausingAndResumingRebuildNothing` | Pausing and resuming a session rebuild no plan. |


## test_register_profiler_parts/test_plan_zones.cpp

The zones the runtime writes while it builds the plan of a graph.

| Test | What it proves |
| --- | --- |
| `Medium_PlanZones.ABuildWritesPlanBuildAroundPlanCompile` | A build writes plan.compile inside plan.build, and the build covers the compile's time. |
| `Medium_PlanZones.ABuildWritesItsPrepareAndClassifySteps` | A build writes the prepare and classify steps in the tree of its compile. |
| `Medium_PlanZones.TheSecondRunWritesNoBuildBecauseThePlanIsCached` | A second run writes no plan.build, because the plan is cached. |
| `Medium_PlanZones.ABuildWritesTheNodeAndRootCountsAsValues` | Building a plan writes the node and root counts as values. |
| `Medium_PlanZones.NoZoneIsWrittenWhileTheGateIsClosed` | Nothing of the plan is written while the gate is closed. |
| `Medium_PlanZones.ADynamicGraphWritesRegionCommit` | A graph of regions writes region.commit when it composes its plan. |


## test_register_profiler_parts/test_run_zones.cpp

The zones the runtime writes while it runs a graph on the worker pool and on the chain.

| Test | What it proves |
| --- | --- |
| `Medium_RunZones.TheSchedulerPathWritesSubmitAndWaitUnderTheRun` | On the worker pool a run writes run.submit and run.wait under run.steps, with their children. |
| `Medium_RunZonesAtLevelTwo.DispatchZonesAreWritten` | The dispatches of a run write dispatch.task and dispatch.submit on the worker threads. |
| `Medium_RunZonesBelowLevelThree.TheStealZoneIsAbsent` | The per-element zones of the steal path are not compiled into a library below level 3. |
| `Medium_RunZones.TheChainPathWritesItsOwnZones` | On the in-order chain a run writes its own zones and none of the pool's. |
| `Medium_RunZones.WorkerThreadsCarryTheirNames` | A worker thread of the pool carries its name in the record. |
| `Medium_RunZones.TheRunZonesAreOnTheCallersThread` | The stage zones of a blocking run are written on the caller's thread, not on a worker's. |
| `Medium_RunZones.RuntimeWaitWritesWaitRuntimeAroundWaitQueues` | Runtime::wait writes wait.runtime around wait.queues. |


## test_register_profiler_parts/test_run_counters.cpp

The counters and values the runtime writes at the end of every run.

| Test | What it proves |
| --- | --- |
| `Medium_RunCounters.TransferBytesMatchTheExplicitCopies` | A run that copies a known number of bytes writes counters of exactly those bytes. |
| `Medium_RunCounters.ARunThatCopiesNothingWritesZeroBytes` | A run that copies nothing writes counters of zero, not no counter. |
| `Medium_RunCounters.TheTaskCountIsWrittenPerRun` | The task count of every run is written as a counter. |
| `Medium_RunCounters.WorkerBucketSumsAreWrittenOnEveryRun` | The worker time is written as four values on every run, with no observer added. |
| `Medium_RunCounters.AnUnobservedRunReportsNoStaleWorkerTime` | The worker values of a run that recorded no timing are zero, not the time of an earlier run. |


## test_register_profiler_parts/test_lock_wait.cpp

SR_LOCK_WAIT, the front the scheduler locks its submission mutex with.

| Test | What it proves |
| --- | --- |
| `Medium_PendingMutexLockWait.AnUncontendedLockWritesNone` | A lock taken with no one holding it writes no zone. |
| `Medium_PendingMutexLockWait.AContendedLockWritesAZone` | A lock taken while another thread holds the mutex writes one zone. |
| `Small_PendingMutexLockWait.TheGuardReleasesTheMutexAtTheEndOfItsScope` | The guard releases the mutex when its scope ends, so the next lock is uncontended. |


## test_register_profiler_parts/test_usm_allocator_zones.cpp

The zones and values the USM allocator writes for an allocation and a release.

| Test | What it proves |
| --- | --- |
| `Medium_UsmAllocatorZones.AnAllocationWritesAZoneAndItsSize` | An allocation writes a memory.allocate zone and a value of its size. |
| `Medium_UsmAllocatorZones.AReleaseWritesAZone` | Releasing the buffer writes a memory.release zone. |
| `Medium_UsmAllocatorZones.EachAllocationWritesOneValueOfItsOwnSize` | The value of an allocation is the size of that allocation, one value per allocation. |


## benchmark_run_overhead.cpp

Measures what the profiler costs one run of a graph with the gate closed, open and observed. The
suite is a `Benchmark_` suite, run by `sr bench`; it prints a table and passes or fails nothing about
the figures.

| Benchmark | What it measures |
| --- | --- |
| `Benchmark_RunOverhead.CostOfOneRunInEachState` | The per-run cost of the zones at the level this library was compiled at, for four states. |

## compile_source_names.cpp

Includes `source_names.hpp` alone, so the build fails if the header misses an include. It holds no test.
