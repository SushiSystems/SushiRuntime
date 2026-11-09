# Test review: plan

**Status:** Open — paused on 2026-11-09 after decision 73; see Resume

Applies the verdicts of `REPORT.md`, in hierarchy order: the shared helpers, adapters and seams the
rewrites need come first, then the defects the review found, then each module's tests. A module job
applies every record of its module: rewrite, resize, split, merge and delete as the record says, and
writes each add. Nothing in a module job changes production code except a seam a job above built.

## Jobs

| Job | Files | Waits on | Status |
| --- | --- | --- | --- |
| T1 | `tests/support/tolerance.hpp` copied from SushiSkills, and a `testing::Environment` fixture base that holds the one runtime and device of the program |  | done: helpers in place; test bodies move onto them in the M jobs |
| T2 | In-memory adapters beside their ports: `TopologyProvider`, the offload ports, `TelemetryProvider`, `TaskMigrator`, `RebalanceStrategy`, `Task`, `AllocationOwner`, `RunObserver` (moving `RecordingObserver`), `USMReleaseObserver`, `FeedAttachment`, `CapabilityCache::Probe` |  | done |
| T3 | `InProcessTransport` beside the `Transport` port |  | done: own module `in_process_transport` (decision 7); one typed `TransportContract` runs small on `InProcessTransport` and medium on `TCPTransport` (RW-87, c40854ec) |
| T4 | `Performance_` and `Stress_` suites moved to the benchmark runner |  | done |
| T5 | The defects of this report, each with a test that fails first, and the pages corrected |  | done: pages, timeouts, `K_ALL_KEYS`, stray comment fixed; RW-83 to RW-86 hold the test defects |
| T6 | The CLI tests on sushicore's in-memory runner and console | sushicore T1 | done: no CLI test patches an owned function; the patches left replace the standard library or set a constant |
| T7 | `tools/testing/check_test_additions.py`, `tools/tests/test_check_test_additions.py` deleted |  | done |
| M1 | the tests of `applications` (2 files): 2 rewrite, 1 split | T1, T2, T3, T4, T5, T6, T7 | done, not built: RW-92 applied (decision 12); the distributed test builds only with RW-93 |
| M2 | the tests of `cli` (20 files): 60 rewrite, 20 resize, 1 split, 5 merge, 7 delete | T1, T2, T3, T4, T5, T6, T7 | done: the source-text rule of the install hint is left to `check_module_install.py` (decision 18, d6e107b0) |
| M3 | the tests of `engine` (52 files): 106 rewrite, 17 resize, 25 split, 7 merge, 21 delete | T1, T2, T3, T4, T5, T6, T7 | done, not built: RW-100 and the placement and observer seams by fb4a126c; the rest of RW-102 ruled medium (decision 27) |
| M4 | the tests of `extension` (49 files): 142 rewrite, 66 resize, 43 split, 13 merge, 13 delete | T1, T2, T3, T4, T5, T6, T7 | done, not built: RW-104 (97da9412), RW-105 (6159bf89) and RW-106 (2235acd0) applied; the small contract run waits on RW-87 |
| M5 | the tests of `foundation` (54 files): 44 rewrite, 27 resize, 22 split, 5 merge, 28 delete | T1, T2, T3, T4, T5, T6, T7 | done, not built: RW-101 by ed71fe83, the rest in RW-115, RW-117 and RW-118; the default device-filter promise is tested in M7 by 8ef71d48 |
| M6 | the tests of `platform` (27 files): 49 rewrite, 18 resize, 12 split, 18 merge, 11 delete | T1, T2, T3, T4, T5, T6, T7 | done, not built: the NUMA routes and the device filter run small, the rest ruled medium (RW-103, decision 28) |
| M7 | the tests of `surface/api` (64 files): 248 rewrite, 73 resize, 80 split, 15 merge, 26 delete | T1, T2, T3, T4, T5, T6, T7 | done, not built: seams for the resizes to RW-108 and RW-110; dropped promises to RW-109, RW-111 to RW-114; three plan-cache promises restored by c40ece5c, the offloaded count left in RW-107 |
| M8 | the tests of `surface/context` (3 files): 3 rewrite, 2 delete | T1, T2, T3, T4, T5, T6, T7 | done: `HandleOfReturnsTheOwnersHandle` is tested through `Runtime` (decision 8) |
| M9 | the tests of `surface/context_access` (1 file): 5 rewrite | T1, T2, T3, T4, T5, T6, T7 | done: two promises held through `Runtime`, three ruled untested (RW-96, decision 17, 0e6a6cfa); a compile check holds the header |
| M10 | the tests of `surface/engine_handles` (2 files): 2 rewrite, 2 delete | T1, T2, T3, T4, T5, T6, T7 | done: the device-free bridge tests run small (decision 9) |
| M11 | the tests of `surface/interop` (4 files): 3 rewrite, 1 split, 5 delete | T1, T2, T3, T4, T5, T6, T7 | done: the default-empty promise lost its test, RW-91 |
| M12 | the tests of `surface/memory_exchange` (3 files): 5 rewrite, 1 resize, 2 delete | T1, T2, T3, T4, T5, T6, T7 | done, not built: RW-89 applied (decision 10); the GPU skip is RW-90 |
| M13 | the tests of `surface/profiling` (23 files): 66 rewrite, 10 resize, 9 split, 11 merge, 13 delete | T1, T2, T3, T4, T5, T6, T7 | done, not built: RW-98 and five RW-99 resizes by 8db1ef11; the three graph-identity records ruled medium (decision 26) |
| M14 | the tests of `tools` (5 files): 2 rewrite, 10 resize, 1 split, 18 merge, 5 delete | T1, T2, T3, T4, T5, T6, T7 | done, not built: `test_test_budget_settings.py` deleted (decision 15); the three resizes applied by 0e276549 (RW-95, decision 16) |
| S1 | The clock, the executor, the host USM source and host device set, the host runtime and the threadless transport (decision 33) | M1 to M14 | done: ce70e55b, da4525a5, 42195e98, 565948f1, 958c0976, c156df96, fixed by e6ec9548 |
| S2 | Host forms for NdRange, raw command group, queue body, device-sized and late-count launches (decision 34) | S1 | done: dynamic, device-sized and late-count get host forms (db46c0e6, 7d4c949d); an NdRange, a raw command group and a queue body get none by decisions 42 and 47 (their arithmetic is host functions of `tests/support/kernel_math.hpp`, the launch tests stay the medium boundary); RW-126 is closed |
| V1 | Every LOGIC case of the medium audit moved onto its seam as `Small_`, one `Medium_` per boundary, folder by folder | S1 | partly done: every folder but `extension/distributed` moved; the cases left medium wait on RW-126 to RW-129 |
| V2 | One `sr build --distributed`, `sr test -l small` and `sr test -l medium --distributed`, fixed until green | V1 | done: small 1310 of 1310, medium 432 of 432 at e6ec9548 |
| R1 | The ten SOLID breaches of the review of 23bf5b2f..25c16ecc, as written (decision 37) | V2 | done: 37246149; the `sycl_usm_source.hpp` SYCL include stays, decision 37 says why |
| R2 | RW-127, RW-129, RW-126, the platform ports (queue factory, device enumerator, hwloc NUMA reader, USMShared source) and the audit's LOGIC cases moved onto them | R1 | done: RW-126, RW-127, RW-129 and RW-130 closed; the four platform ports, the `Clock` port, the scheduler rounds, the TaskScheduler split (decision 68) and the recount (decision 73) are in. The cases left medium are boundaries by decisions 62, 64, 66 and 74: `ChainRun` and the task bodies keep `sycl::queue&`, the worker-side TCP fixture, the CPU-only master and the destroy-order case need their own runtime |
| R3 | Split test_recorder.cpp and test_dynamic_graph.cpp, `make_host_context()` in tests/support, merge the profiler pipeline headers, stale test includes, EpochManager off process-wide state | R1 | partly done: the splits and the merge by 26816063, `make_host_context()` by 37246149; EpochManager is off process-wide state (decision 39); the stale includes outside the split files are dropped (30 files, each still passes `clang -fsyntax-only`); the split files keep theirs |
| R4 | One build, small and medium run after R1 to R3, fixed until green; per-module counts | R2, R3 | todo: build and small 1324 of 1324 green at 37246149; medium 432 of 436, four reconnect timeouts in MasterLink and DispatchExecutor tests, fixed unverified (the ThreadDrive got no timed wake after a loss); medium rerun needed |
| R5 | Decision 54: the enumerator and placement on a plain `DeviceDescriptor`, the in-memory adapters device-free | R2 | done: topology on `DeviceDescriptor`, placement and the device set build no queue at construction (RW-131, decision 57); `InMemoryQueueFactory::make()` and `InMemoryEventReadiness` keep `sycl::queue` and `sycl::event` in their port signatures, ruled as the runtime's execution currency (decisions 64, 66), so the factory's contract rows and `RecordsEveryBuildInOrder` are `Medium_` (decision 69) |
| R6 | RW-129 rest, the task_scheduler.cpp split, the audit's LOGIC cases left, the per-module recount | R5 | partly done: decisions 55 to 60; `test_runtime_context.cpp` raw bodies done (750c8a31), `test_distributed.cpp` master runtimes shared (36d0ae01), coordinator drive modes split (fb71cfd3); left: the worker and second runtimes of `test_distributed.cpp` (decision 59), the medium audit's remaining LOGIC cases, the recount; RW-128 closed by the runtime holding its collector (decision 67, 224b5203) and reopened as RW-132 (decision 70), the rounds and the two shells are classes of their own (decision 68), `task_scheduler.hpp` slimmed (decision 72), the recount done (decision 73) |

