# SushiRuntime adopts SushiProfiler, and logs where it is silent: plan

**Status:** Open — written 2026-10-26; nothing is built, and no task starts before the owner has answered the decisions it waits on (SPEC.md, section 5).

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

The owner chooses how the plan runs, inline in hierarchy order or in waves (`multi-agent-work`). A wave proposal is at the end;
it is a proposal.

## Before any task: decisions and work in other repositories

| What | Blocks |
| --- | --- |
| D1, D2, D23 (module homes, the library boundary, the pin) | `PF-0` and every `PF` task |
| D3, D4, D5, D6 (feed, node table, record fields, pairing) | `PF-2` to `PF-6` |
| D7, D8, D9, D10, D11, D12 | `PF-5`, `PF-8` to `PF-13` |
| D13, D14, D15, D16, D17 | `BW-1`, `LG-1`, `LG-4`, `LG-6` |
| D18, D21 | `LG-3`, `LG-7` |
| D19, D20, D22 | scope of `PF-9`, the order of the build edits, `DOC-1` |
| SushiProfiler G1 (shared collector) | `PF-1` acceptance onward: no zone of the runtime reaches a host's session before it |
| SushiProfiler G2, G3, G4, G6 | `PF-6` |
| SushiProfiler G5 | `PF-12` if D7 is (a) |
| SushiProfiler G8 (the registration pattern) | `PF-7` |
| SushiLog L1, L2, L3, L7 | `LG-7`; `LG-0` is dropped if L7 ships first |

These are not tasks of this plan. Their acceptance criteria belong to the repository that makes them.

## The tasks

| Id | Task | Waits on | Build files (orchestrator) |
| --- | --- | --- | --- |
| BW-1 | Log categories: build edges and tier entries | D13 | yes |
| LG-0 | A log capture helper for the runtime's tests | D18 | yes |
| LG-1 | `api` category; A1 to A5 | BW-1, LG-0 | tests list |
| LG-2 | Scheduler and execution: E1, E2, E6, E7, E10 | LG-0 | tests list |
| LG-4 | Memory and device: M1 to M3 | LG-0 | tests list |
| LG-5 | Process and network: P1 to P4 | BW-1, LG-0 | tests list |
| LG-6 | The assertion path and the sync-mode macro: F1, D17 | D16, D17 | yes if D16 is (a) |
| PF-0 | The pin, the options and the package file | D1, D2, D23 | yes |
| PF-1 | The `profile` front | PF-0, SushiProfiler G1 | yes |
| PF-2 | One writer of a node event record | none | no |
| PF-3 | Report types: node table, record fields, pairing | D4, D5, D6, PF-2 | tests list |
| PF-4 | Fill the node table and the new fields | PF-3 | tests list |
| LG-3 | Counted drops: E3, E4, E5, E8, E9, E11 | PF-4, D18, SushiLog L3 or a local counter | tests list |
| PF-5 | The run observer and the period at submit | D3, PF-4, G4 | tests list |
| PF-6 | The device source | PF-1, PF-5, D10, D11, G2, G3, G6 | yes (module, tier) |
| PF-7 | The environment contributor and the registration function | PF-6, D12, G8 | yes |
| PF-8 | Zones of the plan build | PF-1 | no |
| PF-9 | Zones of the run, the chain and the dispatch; thread names | PF-1, D8, D19 | no |
| PF-10 | Lock waits on `pending_mutex` | PF-9 | no |
| PF-11 | Allocation zones, run-end counters and the worker buckets as values | PF-1, D9 | no |
| PF-12 | Device timing follows the capture window | D7, PF-5, G5 | tests list |
| PF-13 | The overhead benchmark | PF-8 to PF-11 | tests list |
| PF-14 | End to end: a session records a graph run | PF-7, PF-9 | tests list |
| PF-15 | The build without SushiProfiler | PF-14 | no |
| LG-7 | Use SushiLog's throttles; move the per-element lines to `TRACE` | SushiLog L1 to L3, D18, D21 | yes (pin) |
| DOC-1 | The manual, the design topic, the changelog, the backlog | all | no |

