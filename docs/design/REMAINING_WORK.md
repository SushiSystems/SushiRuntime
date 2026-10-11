# Remaining work

The single backlog of this repository. An item names where it sits and, when one exists, the
design document that holds its reasoning. The items closed before 2026-10-05 are kept with their
reasoning in `docs/archive/slop/TODO.md`.

## Correctness

- **Host reads of Shared memory during a run on Windows CUDA.** `Residency::Host` covers the
  runtime's own mid-run reads. A user's host read of a `Residency::Shared` buffer while a kernel
  runs on that GPU faults with 0xC0000006. Three candidate designs, choice open: a read that
  waits on the owning queue's pending work; a debug-build guard that reports the unsafe access
  with its call site; a documented rule that any value read mid-run lives in `Residency::Host`.
  Listed in [`KNOWN_ISSUES.md`](../reference/KNOWN_ISSUES.md).
- **NUMA placement is untested on a machine with several nodes.** The lookup reads the nodeset
  of the object since `2be82a7`; every development machine has one NUMA node.
- **A `Buffer` moved after `Graph::add` dangles silently.** A `State` may move since `c819f31`. The graph records the
  handle by address and the liveness token travels with the value, so the guard does not see the
  move (`modules/surface/api/include/SushiRuntime/api/data/buffer.hpp`, the `@warning` on the move constructor).
  Finding H4 of `docs/archive/slop/CODE_AND_ARCHITECTURE_REVIEW.md`: documented, not detected.

## Design phases that are open

- **Graph replay, P2.** Record, finalize and replay for plans without device-sized nodes, with
  fallback on failure. P0, P1 and P3 are built and their gain is not yet measured through
  `Graph::run` on each device. [`GRAPH_REPLAY.md`](GRAPH_REPLAY.md) §G.
- **Backbone, BB-1b.** Sync export from SYCL to Vulkan.
  [`ENGINE_BACKBONE_REFACTOR.md`](ENGINE_BACKBONE_REFACTOR.md) §5.
- **Backbone, BB-5 to BB-8.** The ship path (fat binary, `Runtime::try_create`, AOT and cache,
  redistributables), the rest of data-plane maturation, async depth and platform posture. None
  was re-checked against the tree on 2026-10-05; the first step is to read §9 to §12 of
  [`ENGINE_BACKBONE_REFACTOR.md`](ENGINE_BACKBONE_REFACTOR.md) against the code and correct its
  status line. BB-5d holds the open question of which licence the SYCL toolchain's
  redistributables ship under (`cmake/SushiRuntimeInstall.cmake`).
- **Physics substrate, R9.** A multi-colour or persistent-kernel dispatch node, so a
  graph-colouring consumer pays one scheduler round trip per kind and not one per colour.
  [`PHYSICS_SUBSTRATE_REQUIREMENTS.md`](PHYSICS_SUBSTRATE_REQUIREMENTS.md).
- **Simulation substrate, WP-5 and WP-7.** Peer-to-peer domain decomposition and the native CPU
  execution path. The other work packages need the same re-check as the backbone plan.
  [`SIMULATION_ENGINE_SUBSTRATE_PLAN.md`](SIMULATION_ENGINE_SUBSTRATE_PLAN.md) §7.

## Before the v1.0.0 freeze

- **Doxygen conformance sweep.** Every public function under a module's `include/` follows
  the rule in [`CONTRIBUTING.md`](../CONTRIBUTING.md) §4, in one pass that changes no behaviour.
  `python tools/documentation/check_source_comments.py include src tests examples cli tools --report`
  counted 1 291 findings on 2026-10-05: 771 comment runs, 295 blocks over the ceiling, 204 file
  headers, 16 separators, 5 history words. The estate's programme 5 owns this.
- **The version bump to 1.0.0 and its tag** are the last step of the freeze. The backbone plan
  holds that the tag waits for BB-3 and BB-4, which have landed, and for the BB-6 decisions.
