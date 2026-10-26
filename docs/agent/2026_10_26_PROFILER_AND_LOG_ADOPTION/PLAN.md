# SushiRuntime adopts SushiProfiler, and logs where it is silent: plan

**Status:** Open — in progress; every task is written against SPEC.md section 8 and its amendments, and nothing is built.

**Implements:** PROFILING#P1, PROFILING#P2, PROFILING#P3, PROFILING#P4

## How to read it

- Task ids: `BW` build wiring, `LG` logging, `PF` profiling, `DOC` the closing documentation. A SushiLog gap is `L1` to `L7`
  and a SushiProfiler gap `G1` to `G9`, both in SPEC.md; a decision is `D1` to `D23`. Place ids (`A1`, `E4`, `M1`...) are the
  rows of SPEC.md, section 2.1.
- Order is hierarchy first: a task comes after every interface and brick it uses, and a module boundary with its build edges
  is committed before either side is written (`multi-agent-work`).
- A step marked **orchestrator** edits a build file (CMake, `cli/sushistack.deps.toml`, `.github/workflows/`, the tier table). The
  implementer does not touch it (rule 3 of the dispatch block).
- Every task has one acceptance criterion and one build. The build is run by the orchestrator or the owner through the CLI,
  never by an implementer: `sr build`, then `sr run <program> -- --gtest_filter='<pattern>'` for the suite named. Program names
  follow `sr_<module>_tests` (`tests/foundation/log/README.md`).
- Tests follow the `testing` skill: a name that starts with the unit, a size prefix (`Small_`, `Medium_`, `Large_`), a fixed seed,
  a regression test seen failing before a fix. A `Medium_` test uses the CPU SYCL device; a `Large_` one needs the GPU and is
  skipped, never passed, without it. A test that reads a log puts SushiLog in synchronous mode and restores the sink and the level
  in its fixture, as `tests/surface/api/runtime/test_runtime.cpp` does around `Medium_LoggingDefaults`.
- Each task's module READMEs change in its commit; the manual pages, the design topic, the changelog and the backlog are
  `DOC-1`, the orchestrator's.
- Each new test file is added to its `tests/**/CMakeLists.txt`, an **orchestrator** edit, in the same commit.

The plan runs inline in hierarchy order, and every build waits until all of its code is written (SPEC.md, section 8).

## What the plan stands on

The decisions are in SPEC.md section 8 and its amendments (R1 to R14). The SushiProfiler side is v0.2.0 at `8b9bc28`, read
on 2026-10-26; none of its gaps blocks a task.

| Fact of SushiProfiler v0.2.0 | Used by |
| --- | --- |
| `SushiProfiler::SushiProfiler` is a shared library, one collector per process (G1) | `PF-1` and every zone; `PF-14` proves it across the library boundary |
| `Source::collect(const PeriodSpan&, SpanSink&)` hands the period's steady span; a span goes in the period that holds its start, older work is `drop_late`, later work waits (G4, G6) | `PF-6`; no period is read in the runtime |
| The `SourceContext` of `begin()` is valid until `end()` and `collect()` may call `intern`, `add_track` and `to_record_ns` (G3) | `PF-6` |
| `register_profiler_parts(Registry&)` returns an owning, non-movable `<Lib>ProfilerParts`; the registry holds a source factory and a contributor by reference (G8) | `PF-7` |
| A period fact is not offered (G2); the layer is the span attribute `layer` | `PF-6` |
| `SushiProfiler::DeviceSpanKeys` (`record/device_span_keys.hpp`) names the span attributes `node`, `invocation`, `operation`, `launched_width`, `live_width`, `flops`, `bytes` and `dtype` | `PF-6` |
| `SP_LOCK_WAIT`, `SP_ZONE_AT`, `SP_VALUE_AT`, `SP_COUNTER_AT` take the compiled level as an argument | `PF-1` |

Work in other repositories that this plan waits on: SushiLog L1 to L3 and L7 for `LG-7` only (the runtime keeps local
counters until they ship). The consumers that break are listed under "Breaking changes and migration"; their edits are not part of
this plan.

## The tasks

| Id | Task | Build files (orchestrator) |
| --- | --- | --- |
| BW-1 | Log categories: build edges and tier entries | yes |
| LG-0 | A log capture helper for the runtime's tests | yes |
| LG-1 | `api` category; A1 to A5 | tests list |
| LG-2 | Scheduler and execution: E1, E2, E6, E7, E10 | tests list |
| LG-4 | Memory and device: M1 to M3 | tests list |
| LG-5 | Process and network: P1 to P4 | tests list |
| LG-6 | The assertion path and the sync-mode macro: F1, D17 | tests list |
| PF-0 | The pin, the options and the package file | yes |
| PF-1 | The `profile` front | yes |
| PF-3 | Report types: node table, record fields, cost, observer, drop counts | tests list |
| PF-2 | One writer of a node event record, per-device pairing, the hand-over of a body's events | tests list |
| TL-1 | Timing leaves `RunReport` (D24) | tests list |
| PF-4 | Fill the node table and the new fields | tests list |
| LG-3 | Counted drops: E3, E4, E5, E8, E9, E11 | tests list |
| PF-5 | The run observers, `Runtime::add_run_observer`, device timing while any is attached | tests list |
| PF-6 | The device source | yes (module, tier) |
| PF-7 | The environment contributor, the registration function and the track name | yes |
| PF-8 | Zones of the plan build | no |
| PF-9 | Zones of the run, the chain and the dispatch; thread names | no |
| PF-10 | Lock waits on `pending_mutex` | no |
| PF-11 | Allocation zones, run counters and the worker buckets as values | no |
| PF-12 | Device timing follows the observers (D7) | tests list |
| PF-13 | The overhead benchmark | tests list |
| PF-14 | End to end: a session records a graph run | tests list |
| PF-15 | The build without SushiProfiler | no |
| PF-16 | Graph label and node origin (R15, R16) | tests list |
| PF-17 | Graph identity (R17) | tests list |
| PF-18 | The source name header (R18) | tests list |
| PF-19 | The live session: max delay and paused timing (R21) | tests list |
| LG-7 | Use SushiLog's throttles; move the per-element lines to `TRACE` | yes (pin) |
| DOC-1 | The manual, the design topic, the changelog, the backlog | no |

`PF-3` comes before `PF-2` because the ledger and its one writer use the report types. `TL-1` follows both: it
removes what the observer replaces. `LG-3` follows `PF-4` because both edit `collect_profiling`.

## Stage 1: build wiring

### BW-1 Log categories: build edges and tier entries (orchestrator)

- **Files:** `modules/surface/api/CMakeLists.txt` (`PRIVATE_DEPENDS log`; `api` lists no `log` today),
  `modules/extension/process_environment/CMakeLists.txt` and `modules/extension/network_socket/CMakeLists.txt` (the same edge),
  `cmake/SushiRuntimeTiers.cmake` only if a same-tier edge is new (none expected: the three consumers are above `log`).
  Adds exactly the categories D13 chose.
- **Acceptance:** the tree configures and the layering checker `tools/layering` passes with the new edges.
- **Build:** one `sr build`.
- **Tests:** none; the checker is the test.

### LG-0 A log capture helper for the runtime's tests

Five test files each build their own capture sink (SPEC.md, L7). New tests share one until SushiLog ships its own; existing
tests are not rewritten here.

- **Files:** new `modules/foundation/test_runner/include/SushiRuntime/test_runner/log_capture.hpp`;
  `modules/foundation/test_runner/README.md`. **Orchestrator:** `modules/foundation/test_runner/CMakeLists.txt` (an include root),
  `tests/foundation/log/CMakeLists.txt` or the test_runner test list for the helper's own test.
