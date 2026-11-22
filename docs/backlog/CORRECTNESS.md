# Correctness

## Remaining work

| ID | Pri | State | Title | Done when | Blocked by | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RW-7 | P2 | open | Detect a `Buffer` moved after it was recorded | the liveness guard reports a recorded `Buffer` that moved (finding H4 of the code and architecture review document in the version history) |  |  | 2026-10-19 |
| RW-8 | P2 | unmeasured | Run the topology code on a machine with several NUMA nodes and processor groups | NUMA placement, processor groups and the POSIX affinity branch have run on such a machine (QC-02, QC-29, QC-33) |  |  | 2026-10-19 |
| RW-67 | P2 | open | Make `StateStopsOnAWindowPastTheEnd` pass under parallel test jobs | the cause of KI-17 is found, the test passes ten runs of the medium label at four jobs, and it leaves `SR_QUARANTINED_TESTS` |  |  | 2026-10-26 |
| RW-75 | P2 | open | Make the five medium tests quarantined under AdaptiveCpp pass on its CPU backend | the five AdaptiveCpp-only names leave SR_QUARANTINED_TESTS and the Linux adaptivecpp job passes the medium label |  |  | 2026-10-29 |
| RW-83 | P2 | open | Make the no-line log tests fail when the capture is empty | AFloatingPoolLogsNoCoreMapLine and ARunNobodyCancelledLogsNoSkipLine fail when no record reaches the capture |  | `tests/engine/scheduler/test_task_scheduler.cpp` | 2026-11-06 |
| RW-84 | P2 | open | Free four tests from the machine's core and device count | LogsAWorkerWhoseCoreHasNoNumaNode, FlatTopologyKeepsAllVictims and EachDeviceIsProbedOnce run and pass on a one-device, any-core machine |  | `tests/engine/scheduler/test_task_scheduler.cpp` | 2026-11-06 |
| RW-85 | P2 | open | Size test_before_step_hooks right and build its descriptor test on Windows | test_before_step_hooks.cpp holds no Medium_ test without a device, and AFileDescriptorAboveIntIsRefusedWithItsValue compiles on Windows or is replaced |  | `tests/surface/api/detail/test_before_step_hooks.cpp` | 2026-11-06 |
| RW-155 | P1 | open | Plan a buffer another run holds against that run's end state | Two graphs on one buffer, planned and run from two threads at once, leave every interval with a valid space |  | `modules/engine/coherence/README.md` | 2026-11-16 |
| RW-160 | P1 | open | Find why a 4-byte Shared USM allocation is refused during a full test run | the cause is measured and fixed, and ten full sr test runs at 8 jobs pass the two death tests |  | `tests/surface/api/data/test_buffer.cpp` | 2026-11-17 |
| RW-163 | P1 | open | Make the submit-zone enclosure test hold under an 8-job medium run | the cause is measured and ten sr test --label medium --jobs 8 runs pass Medium_RuntimeSession.ResolvesEverySubmitThreadToAnEnclosingZone |  | `tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp` | 2026-11-17 |
| RW-172 | P2 | open | Bring the epoch slot-limit death test under the medium time limit | Medium_EpochReclamationDeathTest.AThreadPastTheSlotLimitStopsNamingTheLimit runs within 5 s alone in CI and leaves tests/budget_exemptions.toml |  | `tests/platform/memory/test_epoch_reclamation.cpp` | 2026-11-22 |
| RW-173 | P2 | open | Make the medium tests AdaptiveCpp failed in November pass on its CPU backend | the AdaptiveCpp-only names added to SR_QUARANTINED_TESTS on 2026-11-22 leave it and pass in the adaptivecpp CI job |  | `cmake/SushiRuntimeTests.cmake` | 2026-11-22 |
| RW-174 | P2 | open | Find why a worker-agent pull test missed its Hello on Windows | DisconnectEndsAnOpenInputPull leaves SR_QUARANTINED_TESTS and passes 200 repeats in the windows CI job |  | `tests/extension/distributed/test_worker_agent/test_worker_agent.cpp` | 2026-11-22 |