- **C++ standard migration.** The project targets C++17. When the SYCL toolchain baseline allows
  it, concepts, `std::span`, `std::barrier`, `std::format` and `std::source_location` return at
  C++20, and `std::println` at C++23. The shims standing in today are
  `modules/foundation/core/include/SushiRuntime/core/bit.hpp` and `core/span.hpp`. `Core::Result<T, E>` is permanent by
  decision BB-4.5 and is not a migration target.

## Distributed layer

The v1 limits a user must know are in
[`DISTRIBUTED_GUIDE.md`](../guides/DISTRIBUTED_GUIDE.md) §12. The engineering gaps:

- **Worker scheduler bypass.** The worker runs every kernel on the queue of its device 0, one
  at a time, and keeps its buffer cache there. A device field in `WorkerAgentOptions` and
  dispatch through the worker's scheduler resolve it
  (`modules/extension/distributed/source/worker_agent.cpp`).
- **Frames after the handshake.** Master and worker authenticate each other once; frames are
  not encrypted and carry no signature. Encryption or a MAC per frame resolves it.
- **Flood protection.** Every open connection counts towards `max_peers`, registered or not. A
  separate cap on unregistered peers resolves it
  (`modules/extension/distributed/source/tcp_transport.cpp`).
- **A wrong secret after five `HELLO`.** `MasterLink` remembers four nonces, so the fifth
  `CHALLENGE` on one connection is ignored and only the master's handshake timeout ends it
  (`modules/extension/distributed/source/master_link.cpp`).
- **Unmeasured costs.** `PendingTasks::insert()` allocates per dispatch, and the release
  observer takes the buffer registry's mutex on every USM release. Both wait for the TCP
  benchmark figures.
- **Verification not yet run.** 100 repeats of the TCP fault suite and the TCP rows of the
  distributed benchmark. An ASan run of the layer was tried on 2026-10-10 and could not start:
  the intel/llvm bundle on the Windows machine ships no `clang_rt.asan` runtime, so `sr build
  --type asan` stops at configure. It needs a bundle with compiler-rt, or the Linux machine.
- **Endpoint configuration.** Cluster addresses come from flags and the environment; there is
  no nodes file.
- **Dynamic-graph offload.** The dynamic composer bypasses `TaskGraph::compile`, where
  `stamp_offloadable` runs, so an offloadable op recorded on a region runs its local fallback
  (`modules/engine/graph/source/task_graph.cpp`).

## API quality control

Findings of `docs/agent/2026_10_11_API_QUALITY_CONTROL/REPORT.md` that the repairs left open.

Waiting for a measurement, phase E:

- QC-19: the scheduler's atomics share a cache line with `inflight_`.
- QC-20: three default `sycl::event` objects and two copies per dispatch.
- QC-86: `is_graph_node()` and `get_execution_mode()` are read twice per node.
- QC-58: `prepare_run()` aggregates every region again at each run entry.
- QC-08: a commit renumbers every node; the incremental form changes the layout of `RegionState`.
- QC-117: two mutexes per `release()`, sixteen ledger cursors per cache line.
- QC-121: one allocation and two mutexes per control frame.

Reshapes of a public surface, each a breaking change with its own decision:

- QC-21, 22, 23: `IExecutionEngine` has 16 virtuals, `Node` seven responsibilities,
  `TaskScheduler` eight.
- QC-56: `DeviceIndex` and three more types live in `buffer.hpp`.
- QC-57, 67, 68: a change listener on `Recorder`, the argument order of `untracked_kernel`,
  `scratch()` returning a span.
- QC-59: the capture and `Coupled` forms have no `Dynamic` overload.
- QC-63: `dynamic_graph.hpp` includes `runtime.hpp`.
- QC-85: `Node::profiling` has no reader. `Core::Backend` has none either.
- QC-109: `EpochManager` is an inline singleton in a public header.
- QC-113, 114: `NUMAMapper::get_distance`, `map_distances` and the default body in
  `ITopologyProvider`.
