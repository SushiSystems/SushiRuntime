# Test review: plan

**Status:** Open — not started; the owner chooses inline or parallel

Applies the verdicts of `REPORT.md`, in hierarchy order: the shared helpers, adapters and seams the
rewrites need come first, then the defects the review found, then each module's tests. A module job
applies every record of its module: rewrite, resize, split, merge and delete as the record says, and
writes each add. Nothing in a module job changes production code except a seam a job above built.

## Jobs

| Job | Files | Waits on | Status |
| --- | --- | --- | --- |
| T1 | `tests/support/tolerance.hpp` copied from SushiSkills, and a `testing::Environment` fixture base that holds the one runtime and device of the program |  | done: helpers in place; test bodies move onto them in the M jobs |
| T2 | In-memory adapters beside their ports: `TopologyProvider`, the offload ports, `TelemetryProvider`, `TaskMigrator`, `RebalanceStrategy`, `Task`, `AllocationOwner`, `RunObserver` (moving `RecordingObserver`), `USMReleaseObserver`, `FeedAttachment`, `CapabilityCache::Probe` |  | done |
| T3 | `InProcessTransport` beside the `Transport` port |  | done: own module `in_process_transport` (decision 7); the small `TransportContract` run is RW-87 |
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
| S1 | The clock, the executor, the host USM source and host device set, the host runtime and the threadless transport (decision 33) | M1 to M14 | done, not built: ce70e55b, da4525a5, 42195e98, 565948f1, 958c0976, c156df96 |
| S2 | Host forms for NdRange, raw command group, queue body, device-sized and late-count launches (decision 34) | S1 | todo: RW-126 |
| V1 | Every LOGIC case of the medium audit moved onto its seam as `Small_`, one `Medium_` per boundary, folder by folder | S1 | running |
| V2 | One `sr build --distributed`, `sr test -l small` and `sr test -l medium --distributed`, fixed until green | V1 | todo |

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
