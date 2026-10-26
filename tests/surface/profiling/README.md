# profiling tests

The tests of the `profiling` module, mirroring `modules/surface/profiling/`. The module's test list builds them into
`sr_profiling_tests` and the benchmark into its own program. `sr test` runs them with the rest;
`sr run sr_profiling_tests -- --gtest_filter='<pattern>'` runs one program alone. The tests link SushiProfiler.

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
`AStepWhoseWorkFinishedAfterTheLingerIsCountedInTheDrops`, which gives it a bounded time to collect a closed period.

## test_register_profiler_parts/observed_pipeline.hpp

`ObservedPipeline` is the same three-node graph in the namespace `ObserverTests`, for the tests that add observers and read observations
with no session.


## test_run_queue.cpp

RunQueue, the bounded hand-over of observations from the runs' threads to the consumer.

| Test | What it proves |
| --- | --- |
| `Small_RunQueue.TakesEveryObservationOldestFirst` | A queue that holds some observations hands them over oldest first. |
| `Small_RunQueue.DropsTheOldestWhenFullAndCountsIt` | A full queue drops the oldest observation and counts the records it held. |
| `Small_RunQueue.ACapacityOfZeroHoldsOne` | A queue of capacity zero still holds one observation. |
| `Small_RunQueue.EndDropsWhatWasNeverCollected` | Discard() drops what was never taken and says how many that was. |
| `Small_RunQueue.SeveralThreadsMayPushWhileOneTakes` | Several threads may push while one takes, with nothing lost within the capacity. |


## test_run_feed.cpp

RunFeed, the observer that puts a runtime's runs on the source's queue.

| Test | What it proves |
| --- | --- |
| `Small_RunFeed.QueuesEachObservationTaggedWithItsRuntime` | A feed queues each observation it hears, tagged with its runtime. |
| `Small_RunFeed.TwoFeedsOfOneQueueKeepTheirRuntimesApart` | Two feeds of one queue keep their runtimes apart. |
| `Small_RunFeed.AFeedKeepsItsQueueAlive` | A feed keeps its queue alive, so an observation that arrives after the owner let go is safe. |


## test_clock_conversion.cpp

The conversion of a device's event clock to the steady clock through its pairing.

| Test | What it proves |
| --- | --- |
| `Small_ClockConversion.ThePairedInstantMapsToItsSteadyReading` | The paired instant maps to its steady reading. |
| `Small_ClockConversion.ALaterEventTimeLandsTheSameDistanceLater` | A later event time lands the same distance after the steady reading. |
| `Small_ClockConversion.AnEarlierEventTimeLandsTheSameDistanceEarlier` | An earlier event time lands the same distance before the steady reading. |
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
| `Small_DeviceTrackName.TheHostTrackNamesTheNode` | The host track names the node and is not a device track of any index. |
| `Medium_DeviceTrackName.TheRuntimeOverloadAgreesWithThePartsOverload` | The runtime overload builds the same string from the runtime's own node and device. |
| `Medium_DeviceTrackName.AnIndexTheRuntimeLacksNamesAGenericDevice` | An index the runtime does not have names a device called "device". |


## test_device_source.cpp

DeviceSource over SushiProfiler's own SourceContext, SpanSink and SpanTable, with no device.

