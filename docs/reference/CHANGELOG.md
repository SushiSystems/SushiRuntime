# Changelog

## Unreleased

- 2026-11-06 — cli: Gave the project, package and container commands a `Surroundings` seam so the CLI tests run on sushicore's recording runner and console; the CLI needs sushicore 0.10.12 (`surroundings.py`).
- 2026-11-06 — cli: Fixed `sr config` failing with an AttributeError under sushicore 0.10.12, which reads `_ENV_OVERRIDES` from the configuration module (`config.py`).
- 2026-11-06 — tests: Set the size time limits to 1 s, 10 s and 120 s, the testing skill's numbers, and removed `K_ALL_KEYS`, which nothing read (`cmake/SushiRuntimeTests.cmake`, `profile_keys.hpp`).
- 2026-11-06 — tests: Named every benchmark suite `Benchmark_` for `sr bench` and moved the promises the benchmarks checked into sized tests; the epoch stress tests became `Large_` (`test_epoch_reclamation.cpp`).
- 2026-11-06 — tests: Added in-memory adapters for the rebalancer, offload and topology ports, beside each port (`in_memory_telemetry.hpp`, `in_memory_offload_coordinator.hpp`, `in_memory_topology_provider.hpp`).
- 2026-11-06 — tools: Fixed the mirror checker refusing `tests/support`, the shared test helper folder, by taking SushiSkills' copy (KI-26, `check_test_mirror.py`).
- 2026-11-06 — licence: Relicensed SushiRuntime under the Apache License 2.0 and removed `COMMERCIAL.md`; every source file's licence block follows (`LICENSE`, `tools/settings.py`).
- 2026-11-05 — tools: Took the shared checkers of 2026-11-05 with a baseline of the findings they meet, which RW-80 clears.
- 2026-10-30 — build: Fixed a `tsan` build failing under `-Werror` on every unit compiled without `-fsycl`, which has no device pass for the TSan opt-out to apply to (`cmake/Sanitizers.cmake`).
- 2026-10-30 — cli: Made `sr build --type tsan` link SushiLog and SushiProfiler packages built with `--sanitize thread`, as RelWithDebInfo packages; it linked Release ones (`library_packages.py`).
## v0.5.1 — 2026-10-30

- 2026-10-29 — ci: Gave the sanitizer job 90 minutes and the SushiBLAS job 150; both ran out of time building their packages (`.github/workflows/ci.yml`).
- 2026-10-29 — tests: Fixed every Windows Debug test program crashing on load; the tree stages the Release gtest.dll it links instead of the Debug one vcpkg copied (`cmake/SushiRuntimeTests.cmake`).
- 2026-10-29 — tests: Quarantined five medium tests under AdaptiveCpp, whose CPU backend fails them, as KI-18 to KI-22 with RW-75 to fix them (`cmake/SushiRuntimeTests.cmake`).
- 2026-10-29 — scheduler: Fixed two scheduler tests failing to compile under AdaptiveCpp, whose handler has no `host_task`; they enqueue through `Detail::enqueue_host_task` (`test_dispatch_record.cpp`, `test_event_polling.cpp`).
- 2026-10-29 — ci: Built SushiBLAS against this commit with icpx, the compiler the oneAPI jobs use, so its SushiRuntime package finds IntelSYCL (`.github/workflows/ci.yml`).
- 2026-10-29 — device: Fixed the device line failing to compile under AdaptiveCpp, whose backends carry other names than DPC++'s (`device_queue_set.cpp`, `runtime_environment.cpp`).
- 2026-10-29 — build: Gave the Debug line tables to the host pass alone under intel/llvm, whose spir64 device pass rejects the flag (`cmake/Optimization.cmake`).
- 2026-10-29 — build: Let a Debug tree link an imported package built as `None`, such as Ubuntu's GTest (`cmake/SushiRuntimeLanguage.cmake`).
- 2026-10-29 — deps: Moved the SushiProfiler pin to v0.4.1, whose CLI installs against sushicore 0.10 (`cli/sushistack.deps.toml`).
- 2026-10-29 — ci: Installed the CLI of every pinned Sushi library beside its clone, since a library package is built by that library's own CLI (`.github/actions/setup-sr/action.yml`).
- 2026-10-29 — deps: Moved the SushiLog pin to v0.3.0, the first tag a consumer can build as a package (`cli/sushistack.deps.toml`).
- 2026-10-29 — cli: Handed the library packages the compiler as the build environment's PATH finds it, so a oneAPI `icpx` that only setvars puts on the PATH no longer stops `sr build` (`services/project.py`).
- 2026-10-29 — ci: Cloned the Sushi libraries from the `[sushisystems.*]` pins, which the clone steps still read as `[sushi.*]`, and installed the CLI before the checkers so they find sushicore (`.github/actions/setup-sr/action.yml`).
- 2026-10-29 — cli: Imported `configure_arguments` from sushicore where the build calls it (`services/project.py`).
- 2026-10-29 — cli: Fixed `sr package` printing every line of `cmake --install`; it prints one line counting the files through sushicore 0.10.3, and cmake's output only on a failure (`packaging.py`).
- 2026-10-29 — cli: Fixed the command-list tests that the `backlog` and `design` groups broke; sushicore's groups are checked by name only (`test_cli.py`, `test_help_screen.py`).
