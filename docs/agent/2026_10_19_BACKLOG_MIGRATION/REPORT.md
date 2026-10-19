# Backlog migration: inventory

**Status:** Shipped — 2026-10-19. The owner approved the inventory as proposed.

The migration of this repository to the backlog format of SushiSkills'
`skills/documentation/BACKLOG_FORMAT.md`, step 2 of section 8 of SushiSkills'
`docs/agent/2026_10_19_BACKLOG_FORMAT/SPEC.md`. Every entry was read against the code and
`git log` on 2026-10-19. The backlog was last written at `84300484` (2026-10-15) and 57 commits
followed; the known issues at `cf3670db` (2026-10-17) and two commits followed, neither touching
code. No commit since either names a QC, BB, WP or R finding, so an entry marked true without a
measurement stands as written.

## Shared tools

`copy_shared_tools.py` added `check_backlog.py`, `design_status.py` and their tests; every other
shared file already matched SushiSkills. `tools/README.md` took the shared rows it lacked
(`check_docs_truth.py`, `check_logging.py`, `check_probes.py`); the probe row cites
`applications/probes/` and `applications/benchmarks/`, which this tree does not have, so
`docs/.docs_truth_allow` names them.

## Backlog

| Entry today | Verdict | Proposal |
| --- | --- | --- |
| Host reads of Shared memory on Windows CUDA, three designs | True | `RW-4`, decision |
| NUMA placement untested on several nodes; QC-02, 29, 33 | True | `RW-8`, unmeasured, one entry |
| A `Buffer` moved after `Graph::add` dangles | True | `RW-7` |
| Graph replay P2 | True | `RW-9`, Source `GRAPH_REPLAY.md` |
| Graph replay P0, P1, P3 gain not measured | True | `RW-10`, unmeasured |
| Backbone BB-1b | True | `RW-11`, Source `ENGINE_BACKBONE_REFACTOR.md` |
| Backbone BB-5 to BB-8, re-check the plan | True | `RW-12` |
| Physics substrate R9 | True | `RW-13`, Source `PHYSICS_SUBSTRATE_REQUIREMENTS.md` |
| Simulation substrate WP-5, WP-7, re-check | True | `RW-14`, Source `SIMULATION_ENGINE_SUBSTRATE_PLAN.md` |
| Doxygen conformance sweep, 1 291 findings | Closed: `check_source_comments.py` reports 0 | Delete |
| Version bump to 1.0.0 and its tag | True; waits for the BB-6 decisions | `RW-15`, blocked by `RW-12` |
| C++ standard migration | True | `RW-16`, blocked, `external: the SYCL toolchain baseline` |
| Worker scheduler bypass | True | `RW-17` |
| Frames after the handshake | True | `RW-18` |
| Flood protection | True | `RW-19` |
| A wrong secret after five `HELLO` | True | `RW-20` |
| Unmeasured costs | True | `RW-21`, unmeasured |
| Verification not yet run (TCP repeats, benchmark rows, ASan) | True | `RW-22`, unmeasured |
| Endpoint configuration, no nodes file | True | `RW-23` |
| Dynamic-graph offload | True | `RW-24` |
| QC phase E measurements (19, 20, 86, 58, 08, 117, 121) | True | `RW-26`, unmeasured, one entry |
| Reshapes of a public surface (QC-21 to 136) | True | `RW-6`, decision, one entry |
| QC-18 | True | `RW-27` |
| QC-84 | True | `RW-25` |
| QC-12 | True | `RW-28` |
| QC-06 | True | `RW-29` |
| QC-05 | True | `RW-30` |
| QC-24, the logger's shutdown path has no test | Closed: the logger moved to SushiLog, whose `test_exit.cpp` covers it | Delete |
| QC-28 | True | `RW-31` |
| QC-40 | True | `RW-32`, unmeasured |
| QC-73, 74, 95, 115 | Stale: QC-95, the logger join, moved to SushiLog | `RW-33`, QC-73, 74 and 115 |
| QC-137 and the changelog entries never written | True; listed twice | `RW-34`, one entry |
| `attempt_timeout` and `write_idle_timeout` at zero | True | `RW-35` |
| Wording left by the register pass | Stale: only "(F4)" and "(F5)" in `scheduler_execution_engine.cpp` remain | `RW-36` |
| `NodeDescription` is a tagged struct | True | `RW-37` |
| A recorder about to be cleared copies its work | True | `RW-38` |
| `DynamicGraph` walks every region three times | True | `RW-39` |
| Catch ladders outside the engine and the surface | Stale: `logger.cpp` left; 52 `catch` in the distributed sources | `RW-40` |
| `NodeOptions` has no named setters | True | `RW-41` |
| `version.hpp` says 0.3.0 until the release | True; part of the release | Merged into `RW-3` |
| Seven findings of `check_source_comments.py` | Closed: 0 findings | Delete |
| The adapter of a consumer is quoted from one example | Not work: `check_docs_truth.py` holds each guide to its source | Delete |
| The allocation tests are blind on Windows | True | `RW-43` |
| A plan on a shared buffer under a cluster leaves the chain layer | True | `RW-44` |
| The dispatch decoder accepts 255 bytes | Stale: `K_MAX_DISPATCH_NAME_BYTES` is gone; the bounds were not traced | `RW-45` |
| In the consumers (SushiAI, SushiEngine) | Foreign | Delete; each moves to its repository when it migrates |
| `TaskScheduler` holds two more responsibilities | True: 1 022 lines | `RW-46` |
| Sentinel injection in `compile()` | True | `RW-47` |
| No LTO or IPO; PERF-7, PERF-8 unverified | True | `RW-48` |
| Logic in the root `CMakeLists.txt` | Closed: the version check is in `cmake/SushiRuntimeVersionCheck.cmake`, and no package or CPack logic is left in the root | Delete |
| New tests not seen failing | Stale: `test_log_sink.cpp` left with the logger | `RW-49`, the 36 headers |
| `Doxyfile` names three pages by old paths | Stale: `INPUT` is correct; two comments still cite `docs/ARCHITECTURE.md` | `RW-50` |
| `AGENTS.md` routes builds through `sr` | True | `RW-1`, decision |
| CMake comments cite moved documents; the issue template names `SECURITY.md` | Stale: the template is correct; three CMake comments still cite old paths | `RW-51` |
| The root README repeats the manual | Closed: cut to six sections in `2680b51d` | Delete |
| No `cmake/README.md` | True | `RW-52` |
| Humanizer pass over the manual | Closed: `check_docs_truth.py` passes its em dash rule since `dcc4362c` | Delete |
| Unverified manual content | True | `RW-53`, unmeasured |
| Version sources disagree | True | `RW-2`, decision |
| A release is due | True: 0.3.0 is the last tag | `RW-3`, decision |
| CI: Linux only, `cmake` and `ctest` direct, two checkers missing | Closed: a Windows job, no direct call, `check_all.py` runs every checker | Delete |
| Layout steps left after the move | True | `RW-58`, Source `docs/agent/2026_10_09_USABILITY_AUDIT/PLAN.md` |
| No `.clang-format` or `.editorconfig` | True | `RW-54` |
| Ignore rules | Stale: `docs/api-site/.gitignore` is gone; the root rules are still out of date | `RW-55` |
| Leftovers on disk and in git | Stale: the probes, `build-asan/`, `.scratch_syntax/` and the merged branch are gone; two stashes of 2026-06-20 and `.superpowers/` remain | `RW-57`, decision |
| No way to run `sr` under WSL | True | `RW-5`, decision |
| `check_source_comments.py` differs in `_walk` | Closed: SushiSkills applies the skip first | Delete |
| (new) The CLI configures from the source root | True: `project.py` lines 453 and 461 | `RW-56` |