| Test | What it proves |
| --- | --- |
| `Small_DeviceSource.ConvertsEventTimesThroughThePairing` | Event times go through the device's pairing onto the record's clock. |
| `Small_DeviceSource.SetsNodeInvocationOperationAndLayerAttributes` | The integer and name attributes every span carries. |
| `Small_DeviceSource.ANameAttributeNamesTheChainLayer` | A chain-layer observation names the chain layer. |
| `Small_DeviceSource.OmitsAWidthThatIsUnknown` | An operation that names none is left out and a width that is unknown is left out. |
| `Small_DeviceSource.WritesTheCostOfANodeThatStatesOne` | A node that stated a cost writes flops, bytes and its dtype. |
| `Small_DeviceSource.WritesAStatedZeroCostAndOmitsAnAbsentOne` | A stated zero cost is written as zeros and an absent cost writes nothing. |
| `Small_DeviceSource.PutsAHostNodeOnTheHostTrack` | A host node goes on the runtime's host track on the steady clock, with no uncertainty. |
| `Small_DeviceSource.ASpanOfALaterPeriodWaitsForIt` | A span that starts in a later period waits for it. |
| `Small_DeviceSource.ASpanOfAClosedPeriodIsDroppedLate` | A span that started before the period is dropped late and not added. |
| `Small_DeviceSource.ForwardsTheDropsOfARun` | The drops a run carries reach the sink as late spans. |
| `Small_DeviceSource.ARunWithNoDropsForwardsNone` | A run with no drops forwards none. |
| `Small_DeviceSource.AMissingPairingDropsTheSpan` | A record of a device with no pairing is dropped and counted. |
| `Small_DeviceSource.TheQueuesOverflowIsCounted` | The records a full queue dropped are counted too. |
| `Small_DeviceSource.ARecordWithNoRowIsNamedByItsIndex` | A record with no table row is named by its index. |
| `Small_DeviceSource.AnUnknownRuntimeIsSkipped` | An observation of a runtime the source was not given is skipped. |
| `Small_DeviceSource.WritesTheGraphLabelWhenOneIsSet` | A graph's label is written as the graph attribute of each of its spans. |
| `Small_DeviceSource.TwoGraphsWithEqualNodeIndicesDifferByGraph` | Two graphs whose nodes share registration indices are told apart by their labels. |
| `Small_DeviceSource.WritesTheOriginOfANodeThatStatesOne` | A node that states an origin has it written as the origin attribute. |
| `Small_DeviceSource.OmitsTheOriginAndTheLabelWhenEmpty` | An empty origin and an empty label write no attribute. |
| `Small_DeviceSource.TheLabelOfATableIsInternedOnceForAllItsRuns` | The label of one table is interned once however many runs of it arrive. |
| `Small_DeviceSourceRuntimes.TwoRuntimesGetTracksNamedByTheirNodes` | Two runtimes get tracks named by their own nodes. |
| `Small_DeviceSource.EndStopsTheDeliveryAndDropsWhatWasNeverCollected` | Begin() starts the delivery and end() stops it and drops what was never collected. |
| `Small_DeviceSourceStart.BeginFailsWhenTheAttachmentRefuses` | A source whose delivery cannot start reports why and does not begin. |
| `Small_DeviceSourceStart.TheSourceIsNamedSushiruntimeDevice` | The name the registry knows the source by. |


## test_runtime_environment.cpp

The facts the runtime states about its machine, its devices and its build.

| Test | What it proves |
| --- | --- |
| `Medium_RuntimeEnvironment.StatesEveryDeviceOfTheRuntime` | The record states every device the runtime drives with its class, backend and driver. |
| `Medium_RuntimeEnvironment.StatesTheVersionAndTheWorkers` | The runtime states its version, its workers and the rebalancer. |
| `Medium_RuntimeEnvironment.StatesTheBuildTypeAsABuildFact` | The library's own build type is a build fact, so two records of different builds do not compare. |
| `Medium_RuntimeEnvironment.ARefusedRegistrationStatesNothingTwice` | A registry that refused the source states nothing of the runtime. |


## test_profiling_log_category.cpp

The profiling module logs under a category named after its folder.

| Test | What it proves |
| --- | --- |
| `Small_ProfilingLogCategory.TheCategoryIsNamedAfterTheModule` | The category is the module's folder in the runtime's project. |


## test_register_profiler_parts/test_registration.cpp

How the runtime registers its source and contributor, and how it adds and removes its observers.

