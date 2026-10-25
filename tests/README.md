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

A test unit that instantiates a kernel, by recording one through the API or by calling
`parallel_for` or `single_task` itself, is listed in `DEVICE_SOURCES` beside its program's
sources. Only those units compile with `-fsycl` and the device targets; the others compile for
the host, and a kernel recorded in one of them stops the build on the assertion of
`api/detail/device_pass.hpp`.

## Sizes

A test's size is its CTest label, taken from the prefix of its GoogleTest suite name, never from
a folder. Every `sr_<module>_tests` program and `sr_api_allocation_tests` registers the three
prefixes; `cmake/SushiRuntimeTests.cmake` sets the label and the time limit. A suite with no
size prefix would register no test, so `cmake/CheckTestSizes.cmake` fails the build of its
program.

| Suite prefix | Label | The test may use | Time limit |
| --- | --- | --- | --- |
| `Small_` | `small` | One thread; no SYCL queue or context, no sleep, no file, no child process | 60 seconds |
| `Medium_` | `medium` | Threads, a death test's child process, localhost, a SYCL CPU device | 300 seconds |
| `Large_` | `large` | A real GPU; the test skips when none is present | 900 seconds |

An intermittent test is quarantined by naming it in `SR_QUARANTINED_TESTS` in
`cmake/SushiRuntimeTests.cmake`, with an entry in `docs/reference/KNOWN_ISSUES.md`. It keeps its
size and gains the label `quarantined`.

`sr test --label <label>` runs one label; `sr test` with no label runs the three sizes. Every run
but `--label quarantined` leaves the quarantined tests out. They run in the nightly workflow,
where they never gate. The two `Package.*` tests are plain CTest tests and carry the label
`package`.

The `sr_<module>_benchmarks` programs are not registered with CTest. `sr bench` runs them.

## Source comments

A source comment keeps one line. The `@see` line in a file's header gives the path of its module's
test README, and a `//` line that ends in `see README.md` refers to that same file.

The test names in the READMEs are the second argument of the `TEST` macro, so a heading can be
found in its source file by a plain search.