`LG-3` sits after `PF-4`: `PF-2` makes one writer of a node event record, and the counted drop goes in that one place.

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
  `Medium_GraphRebuild.LogsTheReasonOfEachRebuild` (A4, four reasons); new `tests/surface/api/test_api_log_category.cpp`,
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
  of D23); `cmake/Dependencies.cmake` (a `SUSHIPROFILER_DIR` block beside SushiLog's `:30-48`, taken only when
  `SR_ENABLE_PROFILER` is on); `cmake/SushiRuntimeOptions.cmake` (`SR_ENABLE_PROFILER` default ON, `SR_PROFILE_LEVEL`);
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

- **Files:** `modules/foundation/profile/include/SushiRuntime/profile/profile.hpp` (the macros `SR_ZONE`, `SR_ZONE_FINE`,
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

### PF-2 One writer of a node event record (refactor)

- **Files:** `modules/platform/observability/include/SushiRuntime/observability/profiling_ledger.hpp` (a function that claims the slot,
  fills the record and writes it, and says why it did not);
  `modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp` (`:393-428`),
  `modules/engine/execution/source/chain_run.cpp` (`:277-306`), `modules/engine/scheduler/source/task_scheduler.cpp` (`:630-660`);
  `modules/platform/observability/README.md`.
- **Acceptance:** the three call sites call that one function and every existing test of `sr_observability_tests`, `sr_scheduler_tests`
  and `sr_execution_tests` passes unchanged.
- **Build:** one `sr build`, then the three suites.
- **Tests:** none new (`refactor`); `tests/platform/observability/test_profiling_ledger.cpp` and the event polling tests are the check.

### PF-3 Report types: node table, record fields, pairing

- **Files per D4, D5, D6:** `modules/foundation/report/include/SushiRuntime/report/run_report.hpp` and a new `node_table.hpp`
  (`NodeTableRow`: registration index, name, `OperationID`, host-native flag, multiplicity, device; `NodeTable`);
  `NodeEventRecord` gains the fields D5 chose; `ClockPairing` per device if D6 is (b); `reset()` clears them keeping capacity;
  `modules/foundation/report/README.md`. **Orchestrator:** `tests/foundation/report/CMakeLists.txt` for the new test file.
- **Acceptance:** `RunReport::reset()` returns the table and the new fields to their constructed state and keeps their capacity, and a table row is found by registration index.
- **Build:** one `sr build`, then `sr run sr_report_tests`.
- **Tests:** `tests/foundation/report/test_run_report.cpp`: `Small_RunReport.ResetClearsTheNodeTableAndKeepsItsCapacity`;
  new `tests/foundation/report/test_node_table.cpp`: `Small_NodeTable.FindsARowByRegistrationIndex`,
  `Small_NodeTable.AnIndexPastTheTableHasNoRow`; `Small_NodeEventRecord.HasTheSizeTheReportPromises` (a `static_assert` on the size D5 chose,
  because the record is public and exported by value).
- **Note:** adding a member to a public struct raises the minor version (`SushiRuntimeLibrary.cmake:15-20`); `api-stability` sets the changelog line, which `DOC-1` writes.

### PF-4 Fill the node table and the new fields

- **Files:** `modules/engine/graph/source/compiled_graph.cpp` and `include/.../compiled_graph.hpp` (`capture_profiling_state`, `:69-87`,
  builds the table once per plan, as far as D4 says); `modules/engine/execution/source/scheduler_execution_engine.cpp`
  (`collect_profiling`, `:367-443`, copies or shares the table, fills the device of each row from `cached_queue_index_` or
  `chain.queue_index`, and sets the per-device pairing if D6 is (b)); the one writer of `PF-2` fills `device_index` and `submit_thread`
  (`Collector::thread_index()` through the front); `modules/engine/graph/README.md`, `modules/engine/execution/README.md`.
- **Acceptance:** a profiled run of a graph of three named nodes returns a report whose table has the three rows with their names and
  operations and whose records name the device each ran on.
- **Build:** one `sr build`, then `sr run sr_execution_tests` and `sr_graph_tests`.
- **Tests:** `tests/engine/graph/test_compiled_graph.cpp`: `Small_CompiledGraph.CapturesOneTableRowPerRegistrationIndex`,
  `Small_CompiledGraph.ASharedIndexKeepsItsMultiplicity`; `tests/engine/execution/test_scheduler_execution_engine.cpp`:
  `Medium_CollectProfiling.TheTableAndTheRecordsJoinByNodeIndex`, `Medium_CollectProfiling.APlanRunOnTheChainFillsTheSameTable`;
  `tests/surface/api/graph/test_graph.cpp`: `Medium_GraphProfiling.ReportsTheTableOfItsPlan`. A two-device case is `Large_` and
  skipped without a second device.

### LG-3 Counted drops (E3, E4, E5, E8, E9, E11)

It waits on `PF-2` (one writer) and on D18 (a SushiLog counter, or a local one), and follows `PF-4` because both edit `collect_profiling`.

- **Files:** `profiling_ledger.hpp` (a drop count per cause, which replaces the debug-only assert at `:183` so both builds behave the same);
  the one writer of `PF-2`; `modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp` (`:354-370`: the stall-drive count and the replaced
  error); `modules/engine/scheduler/source/task_scheduler.cpp` (`:576-597`, `:695-696`); `modules/engine/graph/source/task_graph.cpp` (`:287`, `:290`, `:353`, `:356`, `:452`, `:455`);
  `scheduler_execution_engine.cpp` (`collect_profiling` writes the one line per run, `:379`, `:395`; `complete()` writes the cancel and skip counts); the READMEs.
- **Acceptance:** a run that loses dispatches to a full ledger row, to an unreadable event and to a missing queue logs one `WARN` under `sushiruntime.execution` naming each cause and its
  count; a run that loses none logs nothing; a cancelled run logs one `INFO` with the number of tasks it skipped.
- **Build:** one `sr build`, then `sr run sr_observability_tests`, `sr_execution_tests`, `sr_scheduler_tests`.
- **Tests:** `Small_ProfilingLedger.CountsADispatchThatFoundItsRowFull`, `Small_ProfilingLedger.OverflowBehavesTheSameInDebugAndRelease`;
  `Medium_CollectProfiling.LogsOneLineForTheDispatchesItLost`, `Medium_CollectProfiling.LogsNothingWhenNoneWasLost`;
  `Medium_TaskSchedulerCancel.LogsTheTasksASkippedRunLeftUnrun`; `Small_EventPolling.CountsAStallDriveAndKeepsTheFirstFailure` (the replaced error is a regression test: it fails before the change).

### PF-5 The run observer and the period at submit

- **Files per D3 (a):** a `RunObserver` interface in `modules/foundation/report/include/SushiRuntime/report/run_observer.hpp`;
  `modules/engine/execution/include/.../runtime_context.hpp` and `source/runtime_context.cpp` (hold it as `install_offload` holds the
  coordinator, with the same lifetime rule: removed before the context goes); `modules/surface/api/include/SushiRuntime/api/detail/step_run.hpp`
  and `source/step_run.cpp` (the period is read in `submit_step`, `:269`, and stored in `AsyncRunState`; `run_steps` and `finish_step`
  call the observer after `collect_profiling`, `:240-241`, `:299-300`); `Runtime` gets `set_run_observer` (public, per D12).
  Under D3 (b) this task shrinks to the period parameter of the public hand-off call and the file list is the host's.
- **Acceptance:** an observer installed on a runtime is called once per finished run with that run's report and the period read when
  its work was submitted, for a blocking run and for a `run_async` step that is waited for after the period moved on.
- **Build:** one `sr build`, then `sr run sr_api_tests -- --gtest_filter='*RunObserver*'`.
- **Tests:** `tests/surface/api/graph/test_run_handle.cpp`: `Medium_RunObserver.SeesEachBlockingRunOnce`,
  `Medium_RunObserver.SeesAnAsyncStepWithThePeriodOfItsSubmit`, `Medium_RunObserver.IsNotCalledWhenTheRunFailedBeforeItStarted`;
  `tests/engine/execution/test_runtime_context.cpp`: `Small_RuntimeContext.RemovesTheObserverBeforeItsOwnerGoes`.

### PF-6 The device source

- **Files:** new module `profiling` (tier per D1; the files below are the same under any tier):
  `include/SushiRuntime/profiling/device_source.hpp`, `source/device_source.cpp` (the SushiProfiler `Source`: `begin`, `collect`, `end`),
  `source/run_queue.hpp` and `.cpp` (bounded, mutexed queue of `(period, table, records, pairing)`, drops the oldest and counts when full),
  `source/clock_conversion.hpp` (event time to record time through the pairing, saturating the uncertainty to 32 bits), `README.md`.
  **Orchestrator:** `modules/<tier>/profiling/CMakeLists.txt`, the tier table, `tests/<tier>/profiling/CMakeLists.txt`.
- **Acceptance:** fed one report of known records, the source's `collect(period)` adds exactly the spans of that period to a `SpanSink`
  with the times, tracks, names and attributes SPEC.md 1.3 lists, and refuses none.
- **Build:** one `sr build`, then `sr run sr_profiling_tests`.
- **Tests:** `Small_DeviceSource.ConvertsEventTimesThroughThePairing`, `Small_DeviceSource.SetsNodeInvocationAndWidthAttributes`,
  `Small_DeviceSource.OmitsAWidthThatIsUnknown`, `Small_DeviceSource.PutsAHostNodeOnTheHostTrack`,
  `Small_DeviceSource.ASpanOfALaterPeriodWaitsForIt`, `Small_RunQueue.DropsTheOldestWhenFullAndCountsIt`,
  `Small_RunQueue.EndDropsWhatWasNeverCollected`, `Small_ClockConversion.SaturatesTheUncertaintyAt32Bits`. The tests drive the real
  SushiProfiler `SpanSink` over a `SpanTable`, with no device.
- **Waits on:** G2 (the layer), G3 (names after `begin`), G4 (the timeline), G6 (late spans). Under D23 (c) the source keeps the `SourceContext`
  and G3 does not wait.

### PF-7 The environment contributor and the registration function

- **Files:** in the `profiling` module: `include/.../profiling/runtime_environment.hpp` and `source/runtime_environment.cpp`
  (`EnvironmentContributor`: the facts of SPEC.md 1.8, compiled inside the runtime library so the `build` facts are its own),
  `include/.../profiling/register_profiler_parts.hpp` and its source (the function and the owning handle of D12), the module README.
  **Orchestrator:** the install rules for the public header (`cmake/SushiRuntimeInstall.cmake` if the module is public).
- **Acceptance:** starting a SushiProfiler session with the registry the function filled records the device name, kind, backend, driver
  and the runtime's build type in the record's `environment` and `build`, and refuses a second registration of the same name.
- **Build:** one `sr build`, then `sr run sr_profiling_tests`.
- **Tests:** `Medium_RuntimeEnvironment.StatesEveryDeviceOfTheRuntime`, `Medium_RuntimeEnvironment.StatesTheBuildTypeAsABuildFact`,
  `Small_RegisterProfilerParts.RefusesASecondRegistration`, `Small_RegisterProfilerParts.TheHandleOutlivesTheSession`. Where the driver
  version is not answerable on a backend the fact reads `unknown` and a test pins that.

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

### PF-11 Allocation zones, run-end counters and the worker buckets as values

- **Files:** `modules/platform/memory/source/usm_allocator.cpp` (`memory.allocate`, `memory.release` and a value of the bytes, `:101`, `:116`);
  `modules/surface/api/source/step_run.cpp` (counters `transfer.h2d_bytes`, `transfer.d2h_bytes`, steals and tasks at `:229-237`; the worker bucket sums as values per D9 (a));
  the READMEs.
- **Acceptance:** a run that copies a known number of bytes writes counters of exactly those bytes into its period; an allocation writes a `memory.allocate` zone and a value of its size.
- **Build:** one `sr build`, then `sr run sr_api_tests` and `sr_memory_tests` with the filters below.
- **Tests:** `Medium_RunCounters.TransferBytesMatchTheExplicitCopies`, `Medium_RunCounters.WorkerBucketSumsAreWrittenAsValues`,
  `Small_UsmAllocatorZones.AnAllocationWritesAZoneAndItsSize`.

### PF-12 Device timing follows the capture window (per D7)

- **Files by option.** (a) the host adapter in `profiling` toggles `Graph::set_profiling` from a window notice that SushiProfiler gap G5 provides; no runtime file changes
  beyond `PF-5`. (b) the runtime requests profiling for the session's length: `modules/surface/api/source/runtime.cpp`.
  (c) `modules/engine/execution/source/scheduler_execution_engine.cpp` (the sentinel reads the flag per run, `:530-549`) and
  `modules/surface/api/source/plan_cache.cpp` (`set_profiling` no longer marks the plan dirty, `plan_cache.hpp:97-103`); `modules/engine/execution/README.md`.
- **Acceptance:** with a window of one warm-up and two active periods, the record holds device spans for exactly those three periods, and the plan was built the number of times D7's option gives (a: two extra, b: none, c: none).
- **Build:** one `sr build`, then `sr run sr_profiling_tests -- --gtest_filter='*Window*'`.
- **Tests:** `Medium_DeviceTimingWindow.SpansExistOnlyForTheRecordedPeriods`, `Medium_DeviceTimingWindow.CountsTheRebuildsItCosts` (reads `compile_count()`, `graph.hpp:174`).

## Stage 5: measurement and the proof

### PF-13 The overhead benchmark

- **Files:** `tests/engine/execution/benchmark_scheduler_execution_engine.cpp` (an empty-node graph run at levels 0, 1, 2 with the gate closed and with it open);
  `tests/engine/execution/README.md`. The `performance` skill's measurement rules apply; the implementer loads it.
- **Acceptance:** the benchmark prints, per level and gate state, the time of one run of N empty nodes with its spread over fixed repetitions on a named machine, so that the budget in
  SPEC.md 1.9 is a number and not an estimate.
- **Build:** one `sr build` with benchmarks on, then `sr bench` for that program.
- **Tests:** a benchmark is not a test (`testing`); `tests/engine/execution/test_scheduler_execution_engine.cpp` stays green and unchanged.
- **Reports:** the numbers go in `REPORT.md` and, when a budget is missed, in `REMAINING_WORK.md` through `DOC-1`.

### PF-14 End to end: a session records a graph run

- **Files:** new `tests/profiling/` mirror under the module's tier: `test_runtime_session.cpp`; `README.md` of that folder. **Orchestrator:** its `CMakeLists.txt`.
- **Acceptance:** a SushiProfiler `Session` started with the runtime's registered parts, on a timeline with a window, over a graph of three named nodes run on the CPU SYCL device,
  returns a record that has the plan zones, the run zones, device spans with the `node`, `invocation` and `operation` attributes joined to the three names, the period of each
  span equal to the period the run was submitted in, the device and driver in `environment`, and zero dropped spans, with the runtime in a shared library and the host
  linking SushiProfiler as the host would.
- **Build:** one `sr build`, then `sr run sr_profiling_tests -- --gtest_filter='*Session*'`.
- **Tests:** `Medium_RuntimeSession.RecordsPlanRunAndDeviceSpansOfAGraph`, `Medium_RuntimeSession.JoinsEverySpanToANodeName`,
  `Medium_RuntimeSession.PutsEachSpanInThePeriodOfItsSubmit`, `Medium_RuntimeSession.ARunAsyncStepFinishedLateStillArrives` (fails until G6),
  `Large_RuntimeSession.SpansOnTheGpuHaveAPairingUncertaintyBelowTheirDuration` (skipped without a GPU), `Large_RuntimeSession.TwoDevicesGetOneTrackEach` (skipped without a second device).

### PF-15 The build without SushiProfiler

- **Files:** none; a configuration check. The implementer's part is any `#include <SushiProfiler/...>` found outside the `profile` and `profiling` modules, which the checker below reports.
- **Acceptance:** the tree configures, builds and passes `sr test --label small --label medium` with `SR_ENABLE_PROFILER` off and no SushiProfiler checkout on the machine's search path, and
  `tools/layering` finds no include of SushiProfiler outside the two modules.
- **Build:** one `sr build` with the option off.
- **Tests:** the existing suites; `tests/package/main.cpp` still builds against the installed package.

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

### DOC-1 The manual, the design topic, the changelog, the backlog (orchestrator)

- **Files:** `docs/design/PROFILING/README.md` per D22 and `docs/design/README.md` map; `docs/design/REMAINING_WORK.md` (entries for what is left: PF-13's misses, RW for D15 (b), D9 (b));
  `docs/reference/CHANGELOG.md` (one line per landed task, as `CHANGELOG_FORMAT.md`); `docs/reference/PROFILER_KEYS.md` (the attribute keys `PF-1` declared, as the profiler's design asks);
  `docs/guides/` a profiling guide page and a logging page, `docs/architecture/ARCHITECTURE.md`, `docs/guides/INTEGRATION.md`, `docs/guides/FAQ.md` and `docs/reference/GLOSSARY.md` where they mention profiling or
  logging (`git grep -n -i 'profil' -- '*.md'` over the manual, as the `documentation` skill requires); `docs/.docs_truth_allow` for the names the source cannot hold; this folder's status and `**Implements:**` line.
- **Acceptance:** `tools/documentation/check_docs_layout.py`, `check_changelog.py`, `check_docs_truth.py`, `check_design_topics.py` and `tools/logs/check_logging.py` pass on the tree, and every renamed or added public name
  (`RunObserver`, `NodeTable`, the new `NodeEventRecord` fields, the registration function) appears in the manual.
- **Build:** none; the checkers are the check.
- **Tests:** none.

## A wave proposal

Offered only if the owner picks waves. Files are disjoint inside a wave; a `*` marks a task that edits `task_scheduler.cpp` and so never shares a wave with another `*`; `PF-9` and `PF-11` both edit `step_run.cpp` and sit in different waves.

| Wave | Tasks | What closes it |
| --- | --- | --- |
| 1 | BW-1, PF-0 (orchestrator, with the owner's decisions), LG-0 | one `sr build` each |
| 2 | LG-4, LG-5, LG-6 | one `sr build`, the three suites |
| 3 | LG-1, LG-2 \*, PF-1 | one `sr build`, the three suites |
| 4 | PF-2 \* | one `sr build`, the three suites it touches |
| 5 | PF-3, PF-8 | one `sr build` |
| 6 | PF-4 | one `sr build` |
| 7 | LG-3 \*, PF-5 | one `sr build` |
| 8 | PF-6, PF-9 \* | one `sr build` |
| 9 | PF-7, PF-10 \*, PF-11, PF-12 | one `sr build` |
| 10 | PF-13, PF-14, PF-15 | the owner's build, with the GPU for the `Large_` cases |
| 11 | LG-7, DOC-1 | the checkers, then one `sr build` |

## Open questions for the owner, on the plan itself

- Inline or waves.
- Does a task close on its own build, or does the owner run one build per wave? (The owner's rule of 2026-10-24 for layered execution was one build at the end of the programme.)
- The tasks that cannot finish before a SushiProfiler change (`PF-1` acceptance on G1, `PF-6` on G2, G3, G4, G6, `PF-12` on G5): hold them, or write them against a stub of the interface and finish them when the tag exists?