| Test | What it proves |
| --- | --- |
| `Medium_RegisterProfilerParts.RefusesASecondRegistration` | The registry refuses a second registration of the same source name. |
| `Medium_RegisterProfilerParts.TheRegistryMakesTheDeviceSource` | The registered source is the one the registry knows by the runtime's source name. |
| `Medium_RegisterProfilerParts.ASessionAddsAndRemovesTheObserver` | A session adds the observer when its source begins and removes it when it ends. |
| `Medium_RegisterProfilerParts.TheDestructorRemovesItsOwnObserverOnly` | The destructor removes only the observer it added, so another observer keeps its runs. |
| `Medium_RegisterProfilerParts.AFurtherRuntimeAttachesBeforeASessionAndNotDuringOne` | A second runtime may feed the same source before a session starts and not while one runs. |
| `Medium_RegisterProfilerParts.RefusedPartsAttachNothing` | Parts whose registration was refused attach nothing. |


## test_register_profiler_parts/test_run_observer.cpp

The observers a runtime fans its finished runs out to, and the device timing they request.

| Test | What it proves |
| --- | --- |
| `Medium_RunObserver.SeesEachBlockingRunOnce` | An observer sees each blocking run once, with the nodes the plan holds. |
| `Medium_RunObserver.TwoObserversBothReceiveEveryRun` | Two observers both receive every run. |
| `Medium_RunObserver.RemovingOneLeavesTheOther` | Removing one observer leaves the other receiving runs. |
| `Medium_RunObserver.IsNotCalledAfterItsHandleIsReset` | No observer is called once its handle is reset. |
| `Medium_RunObserver.ADestroyedHandleRemovesItsObserver` | A handle that goes out of scope removes its observer. |
| `Medium_RunObserver.AHandleThatOutlivesItsRuntimeRemovesNothing` | A handle that outlives its runtime removes nothing and does not crash. |
| `Medium_RunObserver.AnObserverHearsOnlyItsOwnRuntime` | An observer of one runtime is not called by the runs of another. |
| `Medium_RunObserver.SeesAnAsyncStepWhenItsWorkFinishesNotWhenItIsWaitedFor` | An async step is observed when its device work finishes, not when it is waited for. |
| `Medium_RunObserver.AnAsyncStepWithNoObserverStillCompletes` | An async step of a runtime with no observer reports to nobody and still completes. |
| `Medium_RunObserver.TheFirstRunAfterAnAddOfAPlanBuiltBeforeCarriesRecords` | The first run after an add carries full records even though its plan was built before the add. |
| `Medium_RunObserver.AddingAnObserverCostsNoRebuild` | Adding an observer costs no plan rebuild, for the first observer and for later ones. |
| `Medium_RunObserver.RemovingTheLastObserverCostsNoRebuild` | Removing the last observer costs no plan rebuild and the runs after it are untimed. |
| `Medium_RunObserver.AnObserverAddedMidRunTimesTheNextRunNotTheCurrentOne` | An observer added while a step is in flight gets that step with no records and times the next run. |
| `Medium_RunObserver.ARunWithNoObserverMakesNoObservation` | A graph run with no observer attached records no device timing and reports to nobody. |
| `Medium_RunObserver.ARunOnTheChainPathCarriesTheChainLayer` | A run on the chain path is observed with the chain layer and its records. |
| `Medium_RunObserver.AMultiStepRunIsOneObservationWithEveryStep` | A multi-step run is one observation holding every step's records. |


## test_register_profiler_parts/test_collect_profiling.cpp

What a run's observation holds: the node table, the records, the pairings and the drops.

| Test | What it proves |
| --- | --- |
| `Medium_CollectProfiling.TheTableAndTheRecordsJoinByNodeIndex` | Every record joins a table row by its node index, with the name, operation and cost stated. |
| `Medium_CollectProfiling.EveryDeviceRecordHasItsDevicesPairing` | Each kernel record names a device the runtime has and that device's pairing is present. |
| `Medium_CollectProfiling.APlanRunOnTheChainFillsTheSameTable` | The table of a plan on the chain path holds the same rows as on the worker pool. |
| `Medium_CollectProfiling.CarriesNoDropWhenNoneWasLost` | A run that lost no dispatch carries zero drops and logs nothing. |
| `Medium_CollectProfiling.CarriesTheDropCountsOfTheRun` | A run longer than its ledger was sized for carries the drops and logs one WARN. |
| `Medium_CollectProfiling.TheRunAfterAnOverflowLosesNothing` | The run after an overflow sizes its ledger for the demand and loses nothing. |