- QC-122: `WorkerStats` carries an `int` id and an abbreviated name.
- QC-126: the interop handle kinds are a switch, with two kinds today.
- QC-93, 103, 116, 136: naming sweeps and `enqueue_host_task` in `detail`.

Open after a repair:

- QC-18: the reaping of an event whose query throws has no test, and a successor the failed
  entry released can be dispatched before the failure is recorded.
- QC-84: a `detach()` during a run leaves the stopped coordinator installed, because
  `scheduler/event_polling.hpp` holds it as a plain pointer.
- QC-12: a `cancel()` that races the end of its own run can reach the next run.
- QC-06: the full rebuild after a commit that threw midway has no test.
- QC-05: a graph that drops regions and never runs again keeps them; its two tests fail
  before the fix reliably only under ASan.
- QC-24: the logger's shutdown path has no automated test.
- QC-02, 29, 33: processor groups, the POSIX affinity branch and a machine with several NUMA
  nodes are untested.
- QC-28: `sushi_ptr<T> == sushi_ptr<U>` may be ambiguous under C++20.
- QC-40: whether a SYCL backend takes over an imported file descriptor is not known.
- QC-73, 74, 95, 115: a zero-size `parallel_for`, `NdRange` tiling, the logger join under
  `FreeLibrary`, and `DeviceQueueSet::wait_all()` stopping at the first queue that throws.
- QC-137: seven earlier deprecations have no changelog entry.
- `TcpWorkerOptions::attempt_timeout` and `write_idle_timeout` at zero were not traced.
- Wording left by the register pass: "before v0.4.0" in `log_level.hpp`,
  `resource_liveness.hpp`, `dispatch_width.hpp`, `event_timestamps.hpp` and
  `profiling_ledger.hpp`; "(F4)" and "(F5)" in `execution_engine.cpp`; the "used to"
  sentences in `modules/surface/api/tests/README.md`.

## Structural debt

- **`TaskScheduler` still holds two responsibilities beside the worker loop.** The injector
  queues (`pending_submissions`, `priority_submissions`, their mutex, condition variable and
  shadow counts) and the per-worker time buckets are members of the class, and
  `task_scheduler.cpp` holds 1 014 lines. Each is a brick of the kind task E0 took out
  (`docs/agent/2026_10_09_USABILITY_AUDIT/PLAN.md`, "Task E0").
- **Sentinel injection in `compile()`.** `TaskGraph::compile()` links leaf nodes to a sentinel.
  Moving the injection into `ExecutionEngine::execute()` would let `compile()` return a pure
  DAG. The current design is correct.
- **No LTO or IPO.** Nothing under `cmake/` sets `INTERPROCEDURAL_OPTIMIZATION`. Finding PERF-6
  of `docs/archive/slop/CODE_AND_ARCHITECTURE_REVIEW.md`; PERF-7 and PERF-8 of the same review
  were never verified.
- **Logic in the root `CMakeLists.txt`.** The version equality check, the package config
  generation and the CPack settings belong in function files under `cmake/`.

## Documentation

- **`Doxyfile` names three pages by their old paths.** `INPUT` lists `docs/INTRODUCTION.md`,
  `docs/INTEGRATION.md` and `docs/CLI_GUIDE.md`; they are now under `docs/getting_started/` and
  `docs/guides/`. Its comments cite `docs/ARCHITECTURE.md`. Until it is edited, `sr docs` builds
  the site without those pages.
- **`AGENTS.md` routes builds through `sr`**, which the owner's delegation rule forbids a
  subagent to run; the two need reconciling.
