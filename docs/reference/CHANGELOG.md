# Changelog

## Unreleased


## v0.7.0 — 2026-11-23

- 2026-11-23 — tests: Turned off the core dump of the epoch slot-limit death test's child on Linux, where it took 13 to 18 s, and dropped its time-limit exemption (RW-172, `test_epoch_reclamation.cpp`).
- 2026-11-23 — tests: Took the Windows worker-agent pull test out of quarantine, since SushiCore 0.16.2 reruns a failed shard alone again; its cause stays open as RW-174 (KI-49).
- 2026-11-23 — cli: Required SushiCore 0.16.2 and synced its shared tools (`pyproject.toml`, `shared_tools.lock`).
- 2026-11-22 — execution: Replayed a chained plan only on a device whose driver reports `aspect::ext_oneapi_limited_graph`, since an OpenCL driver without command buffers refused every recording (`chain_replay.cpp`).
- 2026-11-22 — task: Guarded the lists of `InMemoryOffloadCoordinator` with a mutex, since pool workers call it at once; its accessors return copies (`in_memory_offload_coordinator.hpp`).
- 2026-11-22 — tests: Matched a death test's number with `.+`, which GoogleTest reads on Linux and Windows, and set Claim 4 to a per-tick allocation on both paths (`test_kernel_pointers.cpp`, `test_sushiruntime.cpp`).
- 2026-11-22 — ci: Ran the death tests of the ThreadSanitizer job in the threadsafe style, since TSan stops a forked child that starts a thread (`ci.yml`).
- 2026-11-22 — tests: Exempted the epoch slot-limit death test from the medium limit until RW-172, and quarantined twelve tests under AdaptiveCpp for KI-45 to KI-47 (`budget_exemptions.toml`, `SushiRuntimeTests.cmake`).
- 2026-11-22 — deps: Pinned SushiLog and SushiProfiler v0.5.1, which take SushiCore 0.16, so CI no longer installs an older SushiCore over the CLI's (`cli/sushistack.deps.toml`).
- 2026-11-22 — repository_hygiene: Give every test a name and suite that check_test_odr accepts (RW-136).
- 2026-11-22 — general: Clear the known findings of the shared checkers (RW-80).
- 2026-11-22 — tools: Synced the shared tools of SushiCore v0.16.1, whose check_test_odr reads only the rows an instantiation passes and the type of a parameter's member, and removed `tools/known_findings.toml`, which held no finding.
- 2026-11-22 — runtime: Give the four GRAPH_REPLAY units of RW-141 their mirrored tests (RW-146).
- 2026-11-22 — tests: Gave the TEST suites that shared a name with a fixture suite their own names, `Small_BoundaryAccessPins` and `Small_DeterminismValues` (RW-136).
- 2026-11-22 — cli: Ran `sr test` on an asan or tsan tree without the time limits, which a sanitizer breaks, and required SushiCore 0.16 (`project.py`, `cli/pyproject.toml`).
- 2026-11-22 — ci: Installed Intel's CPU OpenCL runtime on the Windows runner, which had no CPU device for the medium tests (`setup-opencl-cpu-windows`).
- 2026-11-22 — tools: Synced the shared tools of SushiCore v0.16.0, whose pre-commit hook checks the repository in place and writes no copy (`tools/hooks/pre_commit.py`).
- 2026-11-22 — memory: Refused a host USM request whose block would be over `PTRDIFF_MAX` bytes before the heap is asked, so a sanitized build returns AllocationFailed instead of aborting (`host_usm_source.cpp`).
- 2026-11-22 — execution: Built chain replay and the peer-access grant under AdaptiveCpp, whose queue has no `get_backend()`, without the oneAPI graph extension (`chain_replay.cpp`, `runtime_transfer_paths.cpp`).
- 2026-11-22 — execution: Built the AdaptiveCpp toolchain again by compiling chain replay only where the SYCL implementation has the oneAPI graph extension; a plan there runs as a chain (KI-44).
- 2026-11-21 — cli: Breaking change: `sr build` no longer removes the tree it configures; it refuses a tree of another generator or source folder and names `sr clean`, and configures any other difference in place (RW-171).
- 2026-11-21 — cli: Breaking change: Removed `sr build --clean`, and made `sr clean` the shared command, which says what it would remove and removes it only with `--yes`, selecting trees by `--type` (RW-170, `8470fc9a`).
- 2026-11-21 — cli: Renamed `sr run --sort` to `sr run --pick`, and kept `--sort` as a hidden alias that names `--pick` until 2027-03-25 (RW-169, `3b5e3c02`).
- 2026-11-21 — cli: Changed `sr run --sort` to list the built examples, each with the `@brief` of its source, in the order the build registers them; `sr run --sort --all` lists every program as before (RW-168, `cf375631`).
- 2026-11-21 — tests: Gave each test shard 5 seconds for its start-up on top of its case limits, so a shard of one small case is no longer killed under a loaded run (RW-166, RW-167, KI-42, KI-43, `e73921a6`).
