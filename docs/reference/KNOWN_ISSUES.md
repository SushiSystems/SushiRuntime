# Known issues

Defects that are recorded and not yet fixed. The work each one needs is in
[`REMAINING_WORK.md`](../design/REMAINING_WORK.md).

## Runtime

| Issue | Where |
| --- | --- |
| A host read of a `Residency::Shared` buffer while a kernel runs on the same GPU faults on Windows with CUDA (0xC0000006). Values read mid-run must live in `Residency::Host` | `include/SushiRuntime/api/data/buffer.hpp` |
| A `Buffer` or `State` moved after it was passed to `Graph::add` leaves the graph pointing at the old address, and the liveness guard does not report it | `include/SushiRuntime/api/data/buffer.hpp` |
| A distributed worker that drops mid-run cannot rejoin | `src/distributed/` |
| A distributed worker does not receive task N+1 while task N runs | `src/distributed/worker_agent.cpp` |
| The worker allocates, frees and inspects devices through `ctx.get_queue(0)`, bypassing its NUMA-aware scheduler | `src/distributed/worker_agent.cpp` |
| The TCP frame header carries its length in native byte order, so a cluster must share one endianness | `src/distributed/tcp_transport.cpp` |
| `accept_loop()` writes the `peers` vector while `send()` and `stop()` read it without a lock | `src/distributed/tcp_transport.cpp` |
| An offloadable op recorded on a `DynamicGraph` region runs its local fallback and is never shipped to a worker | `src/graph/task_graph.cpp` |
| The TCP transport has no encryption and no authentication | [`SECURITY.md`](SECURITY.md), [`DISTRIBUTED_GUIDE.md`](../guides/DISTRIBUTED_GUIDE.md) §12 |

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
| `check_docs_layout.py` reports `docs/api` and `docs/api-site` as outside the tree and `mainpage.md` as misnamed | `docs/api/`, `docs/api-site/` |
| `check_docs_layout.py` reports five links in the estate audit report that quote a link from the old README | `docs/agent/2026_10_05_ESTATE_AUDIT/REPORT.md` |
| Three CMake comments and one issue template cite documents by their paths before 2026-10-05 | `cmake/SushiRuntimeInstall.cmake`, `cmake/SushiRuntimeConfig.cmake.in`, `tests/package/CMakeLists.txt`, `.github/ISSUE_TEMPLATE/bug_report.yml` |
| The changelog has no entry for the dispatch-width fields added on 2026-09-21 or for the `sushicore` lower bound of 0.7.0 | `docs/reference/CHANGELOG.md` |
| Documents frozen in the archive cite each other and the manual by the paths they had before they moved | `docs/archive/slop/`, `docs/archive/superpowers/specs/`, `docs/archive/agent/reports/` |