## test_register_profiler_parts/test_graph_identity.cpp

What tells one graph's runs from another's in a profile: its label, its id and its nodes' origin.

| Test | What it proves |
| --- | --- |
| `Medium_GraphLabel.TheTableCarriesTheLabelOfItsGraph` | A label set on a graph is the label of the table its runs are observed with. |
| `Medium_GraphLabel.AGraphWithNoLabelHasAnEmptyOne` | A graph with no label has an empty one. |
| `Medium_GraphLabel.ALabelChangeAppliesAtTheNextBuildAndForcesNone` | A changed label reaches the table at the next build and forces no rebuild of its own. |
| `Medium_GraphLabel.ADynamicGraphCarriesItsLabel` | A dynamic graph takes a label too. |
| `Medium_NodeOptions.OriginIsCopiedAtRecordTime` | The origin a caller states is copied when the node is recorded, so its storage may go. |
| `Medium_NodeOptions.ANodeThatStatesNoOriginHasNone` | A node that states no origin has an empty one. |
| `Medium_GraphId.TwoGraphsOnOneRuntimeObserveTheirOwnIds` | Two graphs on one runtime observe their own ids and never each other's. |
| `Medium_GraphId.ADynamicGraphRunCarriesItsOwnId` | A run of a dynamic graph carries the dynamic graph's id. |
| `Medium_GraphId.AnAsyncStepCarriesTheIdOfItsGraph` | An async step is observed under the id of the graph that started it. |
| `Medium_GraphId.AMovedFromGraphReportsZero` | A graph keeps its id across a move and the moved-from one reports 0. |
| `Medium_GraphId.NoGraphHasIdZero` | A run with no known graph id would read 0, which no graph has. |


## test_register_profiler_parts/test_runtime_session.cpp

A SushiProfiler session over a runtime end to end: its zones, its device spans and their joins.

| Test | What it proves |
| --- | --- |
| `Medium_RuntimeSession.RecordsPlanRunAndDeviceSpansOfAGraph` | The record holds the plan zones, the run zones and the three device spans of every period. |
| `Medium_RuntimeSession.JoinsEverySpanToANodeName` | Every span joins a node name, its operation and its registration index. |
| `Medium_RuntimeSession.PutsEachSpanInThePeriodHoldingItsStart` | Each span is in the period whose steady span holds its start. |
| `Medium_RuntimeSession.ResolvesEverySubmitThreadToAnEnclosingZone` | Every span's submitting thread resolves to the thread whose submit zone encloses the span's queue time. |
| `Medium_RuntimeSession.WritesTheCostAttributesWhereACostWasStated` | A node that stated a cost writes it as attributes and a node that stated none writes none. |
| `Medium_RuntimeSession.EverySpanNamesItsLayerAndLiesOnADeviceTrack` | Every span names the execution layer of its run and that the tracks are the runtime's. |
| `Medium_RuntimeSession.ARunOnTheChainPathNamesTheChainLayer` | A plan that runs as a chain names the chain layer. |
| `Medium_RuntimeSession.TheRecordStatesTheDevicesAndTheBuild` | The record states the runtime's devices, its drivers and the build of the library. |
| `Medium_RuntimeSession.ARunAsyncStepWaitedForLateStillLandsInItsPeriod` | A run_async step waited for after its period closed still lands in the period it ran in. |
| `Medium_RuntimeSession.AStepWhoseWorkFinishedAfterTheLingerIsCountedInTheDrops` | A step whose own work finishes after its period closed and lingered is counted in the drops. |
| `Medium_RuntimeSession.AnInferenceShapedGraphOnTheCpuDeviceRecordsEveryNode` | An inference-shaped graph on the CPU device records every node of every pass. |


## test_register_profiler_parts/test_runtime_session_devices.cpp