## Known issues

| Issue today | Verdict | Proposal |
| --- | --- | --- |
| Host read of a Shared buffer faults | True | `KI-1`, `Fix: RW-4` |
| A moved `Buffer` dangles | True | `KI-2`, `Fix: RW-7` |
| `cancel()` racing its run's end | True | `KI-3`, `Fix: RW-28` |
| A `DynamicGraph` keeps dropped regions | True | `KI-4`, `Fix: RW-30` |
| A throwing status query releases a successor early | True | `KI-5`, `Fix: RW-27` |
| A worker runs on device 0 only | True | `KI-6`, `Fix: RW-17` |
| A wrong secret after five `HELLO` | True | `KI-7`, `Fix: RW-20` |
| `detach()` during a run keeps the coordinator | True | `KI-8`, `Fix: RW-25` |
| No flood protection | True | `KI-9`, `Fix: RW-19` |
| `PendingTasks::insert()` allocates | Not a defect: unmeasured | Delete; it is `RW-21` |
| The release observer takes the registry mutex | Not a defect: unmeasured | Delete; it is `RW-21` |
| A dynamic-graph op is never offered to a worker | True | `KI-10`, `Fix: RW-24` |
| Frames are not protected after the handshake | True | `KI-11`, `Fix: RW-18` |
| CLI 1.0.0, `Doxyfile` v1, library 0.3.0 | True | `KI-12`, `Fix: RW-2` |
| `CXX-DetectStdlib-*.h` in the root | True: the files were cleaned, the cause stands | `KI-13`, `Fix: RW-56` |
| `_walk` in this copy differs | Closed | Delete |
| Two CMake comments cite the integration guide's old path | True, and a third cites `docs/slop/` | `KI-14`, `Fix: RW-51` |
| The changelog lacks the dispatch-width and `sushicore` lines | True | `KI-15`, `Fix: RW-34` |
| The archive cites old paths | True | `KI-16`, `Fix: none, the archive is frozen` |

