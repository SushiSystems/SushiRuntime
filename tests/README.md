# Tests

`tests/` mirrors `modules/`. The tests of the header
`modules/<tier>/<module>/include/SushiRuntime/<module>/<name>.hpp` are in
`tests/<tier>/<module>/test_<name>.cpp`, and its benchmark is `benchmark_<name>.cpp` beside it. A
test that crosses modules belongs to the highest-tier module it uses, in the file of the unit whose
behaviour it checks. `tools/testing/check_test_mirror.py` checks the mirror.

Each `tests/<tier>/<module>/` folder has a `CMakeLists.txt` that builds `sr_<module>_tests` and,
under `SR_BUILD_BENCHMARKS`, `sr_<module>_benchmarks`, and a `README.md` that records what its tests
prove and why they are built the way they are. `tests/CMakeLists.txt` adds the folder of every
module that is switched on.

| Folder | Holds | Program |
| --- | --- | --- |
| `foundation/`, `platform/`, `engine/`, `surface/`, `extension/` | The tests of each module, one folder per module | `sr_<module>_tests`, `sr_<module>_benchmarks` |
| `package/` | A consumer project that builds against an installed SushiRuntime | `Package.InstallsToAPrefix`, `Package.RunsAConsumerWithNothingButThePrefix` |

`tests/surface/api/test_sushiruntime.cpp` builds its own program, `sr_api_allocation_tests`: it
replaces the global `operator new`, and a replacement is program-wide.

The code the tests of several modules share is in internal modules that only the tests build:
`test_runner` holds the entry point of every program, `measurement`, `thread_barrier`,
`device_probe` and `usm_guard` sit in the foundation tier, and `probe_tasks` and
`runtime_assembly` in the engine tier. Their READMEs are in `modules/<tier>/<module>/`.

One function, `sushiruntime_add_test_program` in `cmake/SushiRuntimeTests.cmake`, builds every
program, so each is linked, placed and registered the same way.

## Labels

A test gets its CTest label from the prefix of its GoogleTest suite name, never from a folder.

| Suite prefix | Label | Registered by | Time limit |
| --- | --- | --- | --- |
| `Unit_` | `unit` | every `sr_<module>_tests` and `sr_api_allocation_tests` | 120 seconds |
| `Integration_` | `integration` | the same programs | 120 seconds |
| `Regression_` | `regression` | the same programs | 120 seconds |
| `Stress_` | `stress` | every `sr_<module>_benchmarks` | 900 seconds |
| `Performance_` | `performance` | every `sr_<module>_benchmarks` | 900 seconds |

The two `Package.*` tests are plain CTest tests and carry the label `package`.
`sr test --suite functional` runs the labels `unit`, `integration` and `regression`.

## Source comments

A source comment keeps one line. The `@see` line in a file's header gives the path of its module's
test README, and a `//` line that ends in `see README.md` refers to that same file.

The test names in the READMEs are the second argument of the `TEST` macro, so a heading can be
found in its source file by a plain search.