- **Acceptance:** a test puts SushiLog in synchronous mode through the helper, logs one `WARN` and one `DEBUG` in a runtime category, and
  reads back the `WARN` with its level, module and message, the `DEBUG` absent; the destructor restores the sink and the level.
- **Build:** one `sr build`, then `sr run sr_log_tests -- --gtest_filter='*LogCapture*'`.
- **Tests:** `Small_LogCapture.RecordsOnlyWhatTheLevelPasses`, `Small_LogCapture.RestoresTheSinkAndTheLevelOnDestruction`; the
  helper is in `test_runner`, which `log` already allows (`SushiRuntimeTiers.cmake`, edge `test_runner log`).
- **Dropped when:** SushiLog ships L7 and the pin carries it (`D18`).

## Stage 2: logging that needs nothing new in SushiLog

### LG-1 The `api` category and the dropped statuses (A1 to A5; A6 per D14)

- **Files:** new `modules/surface/api/include/SushiRuntime/api/api_log_category.hpp`;
  `.../api/graph/recorder.hpp` and `source/recorder.cpp` (the log call cannot sit in the header template `admit`: a non-template
  overload moves to `recorder.cpp`, the template calls it); `.../api/detail/step_run.hpp` and `source/step_run.cpp` (an exported
  `log_dropped_outcome`); `.../api/graph/run_handle.hpp` (`settle`, `:225-231`) and `.../plumbing/step_driver.hpp`
  (`end_pending_step`, `:316-325`) call it; `source/plan_cache.cpp` (the rebuild reason, A4); `source/context_memory.hpp` (A5);
  `modules/surface/api/include/SushiRuntime/api/graph/README.md` and `.../runtime/README.md`. If D14 is (a) or (b), one helper in
  `step_run.cpp` or a new `source/refusal.cpp` logs the 97 sites, and that file joins `modules/surface/api/CMakeLists.txt`
  (**orchestrator**).
- **Acceptance:** each of A1 to A5 has a test that fails before the change and passes after: with the capture sink at `Warn`
  (`Debug` for A4 and A5), the named call writes the line SPEC.md 2.1 gives, once, in `sushiruntime.api`.
- **Build:** one `sr build`, then `sr run sr_api_tests -- --gtest_filter='*Log*:*Refusal*:*Dropped*'`.
- **Tests:** in `tests/surface/api/graph/test_recorder.cpp`, `Medium_RecorderRefusal.LogsTheFirstRefusalAndNamesTheDroppedOnes`
  (A1); in `tests/surface/api/graph/test_run_handle.cpp`, `Medium_RunHandleDrop.LogsTheOutcomeOfAFailedUnwaitedStep` (A2);
  in `tests/surface/api/graph/test_graph.cpp`, `Medium_GraphDestruction.LogsTheOutcomeOfAFailedPendingStep` (A3) and
  `Medium_GraphRebuild.LogsTheReasonOfEachRebuild` (A4, three reasons); new `tests/surface/api/test_api_log_category.cpp`,
  `Small_ApiLogCategory.TheCategoryIsNamedAfterTheModule` (as `tests/engine/scheduler/test_scheduler_log_category.cpp`).
  A5 needs a `sycl::exception` out of `get_pointer_type`; if none can be provoked on the CPU device the test is not written and the
  report says A5 is untested, with the reason.

### LG-2 Scheduler and execution (E1, E2, E6, E7, E10)

- **Files:** `modules/engine/execution/source/scheduler_execution_engine.cpp` (`complete()`, `:588-600`; the two outcomes merge in a
  small function `merge_run_failures` in `scheduler/run_outcome.hpp` or beside it, so a test reaches it without provoking both
  failures); `modules/engine/scheduler/source/task_scheduler.cpp` (`:103-105`, `:158-160`, `:485`, `:490`);
  `modules/engine/execution/source/chain_plan.cpp` and `include/.../chain_plan.hpp` (`ChainPlan` carries the first reason it was
  not chainable; the log line is in `classify_chain`, `:196-230`); `modules/engine/execution/README.md`,
  `modules/engine/scheduler/README.md`.
- **Acceptance:** each place logs the line of SPEC.md 2.1 under `sushiruntime.execution` or `sushiruntime.scheduler`, with the values
  the line names (the code and message of E2, both messages of E1, the requested and applied worker counts of E6, the reason of E10).
- **Build:** one `sr build`, then `sr run sr_scheduler_tests` and `sr run sr_execution_tests` with the filters below.
- **Tests:** `tests/engine/execution/test_scheduler_execution_engine.cpp`:
  `Medium_SchedulerExecutionEngineComplete.LogsTheQueueErrorADroppedTaskErrorHid` (E1, through `merge_run_failures` as a `Small_`
  test and once end to end if both failures can be provoked); `tests/engine/execution/test_chain_plan.cpp`:
  `Small_ClassifyChain.LogsWhyAPlanFellBackToTheScheduler`, one case per reason the module README lists; `tests/engine/scheduler/test_task_scheduler.cpp`:
  `Medium_TaskScheduler.LogsAClampedWorkerCount` (E6), `Medium_TaskScheduler.LogsAPolledFailureWithItsMessage` (E2),
  `Small_TaskScheduler.LogsAWorkerWhoseCoreHasNoNumaNode` (E7, through `build_steal_topology` with a short `core_to_numa`).

### LG-4 Memory and device (M1, M2, M3)

- **Files:** `modules/platform/memory/source/usm_allocator.cpp` (`:107-108`); `modules/platform/device/source/device_queue_set.cpp`
  (`:171-173` adds backend and driver version, `:224-232` and `:309`); the two READMEs.
- **Acceptance:** a failed allocation logs one `WARN` with kind, bytes, alignment and device; the first request for a profiling
  queue logs one `DEBUG` with device, flavour and build time; the device line at `INFO` names backend and driver.
- **Build:** one `sr build`, then `sr run sr_memory_tests` and `sr run sr_device_tests`.
- **Tests:** `tests/platform/memory/test_usm_allocator.cpp` (it already has `ErrorLineCapture`):
  `Medium_UsmAllocatorFailure.LogsTheRefusedRequest` with a request past the device's memory; `tests/platform/device/`:
  `Medium_DeviceQueueSet.LogsTheBuildOfALazyQueueFlavour`, `Medium_DeviceQueueSet.LogsBackendAndDriverInTheDeviceLine`.
  If SYCL throws instead of returning null for the oversized request, the test uses the failure `allocate` really gives and the line moves to where the status is made.

### LG-5 Process and network (P1 to P4)

- **Files:** `modules/extension/process_environment/source/stop_signal.cpp` (the installer becomes a parameter, default
  `std::signal`, so a test injects `SIG_ERR`) and its new `process_environment_log_category.hpp`;
  `modules/extension/network_socket/source/socket_posix.cpp`, `socket_windows.cpp` and the new `network_socket_log_category.hpp`;
  `modules/extension/distributed/source/cluster_monitor.cpp` (`:232-233`, `:249-250`);
  `modules/platform/topology/source/hw_discovery.cpp` (`:303-316`); the READMEs of the four modules.
- **Acceptance:** each place logs the line of SPEC.md 2.1: P1 and P2 at `WARN` once per event (a signal, a connection), P3 and P4 at `DEBUG`.
- **Build:** one `sr build`, then `sr run sr_process_environment_tests`, `sr_network_socket_tests`, `sr_topology_tests`
  (and `sr_distributed_tests` when distributed is on).