## Status lines

| File | Today | Proposal |
| --- | --- | --- |
| `docs/agent/2026_10_05_ESTATE_AUDIT/REPORT.md` | "Complete. ..." | `Shipped — 2026-10-05. Read-only audit.` |
| `docs/agent/2026_10_09_USABILITY_AUDIT/SPEC.md` | "Open. Step B0 done; ..." | `Open — the layout steps of part 2, RW-58` |
| `docs/agent/2026_10_11_API_QUALITY_CONTROL/SPEC.md` | "Research closed on 2026-10-11 ..." | `Shipped — 2026-10-12. The findings left open are in the backlog.` |
| `docs/agent/2026_10_14_TEST_COVERAGE/PLAN.md` | "Phases 1 to 3 ... the break check is open" | `Open — the break check of RW-49` |
| `docs/agent/2026_10_15_TEST_SIZES_AND_CI_STAGES/PLAN.md` | "planned on 2026-10-15, run inline." | `Shipped — 2026-10-16. See REPORT.md.` |
| `docs/agent/2026_10_16_LOGGING_STANDARD/PLAN.md` | "planned on 2026-10-16; in progress, inline." | `Shipped — 2026-10-17. The logger moved to SushiLog.` |

The four open design documents keep their status lines and are cited by `RW-9` to `RW-14`.

## Other files

| File | Finding | Proposal |
| --- | --- | --- |
| Roadmap, journal, handover, milestone or priority files | None | None |
| `README.md` | Has `## Status` | None |
| Branch `pre-clean-main`, 2026-10-18, not merged | Not in any entry; a backup of the owner's | Named in `RW-57` |

## Outcome

The owner approved the inventory on 2026-10-19. The backlog holds `RW-1` to `RW-58`, with
`RW-42` unused (its entry merged into `RW-3`); the known issues hold `KI-1` to `KI-16`. Six
status lines took the four forms.

```
$ python tools/check_all.py .
clean: 11 checkers, tests
```