## Acceptance

- **T1.** No test body builds a runtime or device, and no comparison spells a tolerance literal. Build: the repository's test command over the touched tests.
- **T2.** Each keeps its port's promise with no device or thread. Build: the repository's test command over each module.
- **T3.** The transport tests use it from the port's module. Build: the repository's test command over transport and distributed.
- **T4.** No benchmark sits in the test tree. Build: the repository's test command and its benchmark command.
- **T5.** Every defect above is fixed or has a backlog entry; each README says what the code does. Build: the repository's test command and `python tools/check_all.py .`
- **T6.** No CLI test patches the repository's own functions. Build: the CLI tests.
- **T7.** Neither file exists and the checkers pass. Build: `python tools/check_all.py .`
- **M jobs.** Every record of the module is applied: no test the report deletes remains, every kept and
  new test passes at the size its label states, and the module's tests pass through the project's own
  test command. Build: that command over the module.

The owner chooses inline or parallel mode. SushiBLAS and SushiAI build on this repository's fixture base
and adapters; their plans wait on T1 and T2.

## Resume

Worker 11, 2026-11-09, after decision 73. Done: `RecordsEveryBuildInOrder` is medium (decision 69, 00daae25);
`SR_INSTANT` and `SR_LOCK_WAIT` take the collector, `CollectorLock` and `PendingWork` hold it, RW-128 is reopened as
RW-132 (decision 70, 64f3a915); `HostRuntimeConfig::profile_collector`, `HostSession`, and 13 plan, run-zone and
counter cases small (decision 71, 5f686063); `task_scheduler.hpp` includes only what it names (decision 72, c9221184);
the recount (decision 73, d53718e8). Nothing built or run: the orchestrator runs one build, small and medium. Not done:

1. RW-132 stays open: 28 of the 41 profiling cases (`DeviceTimingWindow`, `RuntimeSession*`, the transfer bytes, the USM
   allocator zones, worker names, the chain path, `dispatch.submit`) need a device, a thread or a path the host runtime
   does not take. Closing it waits on a host path for them or an owner ruling that leaves them medium.
2. Job 4: the other remaining LOGIC cases of the medium audit (the audit table was not in this worker's reach); the
   worker and second runtimes of `test_distributed.cpp` (decision 59); `ChainRun` and the task bodies (decision 64).
3. Unverified until the build: the 13 converted profiling cases assume the host runtime writes `run.submit`,
   `run.wait`, `plan.*`, `wait.queues` and the worker values as the device runtime does.