- **Tests:** `tests/extension/process_environment/test_stop_signal.cpp`: `Small_StopSignal.LogsAHandlerThatCouldNotBeInstalled`;
  `tests/extension/network_socket/test_socket.cpp`: `Medium_Socket.LogsAFailedNoDelayOption` through a seam on `set_option`;
  `tests/extension/distributed/test_distributed.cpp`: a `Medium_ClusterMonitor` case for P3 on `InProcessFabric`;
  `tests/platform/topology/`: `Small_HwDiscovery.LogsAPciAddressThatDoesNotParse`.

### LG-6 The assertion path and the sync-mode macro (F1, D17)

- **Files per D16.** (a) `modules/foundation/core/source/assert.cpp` and a `core` category header; **orchestrator:**
  `modules/foundation/core/CMakeLists.txt` (SushiLog) and `cmake/SushiRuntimeTiers.cmake` if the tier edge changes. (b)
  `modules/surface/api/source/runtime.cpp` installs the handler through `set_assertion_handler`. (c) none.
  Per D17: `modules/foundation/log/include/SushiRuntime/log/logger.hpp` (the macro), `modules/foundation/test_runner/source/test_main.cpp:56`
  (its one use becomes `SushiLog::set_log_synchronous(true)`), `modules/foundation/log/README.md`.
- **Acceptance:** a failed `SR_VERIFY` writes one `FATAL` record, through the capture sink, before the process aborts; a test
  that calls `SushiLog::set_log_synchronous` needs no macro.
- **Build:** one `sr build`, then `sr run sr_core_tests --gtest_filter='*Assert*'` and `sr_log_tests`.
- **Tests:** `tests/foundation/core/test_assert.cpp`: `Medium_AssertDeath.LogsTheFailedCheckAtFatal` as a death test that reads
  the sink output the child writes; `Small_AssertHandler.AnInstalledHandlerSeesTheFailure` for the handler of case (b).
- **Waits on:** D16, D17 (`api-stability` sets the path for the public macro).

## Stage 3: profiling bricks

### PF-0 The pin, the options and the package file (orchestrator)

- **Files:** `cli/sushistack.deps.toml` (`[module] depends_on` gains `sushiprofiler`; `[sushisystems.sushiprofiler]` with the version and commit
  of D23); `cmake/Dependencies.cmake` (`find_package(SushiProfiler CONFIG REQUIRED)` of the prebuilt package, taken only when
  `SR_ENABLE_PROFILER` is on, R19); `cmake/SushiRuntimeOptions.cmake` (`SR_ENABLE_PROFILER` default ON, `SR_PROFILE_LEVEL`);
  `cmake/SushiRuntimeConfig.cmake.in` (`find_dependency`, public or private per D1 and D2); `.github/workflows/ci.yml:300-310`
  (checks that the clone step reads the toml generically; edits it only if it does not); `cmake/SushiRuntimeTiers.cmake` and each new
  module's `CMakeLists.txt` as `PF-1`, `PF-6` and `PF-7` land. The SYCL compile-time work edits `SushiRuntimeModule.cmake` and
  `SushiRuntimeSyclTarget.cmake`; D20 orders the two.
- **Acceptance:** `sr setup` fetches the pinned SushiProfiler and `sr build` configures with the option on, finds
  `SushiProfiler::SushiProfiler`, and links it where D1 says.
- **Build:** one `sr build`.
- **Tests:** none; the configure is the test. The off configuration is `PF-15`.

### PF-1 The `profile` front

Declared by D1 (a); under (b) the same files sit in the module D1 names.

- **Files:** `modules/foundation/profile/profile_config.hpp.in` (generated into the build as `SushiRuntime/profile/profile_config.hpp`, R19; `profile.hpp` defines neither macro), `modules/foundation/profile/include/SushiRuntime/profile/profile.hpp` (the macros `SR_ZONE`, `SR_ZONE_FINE`,
  `SR_ZONE_DETAIL`, `SR_VALUE`, `SR_COUNTER`, `SR_INSTANT`, `SR_LOCK_WAIT`, each on SushiProfiler's `_AT` form with this library's level),
  `profile_gate.hpp` (the level of a translation unit, from `SR_PROFILE_LEVEL` as `log_gate.hpp` does from `SR_ENABLE_LOGGING`),
  `profile_keys.hpp` (the attribute and site names, one place), `README.md`. **Orchestrator:** `modules/foundation/profile/CMakeLists.txt`,
  the tier table.
- **Acceptance:** a zone in a translation unit compiled at level 1 writes one event into an open collector, and the same zone compiled at
  level 0 writes nothing and registers no site; with `SR_ENABLE_PROFILER` off the macros expand to nothing (checked by
  `PF-15`).
- **Build:** one `sr build`, then `sr run sr_profile_tests`.
- **Tests:** `tests/foundation/profile/test_profile_gate.cpp`: `Small_ProfileGate.AZoneAtTheCompiledLevelWritesOneEvent`,
  `Small_ProfileGate.AZoneAboveTheCompiledLevelWritesNothing`, `Small_ProfileKeys.EveryKeyIsListedOnce`. Two translation units of
  different levels, as `tests/foundation/log/test_log_gate.cpp` does for logging.
- **Waits on:** G1. Without a shared collector the event is written into a collector no host drains; the test still passes
  inside one image, and `PF-14` is where the boundary is proven.

### PF-3 Report types: node table, record fields, cost, observer, drop counts

- **Files:** `modules/foundation/report/include/SushiRuntime/report/`: `node_event_record.hpp` (`NodeEventRecord` keeps its seven fields in their
  order and gains `device_index` and `submit_thread` at the end, 48 bytes, and `K_HOST_DEVICE_INDEX`), `clock_pairing.hpp` (`ClockPairing`, `DeviceClock`),
  `node_table.hpp` (`NodeTable`, `NodeTableRow` with `std::optional<NodeCost>`), `drop_counts.hpp` (`DropCause`, `DropCounts`), `run_observer.hpp` (`RunObservation`,
  `RunObserver`, `RunObserverHandle`); `modules/foundation/core/include/SushiRuntime/core/node_cost.hpp` (`NodeCost`: `flops`, `bytes`, `dtype`); `NodeOptions::cost`, the node stamp,
  the task metadata and `PlanNode` carry the cost as `std::optional<NodeCost>`; the READMEs. **Orchestrator:** the new test files' lines.
- **Acceptance:** a table row is found by registration index and an index past the table has none; a cost that was never stated is absent from the row and a stated zero is present;
  the record keeps the size and the field order SushiEngine mirrors.
- **Build:** one `sr build`, then `sr run sr_report_tests`.
- **Tests:** `Small_NodeTable.FindsARowByRegistrationIndex`, `Small_NodeTable.AnIndexPastTheTableHasNoRow`, `Small_NodeTable.KeepsAnAbsentCostApartFromAStatedZero`,
  `Small_NodeEventRecord.HasTheSizeAndTheOrderTheEngineMirrors` (a `static_assert` on size and offsets), `Small_NodeCost.NamesItsDtype`,
  `Small_DropCounts.CountsPerCauseAndSaysWhetherAnyWasLost`, `Small_RunReport.ResetClearsWhatItHolds`.

### PF-2 One writer of a node event record, per-device pairing, the hand-over of a body's events

