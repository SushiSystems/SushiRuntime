# Known issues

Defects that are recorded and not yet fixed. The work each one needs is in
[`REMAINING_WORK.md`](../design/REMAINING_WORK.md).

## Runtime

| Issue | Where |
| --- | --- |
| A host read of a `Residency::Shared` buffer while a kernel runs on the same GPU faults on Windows with CUDA (0xC0000006). Values read mid-run must live in `Residency::Host` | `modules/surface/api/include/SushiRuntime/api/data/buffer.hpp` |
| A `Buffer` moved after it was passed to `Graph::add` leaves the graph pointing at the old address, and the liveness guard does not report it | `modules/surface/api/include/SushiRuntime/api/data/buffer.hpp` |
| A `cancel()` that races the end of its own run can cancel the next run started on the same runtime | `modules/surface/api/include/SushiRuntime/api/graph/step_driver.hpp` |
| A `DynamicGraph` that drops regions and never runs again keeps them until it is destroyed | `modules/surface/api/include/SushiRuntime/api/graph/dynamic_graph.hpp` |
| When an event's status query throws, a successor the failed node released can run before the failure is recorded | `modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp` |
| On Linux with the current DPC++ a profiled in-order chain makes 8 allocations per tick on the calling thread and bare SYCL makes 5, so `Integration_SteadyStateAllocation.ProfiledReuseTicksDoNotAllocatePerTick` fails (512 to 2048 against 320 to 1280); first seen on CI on 2026-10-05, not reproduced on Windows | `tests/allocation/test_steady_state_allocation.cpp` |
| A distributed worker runs every kernel on the queue of its device 0, one at a time, and keeps its buffer cache on that device; its scheduler and its other devices take no part. A device field in `WorkerAgentOptions` and dispatch through the worker's scheduler resolve it | `modules/extension/distributed/source/worker_agent.cpp`, `modules/extension/distributed/source/dispatch_executor.cpp` |
| A worker with a wrong secret that has sent five `HELLO` on one connection is not rejected on that connection: `MasterLink` remembers four nonces and ignores a `CHALLENGE` it can no longer check. The master's handshake timeout closes the connection and the next one rejects the worker. A link that remembers every nonce of its connection resolves it | `modules/extension/distributed/source/master_link.cpp` |
| `Cluster::detach()` during a run stops the master and runs the pending tasks locally, but the stopped coordinator stays installed until the next `detach()`, `attach()` or the end of the runtime | `modules/extension/distributed/source/cluster.cpp` |
| The distributed master has no flood protection: every open connection counts towards `max_peers`, registered or not, so a host that reaches the port can hold every slot. A separate cap on unregistered peers resolves it; until then the port belongs behind a firewall | `modules/extension/distributed/source/tcp_transport.cpp`, [`DISTRIBUTED_GUIDE.md`](../guides/DISTRIBUTED_GUIDE.md) §12 |
| `PendingTasks::insert()` allocates a map node and a copy of the output handles for every dispatch. Not measured; a pooled entry resolves it if the benchmark shows it | `modules/extension/distributed/source/pending_tasks.cpp` |
| With a cluster attached, every USM release calls the master's release observer, which takes the buffer registry's mutex for one lookup, also for memory that was never shared. Not measured; a lock-free test for a shared address resolves it | `modules/extension/distributed/source/distributed_coordinator.cpp`, `modules/extension/distributed/source/buffer_registry.cpp` |
| An offloadable op recorded on a `DynamicGraph` region runs its local fallback and is never shipped to a worker | `modules/engine/graph/source/task_graph.cpp` |
| The distributed layer authenticates master and worker at the handshake and protects nothing after it: frames are not encrypted and carry no signature, so it is for a trusted LAN. Encryption, or a MAC per frame, resolves it | [`DISTRIBUTED_GUIDE.md`](../guides/DISTRIBUTED_GUIDE.md) §2 |

## Build, CLI and CI

| Issue | Where |
| --- | --- |
| `sr docs` builds the API site without the introduction, the integration guide and the CLI guide, because `INPUT` names them by their paths before 2026-10-05 | `Doxyfile` |
| The CI `cli` job installs `sushicore` from `./.sushistack/sushicore`, a folder SushiStack no longer carries | `.github/workflows/ci.yml` |
| The CI lint step ends in `\|\| true` and cannot fail | `.github/workflows/ci.yml` |
| The CLI package declares version 1.0.0 and `Doxyfile` says `v1 (unreleased)`, while the library is 0.3.0 | `cli/pyproject.toml`, `cli/sushiruntime/__init__.py`, `Doxyfile` |
| Configuring leaves empty `CXX-DetectStdlib-*.h` files in the repository root, because the CLI runs the configure step with the source root as its working directory. `check_source_comments.py . --rule rule_license_block` reports the four on disk today | `cli/sushiruntime/services/project.py` |
| `_walk` in this copy of the source comment checker tests `K_SKIPPED_FOLDERS` before `is_file()`; the SushiSkills original still tests them the other way round and stops with `WinError 1920` on `venv/bin/python` | `tools/documentation/check_source_comments.py` |

## Documentation

| Issue | Where |
| --- | --- |
| `check_docs_layout.py` reports five links in the estate audit report that quote a link from the old README | `docs/agent/2026_10_05_ESTATE_AUDIT/REPORT.md` |
| Three CMake comments and one issue template cite documents by their paths before 2026-10-05 | `cmake/SushiRuntimeInstall.cmake`, `cmake/SushiRuntimeConfig.cmake.in`, `tests/package/CMakeLists.txt`, `.github/ISSUE_TEMPLATE/bug_report.yml` |
| The changelog has no entry for the dispatch-width fields added on 2026-09-21 or for the `sushicore` lower bound of 0.7.0 | `docs/reference/CHANGELOG.md` |
| Documents frozen in the archive cite each other and the manual by the paths they had before they moved | `docs/archive/slop/`, `docs/archive/superpowers/specs/`, `docs/archive/agent/reports/` |