- **CMake comments cite moved documents.** `cmake/SushiRuntimeInstall.cmake` cites
  `docs/slop/ENGINE_BACKBONE_REFACTOR.md`; `cmake/SushiRuntimeConfig.cmake.in` and
  `tests/package/CMakeLists.txt` cite `docs/INTEGRATION.md`. `.github/ISSUE_TEMPLATE/bug_report.yml`
  names `SECURITY.md`, now `docs/reference/SECURITY.md`.
- **The root README repeats the manual.** Its "How it works", "Architecture" and "Project
  layout" sections restate `docs/getting_started/INTRODUCTION.md` and
  `docs/architecture/ARCHITECTURE.md`. The README keeps the pitch, the requirements and the
  build line; the rest moves to the page that already holds it.
- **No `cmake/README.md`.** The build functions and the tier table are described only in the
  comments of the files themselves.
- **Humanizer pass over the manual.** The estate audit measured 3.3 to 4.0 em dashes per 300
  words in the seven manual pages against a ceiling of about one.
- **Changelog entries that were never written.** The dispatch-width work of 2026-09-21
  (`feat(core)`, `feat(execution)`, `feat(graph)`) and the raise of the `sushicore` lower bound
  to 0.7.0 (`cli/pyproject.toml`) have no line in `docs/reference/CHANGELOG.md`.
- **Unverified manual content.** Whether the C++ samples compile against the current headers,
  and the per-flag descriptions in `docs/guides/CLI_GUIDE.md`, were not checked by the audit.
- **`check_layering.py` checks nothing here.** `K_TIER_ORDER` is empty. The repository is one
  module whose areas depend on each other in the order
  `docs/architecture/ARCHITECTURE.md` §2 draws; whether the checker should hold that order is
  undecided.

## Repository hygiene

- **Version sources disagree.** The library is 0.3.0 (`CMakeLists.txt`,
  `modules/foundation/core/include/SushiRuntime/core/version.hpp`); the CLI package is 1.0.0 (`cli/pyproject.toml`,
  `cli/sushiruntime/__init__.py`); `Doxyfile` says `v1 (unreleased)`. The owner decides whether
  the CLI shares the product version.
- **A release is due.** 49 commits have landed since v0.3.0, three of them marked breaking, and
  the Unreleased section holds 44 entries. Pre-1.0, a breaking change makes the next release
  v0.4.0.
- **CI.** `.github/workflows/ci.yml` runs on Linux only, calls `cmake` and `ctest` directly,
  runs none of the checkers, has no release workflow, and its `cli` job installs `sushicore`
  from a path SushiStack no longer carries.
- **The umbrella header's name.** `SushiRuntime/SushiRuntime.h` is the one header with a `.h`
  suffix and a capitalised name. A rename is a public interface change.
- **Layout steps left after the move.** The library exports every symbol, and no header is
  compiled alone. The order is in `docs/agent/2026_10_09_USABILITY_AUDIT/PLAN.md`.
- **No `.clang-format` or `.editorconfig`** in the root.
- **Ignore rules.** `docs/api-site/.gitignore` repeats two root rules; the root `.gitignore`
  still lists `dependencies/` and `docs/html`, and has no rule for `.superpowers/`,
  `.scratch_syntax/`, `.pytest_cache/` or `.venv/`.
- **Leftovers on disk and in git.** Four empty `CXX-DetectStdlib-*.h` probes in the root, written
  because the CLI configures with the source root as working directory
  (`cli/sushiruntime/services/project.py`); `venv/`, `build-asan/`, `.superpowers/`,
  `.scratch_syntax/`; the merged local branch `feature/sushicore_help_screen`; two stash entries
  from 2026-06-20 on a branch that no longer exists.
- **`check_source_comments.py` differs from SushiSkills in `_walk`.** The original calls
  `is_file()` on `venv/bin/python`, a link Windows cannot open, before it applies
  `K_SKIPPED_FOLDERS`, and stops with `WinError 1920`. This copy applies the skip first. Make the
  same swap in SushiSkills, then recopy the file with its `K_SKIPPED_FOLDERS` kept.
