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

## Known issues

| ID | Origin | Symptom | Workaround | Fix | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- |
| KI-18 | own | AdaptiveCpp grants a USM request twice the device memory; the refusal test sees an address | the test is quarantined under AdaptiveCpp; other toolchains run it | RW-75 | `tests/platform/memory/test_usm_allocator.cpp` | 2026-10-29 |
| KI-19 | own | Under AdaptiveCpp a placement rule asking for uint64 max memory still finds a device | the test is quarantined under AdaptiveCpp; other toolchains run it | RW-75 | `tests/surface/api/runtime/test_placement_scope.cpp` | 2026-10-29 |
| KI-20 | own | Under AdaptiveCpp a submitting thread falls outside every profiler zone | the test is quarantined under AdaptiveCpp; other toolchains run it | RW-75 | `tests/surface/profiling/test_register_profiler_parts/test_runtime_session.cpp` | 2026-10-29 |
| KI-21 | own | AdaptiveCpp reports shared USM as host USM, so a range-copy borrow test aborts | the test is quarantined under AdaptiveCpp; other toolchains run it | RW-75 | `tests/surface/api/interop/test_interop.cpp` | 2026-10-29 |
| KI-22 | own | AdaptiveCpp refuses an unimportable handle kind for its missing bindless extension | the test is quarantined under AdaptiveCpp; other toolchains run it | RW-75 | `tests/surface/memory_exchange/test_memory_exchange.cpp` | 2026-10-29 |
| KI-24 | own | placement_of accepts a same-context device mismatch the dispatch span check refuses | none | none, the anchor logic is to be unified in one place | `modules/engine/scheduler/source/dispatch_placement.cpp` | 2026-11-01 |