- **Files:** `modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp` (`record_event` and `record_host` fill a whole record, so `device_index`
  and `submit_thread` are assigned in one place; a pairing slot per device claimed by that device's first kernel; a drop count per cause that replaces the debug-only assert),
  `event_timestamps.hpp` (`merge_timestamps` and `read_dispatch_timestamps`: the earliest start, the latest end and the earliest submit of the events a body handed over);
  `modules/engine/task/include/SushiRuntime/task/dispatch_events.hpp` (the thread-local hand-over scope) and `modules/surface/api/include/SushiRuntime/api/graph/dispatch_events.hpp`
  with its source (`note_dispatch_event`); `event_polling.hpp`, `chain_run.cpp` and `task_scheduler.cpp` call the ledger's two functions; the READMEs.
- **Acceptance:** the three writers contain no field assignment of a record; a body that hands over two events is recorded from the earliest start to the latest end, and a body
  that hands over none is recorded as before; each device claims its own pairing.
- **Build:** one `sr build`, then the observability, scheduler and execution suites.
- **Tests:** `Small_ProfilingLedger.RecordEventFillsEveryField`, `Small_ProfilingLedger.KeepsOnePairingPerDevice`, `Small_ProfilingLedger.CountsADispatchThatFoundItsRowFull`,
  `Small_ProfilingLedger.OverflowBehavesTheSameInDebugAndRelease`, `Small_EventTimestamps.MergesEventsIntoOneSpan`, `Medium_DispatchEvents.ABodyHandingOverTwoEventsIsTimedAcrossBoth`,
  `Medium_DispatchEvents.ABodyHandingOverNoneIsTimedAsBefore`; the ledger's existing move tests are rewritten for the device count.

### TL-1 Timing leaves `RunReport` (D24)

- **Files:** `modules/foundation/report/.../run_report.hpp` (removes `node_events`, `clock`, `node_timings`, `NodeTiming`, `worker_timings`, `WorkerTiming`, `scheduling_overhead_ms`,
  `total_duration_ms`, `graph_analysis_duration_ms`, `host_to_device_bytes`, `device_to_host_bytes`); `modules/surface/api/` (`RuntimeConfig::profiling`, `AdvancedControls::is_profiling_enabled`,
  `Graph::set_profiling`, `is_profiling_requested`, the `DynamicGraph` and `StepDriver` twins, the plan cache and region composer requests, `run_steps` and `AsyncRunState` lose their
  compile and wall time, `fill_run_report`); `modules/engine/execution/` (`RuntimeSettings::profiling`, `NodeTimingAccumulator` in `modules/engine/graph/`); the READMEs. The stage timings are the
  zones of `PF-8` and `PF-9`, the byte counts and worker buckets the values of `PF-11`.
- **Acceptance:** none of the removed names remains in `modules`, `tests`, `applications` or the manual; a run still reports success, cancellation, task count, layer and the NUMA and distributed counts.
- **Build:** one `sr build`, then every suite that read a removed member, rewritten to a test observer (`tests/surface/api/graph/`, `tests/engine/execution/`, `tests/foundation/report/`,
  `tests/surface/api/runtime/`, `tests/package/main.cpp`, the examples).
- **Tests:** the existing tests that read a removed member are rewritten, not deleted: those that read node events attach a recording observer; those that read a duration assert on the outcome only.

### PF-4 Fill the node table and the new fields

- **Files:** `modules/engine/graph/source/compiled_graph.cpp` (`capture_profiling_state` builds the shared `NodeTable` once per plan); `modules/engine/execution/source/scheduler_execution_engine.cpp`
  (`collect_profiling` fills a `RunObservation`: table, records, per-device pairings, drop counts); the writers of `PF-2` fill `device_index` and `submit_thread` (the profiler's thread index through the front);
  `modules/engine/graph/README.md`, `modules/engine/execution/README.md`.
- **Acceptance:** a profiled run of a graph of three named nodes observes a table with three rows with their names, operations and costs, and records that name the device each ran on.
- **Build:** one `sr build`, then `sr run sr_execution_tests` and `sr_graph_tests`.
- **Tests:** `Small_CompiledGraph.CapturesOneTableRowPerRegistrationIndex`, `Small_CompiledGraph.ASharedIndexKeepsItsMultiplicity`, `Small_CompiledGraph.CarriesTheCostOfTheFirstNode`,
  `Medium_CollectProfiling.TheTableAndTheRecordsJoinByNodeIndex`, `Medium_CollectProfiling.APlanRunOnTheChainFillsTheSameTable`, `Medium_CollectProfiling.CarriesTheDropCountsOfTheRun`,
  `Medium_CollectProfiling.CarriesNoDropWhenNoneWasLost`. A two-device case is `Large_` and skipped without a second device.

### LG-3 Counted drops (E3, E4, E5, E8, E9, E11)

It follows `PF-4` because both edit `collect_profiling`; the counted drop goes in the ledger of `PF-2`.

- **Files:** `profiling_ledger.hpp` (a drop count per cause); `event_polling.hpp` (the stall-drive count and the kept first failure); `task_scheduler.cpp` (the skipped-task counts, the missing queue);
  `modules/engine/graph/source/task_graph.cpp` (untracked regions); `modules/engine/execution/include/SushiRuntime/execution/dropped_dispatches.hpp` (one line per run); the READMEs.
- **Acceptance:** a run that loses dispatches to a full ledger row, to an unreadable event and to a missing queue logs one `WARN` under `sushiruntime.execution` naming each cause and its count; a run that
  loses none logs nothing; a cancelled run logs one `INFO` under `sushiruntime.scheduler` with the number of tasks it skipped.
- **Build:** one `sr build`, then `sr run sr_observability_tests`, `sr_execution_tests`, `sr_scheduler_tests`.
- **Tests:** `Small_DroppedDispatches.LogsOneLineNamingEachCause`, `Small_DroppedDispatches.LogsNothingWhenNoneWasLost`, `Medium_TaskSchedulerCancel.LogsTheTasksASkippedRunLeftUnrun`,
  `Small_EventPolling.KeepsTheFirstFailureAndLogsTheSecond` (fails before the change), `Medium_EventPolling.CountsAStallDrive`.

### PF-5 The run observers, `Runtime::add_run_observer`, device timing while any is attached

- **Files:** `RunObserver`, `RunObservation` and `RunObserverHandle` are declared in `PF-3`; `modules/engine/execution/.../runtime_context.hpp` and `source/runtime_context.cpp` keep the observers as one immutable list behind one
  atomic shared pointer, copied on every add and remove, and release the list before the engine on destruction; `modules/surface/api/include/SushiRuntime/api/runtime/runtime.hpp` and `source/runtime.cpp`
  (`Runtime::add_run_observer`, which returns the move-only `RunObserverHandle`); `a run reads `wants_device_timing()` once when it is armed and stores the answer in `CompiledGraph::profiling` (R20), so no plan is stale for timing; `step_run.cpp` hands the observation of a blocking run to every observer at the end of the run, and the engine hands that of an asynchronous step over when the step's device work completes (the sentinel), so no
  period is read and a caller that waits late loses nothing.
- **Acceptance:** every observer added to a runtime is called once per finished run with that run's observation, for a blocking run and for a `run_async` step whose caller waits after the period moved on; adding and removing an observer cost no plan rebuild,
  and the first run after an add is timed and carries records.
- **Build:** one `sr build`, then `sr run sr_api_tests -- --gtest_filter='*RunObserver*'`.
- **Tests:** `Medium_RunObserver.SeesEachBlockingRunOnce`, `Medium_RunObserver.TwoObserversBothReceiveEveryRun`, `Medium_RunObserver.RemovingOneLeavesTheOther`,
  `Medium_RunObserver.SeesAnAsyncStepWhenItsWorkFinishesNotWhenItIsWaitedFor`, `Medium_RunObserver.TheFirstRunAfterAnAddOfAPlanBuiltBeforeCarriesRecords`, `Medium_RunObserver.AddingAnObserverCostsNoRebuild`,
  `Medium_RunObserver.RemovingTheLastObserverCostsNoRebuild`, `Medium_RunObserver.AnObserverAddedMidRunTimesTheNextRunNotTheCurrentOne`, `Medium_RunObserver.IsNotCalledAfterItsHandleIsReset`;
  `tests/engine/execution/test_runtime_context.cpp`: `Small_RuntimeContext.RemovesEveryObserverBeforeItsOwnerGoes`.

### PF-6 The device source

- **Files:** new module `profiling` (surface tier): `include/SushiRuntime/profiling/device_source.hpp` and `source/device_source.cpp` (the SushiProfiler `Source`: `begin`, `collect`, `end`; it places each span by its
  converted start against the `PeriodSpan`, drops an older one with `drop_late`, keeps a later one, and forwards the observation's drop counts and its queue's overflow), `run_queue.hpp` and `.cpp` (bounded,
  mutexed queue of observations; drops the oldest and counts), `clock_conversion.hpp` (event time to steady time through the device's pairing, uncertainty saturated to 32 bits), `device_track_name.hpp` and
  `.cpp`, `README.md`. The source writes `SushiProfiler::DeviceSpanKeys::K_NODE`, `K_INVOCATION`, `K_OPERATION`, `K_LAUNCHED_WIDTH`, `K_LIVE_WIDTH`, `K_FLOPS`, `K_BYTES` and `K_DTYPE`
  when the row states a cost, and the name attribute `layer`. **Orchestrator:** `modules/surface/profiling/CMakeLists.txt`, the tier table, `tests/surface/profiling/CMakeLists.txt`.
- **Acceptance:** fed observations of known records, the source's `collect(period)` adds exactly the spans whose converted start lies in that period, with the times, tracks, names and attributes SPEC.md 1.3 lists,
  refuses none, and counts what it drops.
- **Build:** one `sr build`, then `sr run sr_profiling_tests`.
- **Tests:** `Small_DeviceSource.ConvertsEventTimesThroughThePairing`, `Small_DeviceSource.SetsNodeInvocationOperationAndLayerAttributes`, `Small_DeviceSource.OmitsAWidthThatIsUnknown`,
  `Small_DeviceSource.WritesTheCostOfANodeThatStatesOne`, `Small_DeviceSource.WritesAStatedZeroCostAndOmitsAnAbsentOne`, `Small_DeviceSource.PutsAHostNodeOnTheHostTrack`,
  `Small_DeviceSource.ASpanOfALaterPeriodWaitsForIt`, `Small_DeviceSource.ASpanOfAClosedPeriodIsDroppedLate`, `Small_DeviceSource.ForwardsTheDropsOfARun`, `Small_DeviceSource.AMissingPairingDropsTheSpan`,
  `Small_RunQueue.DropsTheOldestWhenFullAndCountsIt`, `Small_RunQueue.EndDropsWhatWasNeverCollected`, `Small_ClockConversion.SaturatesTheUncertaintyAt32Bits`,
  `Small_DeviceTrackName.NamesTheNodeTheIndexAndTheDevice`. The tests drive the real SushiProfiler `SpanSink` over a `SpanTable`, with no device.

### PF-7 The environment contributor, the registration function and the track name

- **Files:** in the `profiling` module: `runtime_environment.hpp` and `source/runtime_environment.cpp` (`EnvironmentContributor`: the facts of SPEC.md 1.8, compiled inside the runtime library so the `build` facts are
  its own), `register_profiler_parts.hpp` and its source (`register_profiler_parts(Registry&, const Runtime&)` and `RuntimeProfilerParts`, which owns the queue, the contributor and the attachment, adds its observer
  to the runtime when a session begins and removes it when the session ends and in its destructor, through a handle of its own, and takes more runtimes with `attach`), the module README. **Orchestrator:** the install rules for the public headers.
- **Acceptance:** starting a SushiProfiler session with the registry the function filled records the device name, kind, backend, driver and the runtime's build type in the record's `environment` and `build`, and refuses a
  second registration of the same name; the parts object's destructor leaves its runtime with the observers it did not add.
- **Build:** one `sr build`, then `sr run sr_profiling_tests`.
- **Tests:** `Medium_RuntimeEnvironment.StatesEveryDeviceOfTheRuntime`, `Medium_RuntimeEnvironment.StatesTheBuildTypeAsABuildFact`, `Small_RegisterProfilerParts.RefusesASecondRegistration`,
  `Medium_RegisterProfilerParts.TheDestructorRemovesItsOwnObserverOnly`, `Medium_RegisterProfilerParts.ASessionAddsAndRemovesTheObserver`. Where the driver version is not answerable the fact reads `unknown`.

## Stage 4: the runtime's own zones

### PF-8 Zones of the plan build

- **Files:** `modules/surface/api/source/plan_cache.cpp` (`plan.build`, `plan.prepare`, `plan.classify`, `:100-147`);
  `modules/engine/graph/source/task_graph.cpp` (`plan.compile`, the values `graph.nodes` and `graph.roots`, `:472-597`);
  `modules/surface/api/source/region_composer.cpp` (`region.commit`, `:379-391`); `modules/engine/graph/README.md`. None in a header template
  (`step_driver.hpp:502-520` stays as it is).
- **Acceptance:** with a session open on a timeline, building a plan writes `plan.build` with `plan.compile` as its child, and the first
  one's inclusive time is not less than the second's.
- **Build:** one `sr build`, then `sr run sr_api_tests -- --gtest_filter='*PlanZones*'`.
- **Tests:** `Medium_PlanZones.ABuildWritesPlanBuildAroundPlanCompile`, `Small_PlanZones.NoZoneIsWrittenWhileTheGateIsClosed`; both read the
  record the session returns, with a fixed graph.

### PF-9 Zones of the run, the chain and the dispatch; thread names

- **Files:** `modules/surface/api/source/step_run.cpp` (`run.steps`, `:185-243`; the values of `PF-11` are written here);
  `modules/engine/execution/source/scheduler_execution_engine.cpp` (`run.submit`, `run.wait` with `latch`, `quiesce`, `errors`, `:497-604`);
  `modules/engine/execution/source/chain_run.cpp` (`chain.submit_step`, `chain.wait`, `chain.record_events`);
  `modules/engine/scheduler/source/task_scheduler.cpp` (`dispatch.task`, `dispatch.submit`, `dispatch.host`, `:571`, `:727`, `:635`; the worker
  threads call `Collector::register_this_thread` and `set_thread_name("sr-worker-N")` at the top of `dispatch_loop`, `:409`, so the
  first zone of a worker does not allocate); `modules/platform/device/source/device_queue_set.cpp` (`wait.queues`, `:269`);
  `modules/surface/api/source/runtime.cpp` (`wait.runtime`, `:218`); the READMEs of `execution` and `scheduler`.
  Levels per D8. D19 decides whether the distributed layer gets a part of this task.
- **Acceptance:** a blocking run of a graph on the scheduler path records `run.steps` containing `run.submit` and `run.wait`; a run of
  the same graph on the chain path records `chain.submit_step` and `chain.wait`; a worker thread's zones carry its name.
- **Build:** one `sr build`, then `sr run sr_execution_tests -- --gtest_filter='*RunZones*'`.
- **Tests:** `Medium_RunZones.TheSchedulerPathWritesSubmitAndWaitUnderTheRun`, `Medium_RunZones.TheChainPathWritesItsOwnZones`,
  `Medium_RunZones.WorkerThreadsCarryTheirNames`; the thread-name test reads the thread table of the record. The per-dispatch zones are
  at level 2 or 3 by D8, and the test compiles at the level it asserts.

### PF-10 Lock waits on `pending_mutex`

- **Files:** `modules/engine/scheduler/source/task_scheduler.cpp` (`:267`, `:285`, `:297`, `:316`, `:383`, `:497`, `:869`, `:896`, `:991`; not the
  park at `:451`); `modules/engine/scheduler/README.md`.
- **Acceptance:** with one thread holding `pending_mutex`, a second thread's `submit` writes one `lock_wait` zone whose duration is at least the time the first thread held it; an
  uncontended `submit` writes none.
- **Build:** one `sr build`, then `sr run sr_scheduler_tests -- --gtest_filter='*LockWait*'`.
- **Tests:** `Medium_PendingMutexLockWait.AContendedSubmitWritesAZone`, `Small_PendingMutexLockWait.AnUncontendedSubmitWritesNone`. The first needs a way to hold the mutex
  from the test: the scheduler exposes none, so the test uses a second scheduler thread blocked inside `submit_batch` through a task body that takes its time; if that cannot be made
  deterministic the contended case moves behind a seam and the task says so.
- **Waits on:** `PF-9` (the same file); RW-46 would move the injector out of `task_scheduler.cpp`, and this task follows it if it lands first.

### PF-11 Allocation zones, run counters and the worker buckets as values

- **Files:** `modules/platform/memory/source/usm_allocator.cpp` (`memory.allocate`, `memory.release` and a value of the bytes); `modules/surface/api/source/step_run.cpp` (the counters `run.tasks`, `run.steals`,
  `transfer.h2d_bytes` and `transfer.d2h_bytes`, written on every run); `modules/engine/execution/source/scheduler_execution_engine.cpp` (the worker bucket sums as the values `worker.busy`, `worker.stealing`,
  `worker.polling` and `worker.idle`, written on every run beside the counters); the READMEs.
- **Acceptance:** a run that copies a known number of bytes writes counters of exactly those bytes into its period, with or without an observer attached; an allocation writes a `memory.allocate` zone and a value of its size.
- **Build:** one `sr build`, then `sr run sr_api_tests` and `sr_memory_tests` with the filters below.
- **Tests:** `Medium_RunCounters.TransferBytesMatchTheExplicitCopies`, `Medium_RunCounters.WorkerBucketSumsAreWrittenOnEveryRun`, `Small_UsmAllocatorZones.AnAllocationWritesAZoneAndItsSize`.

### PF-12 Device timing is a per-run choice (D7, R20)

- **Files:** `CompiledGraph::profiling` becomes the armed value of the run in progress. `step_run.cpp` reads `wants_device_timing()` once per blocking run and per asynchronous step and stores it; the sentinel reads the armed value when it
  runs instead of capturing it at wiring; `chain_run.cpp` takes the profiling flavour from it; `plan_cache.cpp`, `region_composer.cpp` and `dynamic_graph.hpp` drop the timing term from staleness and from `rebuild_reason`, and the composer lists
  its nodes and captures the node table unconditionally. A timed run differs in one thing, the queue: the profiling-enabled flavour of the device's queue, built on first use. The plan holds the node table when idle and, after a timed run, the ledger at its
  high-water size; an untimed run costs one load and a branch.
- **Acceptance:** with an observer attached for one warm-up and two active periods, the record holds device spans for exactly the periods the capture window kept, and no plan was rebuilt.
- **Build:** one `sr build`, then `sr run sr_profiling_tests -- --gtest_filter='*Window*'`.
- **Tests:** `Medium_DeviceTimingWindow.SpansExistOnlyForTheRecordedPeriods`, `Medium_DeviceTimingWindow.CostsNoRebuild` (reads `compile_count()`).

### PF-13 The overhead benchmark

- **Files:** `tests/engine/execution/benchmark_scheduler_execution_engine.cpp` (an empty-node graph run at levels 0, 1, 2 with the gate closed and with it open);
  `tests/engine/execution/README.md`. The `performance` skill's measurement rules apply; the implementer loads it.
- **Acceptance:** the benchmark prints, per level and gate state, the time of one run of N empty nodes with its spread over fixed repetitions on a named machine, so that the budget in
  SPEC.md 1.9 is a number and not an estimate.
- **Build:** one `sr build` with benchmarks on, then `sr bench` for that program.
- **Tests:** a benchmark is not a test (`testing`); `tests/engine/execution/test_scheduler_execution_engine.cpp` stays green and unchanged.
- **Reports:** the numbers go in `REPORT.md` and, when a budget is missed, in `REMAINING_WORK.md` through `DOC-1`.

### PF-14 End to end: a session records a graph run

- **Files:** new `tests/surface/profiling/test_runtime_session.cpp` and its `README.md`. **Orchestrator:** the `CMakeLists.txt` of that folder.
- **Acceptance:** a SushiProfiler `Session` started with the runtime's registered parts, on a timeline with a window, over a graph of three named nodes run on the CPU SYCL device, returns a record that has the plan zones, the
  run zones, device spans with the `node`, `invocation`, `operation` and `layer` attributes joined to the three names, the cost attributes of the nodes that stated one and none on the others, a `submit_thread` that resolves to the
  thread whose `run.submit` or `dispatch.submit` zone encloses the span's `queued_ns`, the device and driver in `environment`, and zero dropped spans, with the runtime in a shared library and the host linking SushiProfiler as the
  host would. No period is read in the runtime: each span is in the period whose steady span holds its start.
- **Build:** one `sr build`, then `sr run sr_profiling_tests -- --gtest_filter='*Session*'`.
- **Tests:** `Medium_RuntimeSession.RecordsPlanRunAndDeviceSpansOfAGraph`, `Medium_RuntimeSession.JoinsEverySpanToANodeName`, `Medium_RuntimeSession.PutsEachSpanInThePeriodHoldingItsStart`,
  `Medium_RuntimeSession.ResolvesEverySubmitThreadToAnEnclosingZone`, `Medium_RuntimeSession.WritesTheCostAttributesWhereACostWasStated`, `Medium_RuntimeSession.ARunAsyncStepWaitedForLateStillLandsInItsPeriod`,
  `Medium_RuntimeSession.AStepWhoseWorkFinishedAfterTheLingerIsCountedInTheDrops`, `Medium_RuntimeSession.AnInferenceShapedGraphOnTheCpuDeviceRecordsEveryNode`,
  `Medium_RuntimeSessionStress.TwoRuntimesFeedOneSourceFromSeveralThreadsWhileTheConsumerCollects`, `Large_RuntimeSession.SpansOnTheGpuHaveAPairingUncertaintyBelowTheirDuration` (skipped without a GPU),
  `Large_RuntimeSession.TwoDevicesGetOneTrackAndOnePairingEach` (skipped without a second device).

### PF-15 The build without SushiProfiler

- **Files:** none; a configuration check. The implementer's part is any `#include <SushiProfiler/...>` found outside the `profile` and `profiling` modules, which the checker below reports.
- **Acceptance:** the tree configures, builds and passes `sr test --label small --label medium` with `SR_ENABLE_PROFILER` off and no SushiProfiler checkout on the machine's search path, and
  `tools/layering` finds no include of SushiProfiler outside the two modules.
- **Build:** one `sr build` with the option off.
- **Tests:** the existing suites; `tests/package/main.cpp` still builds against the installed package.

### PF-16 Graph label and node origin (R15, R16)

Both are additions: no header sent to a consumer changes.

- **Files:** `modules/surface/api/include/SushiRuntime/api/graph/graph.hpp`, `dynamic_graph.hpp` and `plumbing/plan_cache.hpp`, `region_composer.hpp` (`set_label(std::string)`; the label is read when a plan is built, so a changed label applies at the next build and forces none);
  `node_options.hpp` (`std::string_view origin`), `detail/node_stamp.hpp`, `source/recorder.cpp` (the recorder copies the origin as it copies the name), `modules/engine/task/include/SushiRuntime/task/task_types.hpp` (`TaskMetadata::origin`),
  `plan_view.hpp` (`PlanNode::origin`), `modules/foundation/report/include/SushiRuntime/report/node_table.hpp` (`NodeTable::label`, `NodeTableRow::origin`), `modules/engine/graph/source/compiled_graph.cpp`
  (the table takes both), `modules/surface/profiling/source/device_source.cpp` (the attributes `K_GRAPH` and `K_ORIGIN`, each interned once per table and per row); the READMEs.
- **Acceptance:** two graphs with equal node indices give spans with different `graph` values; a node recorded with an origin gives a span with it, and one without gives none; a graph with no label gives no `graph` attribute.
- **Build:** one `sr build`, then `sr run sr_report_tests`, `sr_graph_tests`, `sr_api_tests` and `sr_profiling_tests` with the filters below.
- **Tests:** `Small_NodeTable.CarriesTheLabelAndTheOriginOfItsRows`, `Medium_GraphLabel.ALabelChangeAppliesAtTheNextBuildAndForcesNone`, `Medium_NodeOptions.OriginIsCopiedAtRecordTime`,
  `Small_DeviceSource.WritesTheGraphLabelWhenOneIsSet`, `Small_DeviceSource.TwoGraphsWithEqualNodeIndicesDifferByGraph`, `Small_DeviceSource.WritesTheOriginOfANodeThatStatesOne`, `Small_DeviceSource.OmitsTheOriginAndTheLabelWhenEmpty`.

### PF-17 Graph identity (R17)

Additive: `RunObservation` gains one field at its end and `NodeTable` one accessor.

- **Files:** `modules/surface/api/include/SushiRuntime/api/detail/graph_id.hpp` and `source/graph_id.cpp` (`GraphId`: one process-wide atomic counter from 1, move-only, a moved-from value is 0); `plumbing/plan_cache.hpp` and
  `region_composer.hpp` hold it and expose `id()`; `graph.hpp` and `dynamic_graph.hpp` re-export it; `source/plan_cache.cpp` and `region_composer.cpp` stamp it into the plan (`CompiledGraph::graph_id`); `node_table.hpp`
  (`NodeTable::graph_id()`); `report/run_observer.hpp` (`RunObservation::graph_id`); `scheduler_execution_engine.cpp` and `step_run.cpp` fill it on the blocking and the asynchronous path; the READMEs.
- **Acceptance:** two graphs on one runtime with one observer give observations that carry their own graph's id; a `DynamicGraph` run carries the `DynamicGraph`'s id; ids are non-zero and differ; a moved-from graph reports 0.
- **Build:** one `sr build`, then `sr run sr_api_tests -- --gtest_filter='*GraphId*'` and `sr_report_tests`.
- **Tests:** `Medium_GraphId.TwoGraphsOnOneRuntimeObserveTheirOwnIds`, `Medium_GraphId.ADynamicGraphRunCarriesItsOwnId`, `Medium_GraphId.AnAsyncStepCarriesTheIdOfItsGraph`, `Small_GraphId.IdsAreNonZeroDistinctAndMovedFromReportsZero`,
  `Small_NodeTable.CarriesTheGraphId`.

### PF-18 The source name header (R18)

Additive: one public header, and the constant leaves `profile_keys.hpp`.

- **Files:** new `modules/surface/profiling/include/SushiRuntime/profiling/source_names.hpp`; `source/device_source.cpp` and `source/register_profiler_parts.cpp` use it; `modules/foundation/profile/include/SushiRuntime/profile/profile_keys.hpp`
  drops its copy; the READMEs of `profile` and `profiling`, `docs/reference/PROFILER_KEYS.md` and the changelog.
- **Acceptance:** no literal `sushiruntime.device` remains outside the header; the header includes `<string_view>` only.
- **Build:** one `sr build`, then `sr run sr_profiling_tests -- --gtest_filter='*SourceNames*'`.
- **Tests:** `Small_SourceNames.TheDeviceSourceIsNamedSushiruntimeDevice`, `Medium_SourceNames.TheSourceTheRegistryMakesCarriesThatName`.

### PF-19 The live session: max delay and paused timing (R21)

Additive: the gate belongs to the internal `observe_runs`, and `Runtime::add_run_observer` is unchanged.

- **Files:** `modules/surface/profiling/include/SushiRuntime/profiling/device_source.hpp` and `source/device_source.cpp` (`max_delay_ns()`, a constant of 2 000 ms below the profiler's 60 000 ms limit, with a constructor argument so a test states its own);
  `modules/engine/execution/include/SushiRuntime/execution/runtime_context.hpp` and `source/runtime_context.cpp` (an observer may carry a gate, a plain function returning whether it wants timing now; `wants_device_timing()` is true when any ungated observer
  is added or any gate is open; `notify_run` skips an observer whose gate is closed); `source/register_profiler_parts.cpp` adds its feeds with the gate `Collector::sources_wanted`; the READMEs, `PROFILER_KEYS.md` and the changelog.
- **Acceptance:** the source reports its delay and the session holds a period that long; a span that finishes within it lands in its period and none is counted late; a paused session runs the graph with no device timing and no records, and a resumed one times
  the next run; an ungated observer always wants timing.
- **Build:** one `sr build`, then `sr run sr_profiling_tests` and `sr_execution_tests` with the filters below.
- **Tests:** `Small_DeviceSource.ReportsItsMaxDelayBelowTheProfilersLimit`, `Small_DeviceSource.ASpanPastTheDelayIsStillCountedLate`, `Medium_RuntimeContextObservers.AnUngatedObserverAlwaysWantsTiming`, `Medium_RuntimeContextObservers.AGatedObserverWantsTimingOnlyWhileItsGateIsOpen`, `Medium_RuntimeContextObservers.ADeliveryToAGatedObserverWaitsForItsGate`,
  `Medium_RuntimeSession.ASpanThatFinishesWithinTheMaxDelayLandsInItsPeriodAndIsNotLate`, `Medium_DeviceTimingWindow.APausedSessionCostsNoDeviceTiming`.

## Stage 6: closing

### LG-7 Use SushiLog's throttles; the per-element lines to `TRACE`

- **Files:** `modules/extension/offload_coordination/source/rule_placement.cpp` and `include/.../rule_placement.hpp` (`is_first_warning`, `warned_`, `forget_warnings` are replaced
  by SushiLog's log-once, L1); `modules/extension/distributed/source/tcp_transport.cpp` (`:70-75` replaced by L2); the local counters of `LG-3` replaced by L3 where it fits;
  `task_graph.cpp:244`, `:296`, `:362`, `:461`, `:597`, `task_scheduler.cpp:617`, `scheduler_execution_engine.cpp:602` move to `TRACE`; the five test files' own capture sinks move to the shared one.
  **Orchestrator:** the SushiLog pin (`cli/sushistack.deps.toml:71-73`, D21).
- **Acceptance:** the two hand-rolled throttles are gone, the warning of a rule and the TCP attempt lines are unchanged as the existing tests read them, and a `NDEBUG` build compiles the six per-element lines out.
- **Build:** one `sr build`, then `sr run sr_offload_coordination_tests` and `sr_distributed_tests`.
- **Tests:** the existing `tests/extension/offload_coordination/test_offload_dispatcher.cpp` and `tests/extension/distributed/test_distributed.cpp` cases that read the first-warning and attempt lines, unchanged (`refactor`);
  no new test for the level move.

### DOC-1 The manual, the design topic, the changelog, the backlog

- **Files:** `docs/design/PROFILING/README.md` per D22 with the phases `P1` (capture and observer), `P2` (device source and registration), `P3` (the runtime's zones) and `P4` (logging adoption), and the `docs/design/README.md` map;
  `docs/design/REMAINING_WORK.md` (what this pass leaves: the switch of device timing without a rebuild, the budget misses of `PF-13`, the distributed layer's timing, the typed deferred refusal of `Recorder`, the removal
  of `SR_LOGGER_SET_SYNC_MODE`, the use of SushiLog's throttles); `docs/reference/CHANGELOG.md` (one entry per removed member naming its replacement, one per added public name, `!` on the commit subject the orchestrator writes);
  `docs/reference/PROFILER_KEYS.md`; the manual pages that name a changed or removed member (`git grep -n` of every name over `*.md`), `docs/.docs_truth_allow` for the names the source cannot hold; this folder's status and `**Implements:**` line.
- **Acceptance:** `tools/documentation/check_docs_layout.py`, `check_changelog.py`, `check_docs_truth.py`, `check_design_topics.py` and `tools/logs/check_logging.py` pass on the tree, and every renamed or added public name
  (`RunObserver`, `RunObservation`, `NodeTable`, `NodeCost`, the new `NodeEventRecord` fields, `Runtime::add_run_observer`, `register_profiler_parts`, `device_track_name`, `note_dispatch_event`) appears in the manual.
- **Build:** none; the checkers are the check.
- **Tests:** none.

## Decisions taken in this pass

Choices the rulings did not cover, taken SOLID-first and then by what rebuilds least. Each carries its reason.

| Choice | Reason |
| --- | --- |
| A body hands over the events it submitted with `note_dispatch_event(const sycl::event&)`, collected by a thread-local scope the dispatcher opens, and keeps returning one event | The body signature and `execute_host` stay as they are, so no existing body and no header that names them changes |
| The ledger takes two parameter structs, `DispatchFacts` and `DispatchTimes`, in one `record()` call | Nine positional arguments of one type invite a swapped pair |
| Host-native records stay on the steady clock and carry `K_HOST_DEVICE_INDEX`; the observation has no host pairing | A conversion to an event clock and back adds uncertainty for no reader |
| `DropCause` and `DropCounts` live in `report`, next to `RunObservation`, not in `observability` | `report` is the lower tier and the observation carries them |
| `RunObserverHandle` calls a `std::function` the runtime builds, and holds the context weakly | `report` then needs no knowledge of the context, and a handle may outlive its runtime |
| Observers are added to the context through `observe_runs(context, observer)`, which `Runtime::add_run_observer` and the parts object both call | One place builds the handle |
| `runtime.hpp` forward-declares `RunObserver` and `RunObserverHandle`; a caller includes `report/run_observer.hpp` | The widely included header gains no include |
| A blocking run hands its observation over at the end of `run_steps`; an asynchronous step hands it over from its sentinel through `submit_observed` | A multi-step blocking run has one ledger for all steps, and a step waited for late must lose nothing (R3) |
| The track is `<node>/<device index>/<device name>`, the host track `<node>/host` | The node separates two runtimes on one timeline (D11); the index separates two devices of one name |
| The source queue holds 64 observations | A run is one observation, and 64 runs outlast a linger of a few periods at the frame rates the profiler targets |
| `RuntimeProfilerParts::attach(const Runtime&)` adds a further runtime before a session starts | D11 asks for one source fed by every runtime, and the registry refuses a second source of the name |
| `AdvancedControls::is_profiling_enabled()` is removed with `RuntimeConfig::profiling` | Its only meaning was the removed setting |
| `DeviceQueueSet`'s constructor keeps its `profiling` parameter, and the runtime passes `false` | The module is internal and has its own tests; a profiling queue is built lazily by flavour |
| `NodeDType` is an enum with the names `f32`, `f16`, `bf16` and `i8` | A closed set the profiler writes as a name attribute |
| Each `CompiledGraph` keeps one scratch `RunObservation` that every run clears and refills | A run that observes must not allocate per tick; the observer already receives a const reference |
| `profile_config.hpp.in` guards each macro with `#ifndef`, so a test unit may state its own level before the include | The gate tests compile one call at four levels; `profile.hpp` itself has no default, as R19 asks |
| The id counter lives in `source/graph_id.cpp`, behind an exported function | One counter per process even when a host loads the library through another image |
| The gate is a plain function pointer, `bool (*)() noexcept`, held per observer in the context | A relaxed load per observer per run, no allocation, and `RunObserver` and `add_run_observer` keep their frozen shape |
| The source reports 2 000 ms | A step of a frame loop finishes within a few frames of its period; a longer bound holds periods open for nothing, and the profiler refuses above 60 000 ms |
| A timed run is chosen when the run is armed, and the node table is always built | The plan then needs no rebuild when an observer comes or goes, and the table costs one string per registration index |
| `set_label` stores the label on the plan cache and the composer, and the table copies it at build | A label change then rebuilds nothing, as R15 asks |
| The worker bucket values are read and reset by `write_run_values()` | An unobserved run must not report the buckets of an earlier observed one |

## Breaking changes and migration

D24 removes the members below with no deprecation release (the library is before 1.0, which `api-stability` allows). Each has one changelog entry naming its replacement, and the commit subject carries `!`.

| Removed | Replacement |
| --- | --- |
| `RunReport::node_events`, `RunReport::clock` | `RunObservation::events` and `RunObservation::device_clocks`, received by a `RunObserver` added with `Runtime::add_run_observer` |
| `RunReport::node_timings`, `NodeTiming`, `RunReport::worker_timings`, `WorkerTiming`, `RunReport::scheduling_overhead_ms` | The profiler: the device spans of `sushiruntime.device`, the values `worker.busy`, `worker.stealing`, `worker.polling`, `worker.idle` |
| `RunReport::total_duration_ms`, `RunReport::graph_analysis_duration_ms` | The zones `run.steps` and `plan.build` |
| `RunReport::host_to_device_bytes`, `RunReport::device_to_host_bytes` | The counters `transfer.h2d_bytes` and `transfer.d2h_bytes`; the cumulative `AdvancedControls::host_to_device_bytes()` and `device_to_host_bytes()` stay |
| `RuntimeConfig::profiling`, `AdvancedControls::is_profiling_enabled()` | Adding an observer requests device timing |
| `Graph::set_profiling`, `Graph::is_profiling_requested`, the `DynamicGraph` twins | The same attachment |
| `NodeEventRecord` layout (40 to 48 bytes) | The two appended fields `device_index` and `submit_thread` |

Consumers outside this repository, whose edits are not part of this plan:

- SushiAI `modules/layers/train/source/trainer.cpp` stops reading `NodeTiming` totals and attaches `register_profiler_parts`.
- SushiBLAS `tests/foundation/graph_primitives/`, four tests that read `node_events` launched widths, install a test observer with `Runtime::add_run_observer` and read `RunObservation::events`.
- SushiEngine attaches an observer in its runtime backend; its mirror of `NodeEventRecord` (`runtime_backend.hpp`) grows to 48 bytes and takes the per-device clocks; matter's `node_export.cpp`
  converts each device's records with that device's pairing, and matter's device graph set stops calling the graph profiling switch.
