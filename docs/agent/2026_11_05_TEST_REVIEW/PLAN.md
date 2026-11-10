# Test review: plan

**Status:** Open — 31 of 31 jobs done; the build and test run of 2026-11-10 (80 of 80 shards) predates the M5 and V1 change, so R4 reruns it

Applies the verdicts of `REPORT.md`, in hierarchy order: the shared helpers, adapters and seams the
rewrites need come first, then the defects the review found, then each module's tests. A module job
applies every record of its module: rewrite, resize, split, merge and delete as the record says, and
writes each add. Nothing in a module job changes production code except a seam a job above built.

## Jobs

| Job | Files | Waits on | Status |
| --- | --- | --- | --- |
| T1 | `tests/support/tolerance.hpp` copied from SushiSkills, and a `testing::Environment` fixture base that holds the one runtime and device of the program |  | done |
| T2 | In-memory adapters beside their ports: `TopologyProvider`, the offload ports, `TelemetryProvider`, `TaskMigrator`, `RebalanceStrategy`, `Task`, `AllocationOwner`, `RunObserver` (moving `RecordingObserver`), `USMReleaseObserver`, `FeedAttachment`, `CapabilityCache::Probe` |  | done |
| T3 | `InProcessTransport` beside the `Transport` port |  | done |
| T4 | `Performance_` and `Stress_` suites moved to the benchmark runner |  | done |
| T5 | The defects of this report, each with a test that fails first, and the pages corrected |  | done |
| T6 | The CLI tests on sushicore's in-memory runner and console | sushicore T1 | done |
| T7 | `tools/testing/check_test_additions.py`, `tools/tests/test_check_test_additions.py` deleted |  | done |
| M1 | the tests of `applications` (2 files): 2 rewrite, 1 split | T1, T2, T3, T4, T5, T6, T7 | done |
| M2 | the tests of `cli` (20 files): 60 rewrite, 20 resize, 1 split, 5 merge, 7 delete | T1, T2, T3, T4, T5, T6, T7 | done |
| M3 | the tests of `engine` (52 files): 106 rewrite, 17 resize, 25 split, 7 merge, 21 delete | T1, T2, T3, T4, T5, T6, T7 | done |
| M4 | the tests of `extension` (49 files): 142 rewrite, 66 resize, 43 split, 13 merge, 13 delete | T1, T2, T3, T4, T5, T6, T7 | done |
| M5 | the tests of `foundation` (54 files): 44 rewrite, 27 resize, 22 split, 5 merge, 28 delete | T1, T2, T3, T4, T5, T6, T7 | done |
| M6 | the tests of `platform` (27 files): 49 rewrite, 18 resize, 12 split, 18 merge, 11 delete | T1, T2, T3, T4, T5, T6, T7 | done |
| M7 | the tests of `surface/api` (64 files): 248 rewrite, 73 resize, 80 split, 15 merge, 26 delete | T1, T2, T3, T4, T5, T6, T7 | done |
| M8 | the tests of `surface/context` (3 files): 3 rewrite, 2 delete | T1, T2, T3, T4, T5, T6, T7 | done |
| M9 | the tests of `surface/context_access` (1 file): 5 rewrite | T1, T2, T3, T4, T5, T6, T7 | done |
| M10 | the tests of `surface/engine_handles` (2 files): 2 rewrite, 2 delete | T1, T2, T3, T4, T5, T6, T7 | done |
| M11 | the tests of `surface/interop` (4 files): 3 rewrite, 1 split, 5 delete | T1, T2, T3, T4, T5, T6, T7 | done |
| M12 | the tests of `surface/memory_exchange` (3 files): 5 rewrite, 1 resize, 2 delete | T1, T2, T3, T4, T5, T6, T7 | done |
| M13 | the tests of `surface/profiling` (23 files): 66 rewrite, 10 resize, 9 split, 11 merge, 13 delete | T1, T2, T3, T4, T5, T6, T7 | done |
| M14 | the tests of `tools` (5 files): 2 rewrite, 10 resize, 1 split, 18 merge, 5 delete | T1, T2, T3, T4, T5, T6, T7 | done |
| S1 | The clock, the executor, the host USM source and host device set, the host runtime and the threadless transport (decision 33) | M1 to M14 | done |
| S2 | Host forms for NdRange, raw command group, queue body, device-sized and late-count launches (decision 34) | S1 | done |
| V1 | Every LOGIC case of the medium audit moved onto its seam as `Small_`, one `Medium_` per boundary, folder by folder | S1 | done |
| V2 | One `sr build --distributed`, `sr test -l small` and `sr test -l medium --distributed`, fixed until green | V1 | done |
| R1 | The ten SOLID breaches of the review of 23bf5b2f..25c16ecc, as written (decision 37) | V2 | done |
| R2 | RW-127, RW-129, RW-126, the platform ports (queue factory, device enumerator, hwloc NUMA reader, USMShared source) and the audit's LOGIC cases moved onto them | R1 | done |
| R3 | Split test_recorder.cpp and test_dynamic_graph.cpp, `make_host_context()` in tests/support, merge the profiler pipeline headers, stale test includes, EpochManager off process-wide state | R1 | done |
| R4 | One build, small and medium run after R1 to R3, fixed until green; per-module counts | R2, R3 | done |
| R5 | Decision 54: the enumerator and placement on a plain `DeviceDescriptor`, the in-memory adapters device-free | R2 | done |
| R6 | RW-129 rest, the task_scheduler.cpp split, the audit's LOGIC cases left, the per-module recount | R5 | done |

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

## Not done as written

A job is `failed` while a record of its module, or a ruling it names, is not applied in the tree. Each item below gives the record, the
`REPORT.md` decision that covers it and the backlog entry that holds it. "(owner)" marks a shape the owner reserves.

- **R4.** Not started in this plan. The build, `sr test -l small` and `sr test -l medium` with per-module counts are the orchestrator's.

Backlog entries RW-83, RW-84, RW-85 and RW-86 are still open, but the tree holds their fixes (T5): the two no-line log tests assert the sink saw a line
first, the core-count and device-count tests are small, `test_before_step_hooks.cpp` holds two `Small_` tests, and decision 15 deleted
`test_test_budget_settings.py`. The orchestrator closes them.

## Resume

Worker 12, 2026-11-09, after decision 74; statuses audited against the tree on 2026-11-10. Closed in the plan: S2, R5, R2, R3 (the rows were stale; RW-126, RW-128,
RW-129 and RW-131 are closed and decisions 42, 47, 64, 66 and 74 settle what stays medium) and R6. Nothing built or run; the new and changed test files pass
`clang -fsyntax-only`. Not done:

1. R4: the build, `sr test -l small` and `sr test -l medium`, per-module counts. The orchestrator runs them.
2. RW-111 is ruled (decision 75): `run_for` reads the runtime context's clock, and a test advances a `ManualClock` to end a budget. Its
   new case is unverified until the build.
3. RW-117 is closed (decision 87): SushiLog v0.4.1 gave the logger an executor and a clock, and the `Fatal` delivery test runs small.
   RW-125 is ruled (decision 76): its done-when is decision 74's rule, and it is closed.
4. Unverified until the build: the six new `Small_DescriptorRefusal` and `Small_DeviceRefusal` rows, and the reduced
   `Medium_InteropImport` suite, which now holds one region row in place of the parameterised refusals.