## Known issues

| ID | Origin | Symptom | Workaround | Fix | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- |
| KI-18 | own | AdaptiveCpp grants a USM request twice the device memory; the refusal test sees an address | the test is quarantined under AdaptiveCpp; other toolchains run it | RW-75 | `tests/platform/memory/test_usm_allocator.cpp` | 2026-10-29 |
| KI-19 | own | Under AdaptiveCpp a placement rule asking for uint64 max memory still finds a device | the test is quarantined under AdaptiveCpp; other toolchains run it | RW-75 | `tests/surface/api/runtime/test_placement_scope.cpp` | 2026-10-29 |
| KI-20 | own | Under AdaptiveCpp a submitting thread falls outside every profiler zone | the test is quarantined under AdaptiveCpp; other toolchains run it | RW-75 | `tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp` | 2026-10-29 |
| KI-21 | own | AdaptiveCpp reports shared USM as host USM, so a range-copy borrow test aborts | the test is quarantined under AdaptiveCpp; other toolchains run it | RW-75 | `tests/surface/api/interop/test_interop.cpp` | 2026-10-29 |
| KI-22 | own | AdaptiveCpp refuses an unimportable handle kind for its missing bindless extension | the test is quarantined under AdaptiveCpp; other toolchains run it | RW-75 | `tests/surface/memory_exchange/test_memory_exchange.cpp` | 2026-10-29 |
| KI-31 | toolchain | DPC++ command_submit drifts ~20 ms early; its device-host base is never refreshed | RW-134 pairs from start and end and the host's submit and reap | none, upstream getCurrentDeviceTime tests Diff before it is set |  | 2026-11-14 |
| KI-32 | toolchain | CUDA event times are float ms since start; kernel times step 15.6-125 us with uptime | Trace within the first 4 minutes of a process; steps are 15.6 us there | none, the DPC++ CUDA adapter getElapsedTime; patch the toolchain |  | 2026-11-14 |
| KI-37 | own | A plan made while another run holds its buffer can leave an interval with no valid space | Commit graphs that share a buffer from one thread | RW-155 | `modules/engine/coherence/README.md` | 2026-11-16 |
| KI-40 | own | The submit-zone enclosure test is quarantined: it failed once under an 8-job medium run | sr test --label quarantined runs it apart; it passed 200 repeats alone | RW-163 | `tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp` | 2026-11-17 |
| KI-41 | own | Two data-handle death tests are quarantined: a 4-byte Shared USM alloc was refused once | sr test --label quarantined runs them apart; they pass when run alone | RW-160 | `tests/surface/api/data/test_buffer.cpp` | 2026-11-17 |
| KI-45 | own | AdaptiveCpp reports a Shared USM allocation as host memory and foreign memory as USM | the test is quarantined under AdaptiveCpp; other toolchains run it | RW-173 | `tests/support/usm_source_contract.hpp` | 2026-11-22 |
| KI-46 | own | AdaptiveCpp refuses an import with no kind for its missing bindless extension | the test is quarantined under AdaptiveCpp; other toolchains run it | RW-173 | `tests/surface/memory_exchange/test_memory_exchange.cpp` | 2026-11-22 |
| KI-47 | own | Under AdaptiveCpp a distributed worker runs no offloaded task; the master runs them all | the test is quarantined under AdaptiveCpp; other toolchains run it | RW-173 | `tests/extension/distributed/test_distributed.cpp` | 2026-11-22 |
| KI-48 | own | Under AdaptiveCpp a split join waits on an unrelated held step on the part queue and hangs | the test is quarantined under AdaptiveCpp; other toolchains run it | RW-173 | `tests/engine/task/test_split_dispatch.cpp` | 2026-11-22 |