A session over a GPU and over two devices, the cases that need hardware the CPU device cannot stand in for.

| Test | What it proves |
| --- | --- |
| `Large_RuntimeSession.SpansOnTheGpuHaveAPairingUncertaintyBelowTheirDuration` | The longest span of a period on the GPU lasts longer than the uncertainty of its device's pairing. |
| `Large_RuntimeSession.TwoDevicesGetOneTrackAndOnePairingEach` | With two devices each gets its own track and its spans are all placed. |


## test_register_profiler_parts/test_runtime_session_stress.cpp

Two runtimes feeding one source from several threads while the consumer collects.

| Test | What it proves |
| --- | --- |
| `Medium_RuntimeSessionStress.TwoRuntimesFeedOneSourceFromSeveralThreadsWhileTheConsumerCollects` | The source takes observations from two runtimes on several threads while the consumer drains, and that no observation is lost within the queue's capacity. |


## test_register_profiler_parts/test_device_timing_window.cpp

Device timing follows the observers a session adds, and the capture window decides what it keeps.

| Test | What it proves |
| --- | --- |
| `Medium_DeviceTimingWindow.SpansExistOnlyForTheRecordedPeriods` | With a window of one warm-up and two active periods the record holds spans for exactly those three. |
| `Medium_DeviceTimingWindow.CostsNoRebuild` | A session costs no plan rebuild, when it starts, in its window or when it ends. |
| `Medium_DeviceTimingWindow.ARecordMadeWithoutTheSourceHoldsNoDeviceSpans` | A runtime with no session records no device spans in a record made without its source. |


## test_register_profiler_parts/test_plan_zones.cpp

The zones the runtime writes while it builds the plan of a graph.

| Test | What it proves |
| --- | --- |
| `Medium_PlanZones.ABuildWritesPlanBuildAroundPlanCompile` | A build writes plan.build around plan.compile, and the first one is the longer. |
| `Medium_PlanZones.ABuildWritesTheNodeAndRootCountsAsValues` | Building a plan writes the node and root counts as values. |
| `Medium_PlanZones.NoZoneIsWrittenWhileTheGateIsClosed` | Nothing of the plan is written while the gate is closed. |
| `Medium_PlanZones.ADynamicGraphWritesRegionCommit` | A graph of regions writes region.commit when it composes its plan. |


## test_register_profiler_parts/test_run_zones.cpp

The zones the runtime writes while it runs a graph on the worker pool and on the chain.

| Test | What it proves |
| --- | --- |
| `Medium_RunZones.TheSchedulerPathWritesSubmitAndWaitUnderTheRun` | On the worker pool a run writes run.submit and run.wait under run.steps, with their children. |
| `Medium_RunZones.DispatchZonesAreWrittenAtLevelTwo` | The dispatches of a run write dispatch.task and dispatch.submit on the worker threads. |
| `Medium_RunZones.TheStealZoneIsAbsentBelowLevelThree` | The per-element zones of the steal path are not compiled into a level-2 library. |
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
| `Medium_PendingMutexLockWait.TheGuardReleasesTheMutexAtTheEndOfItsScope` | The guard releases the mutex when its scope ends, so the next lock is uncontended. |


## test_register_profiler_parts/test_usm_allocator_zones.cpp

The zones and values the USM allocator writes for an allocation and a release.

| Test | What it proves |
| --- | --- |
| `Medium_UsmAllocatorZones.AnAllocationWritesAZoneAndItsSize` | An allocation writes a memory.allocate zone and a value of its size. |
| `Medium_UsmAllocatorZones.EachAllocationWritesOneValueOfItsOwnSize` | The value of an allocation is the size of that allocation, one value per allocation. |


## benchmark_run_overhead.cpp

Measures what the profiler costs one run of a graph with the gate closed, open and observed.

| Test | What it proves |
| --- | --- |
| `Medium_RunOverheadBenchmark.PrintsTheCostOfOneRunInEachState` | Measures the per-run cost of the zones at the level this library was compiled at, for four states. |
