# Tests

The root `tests/` folder holds the tests that cross module boundaries: a test here names headers of
more than one module by path. A test of one module lives in `modules/<tier>/<module>/tests/`, builds
into `sr_<module>_tests`, and is documented in that folder's `README.md`.

Each folder has a README that records what its tests prove and why they are built the way they are.

| Folder | Holds | Program | Reasoning |
| --- | --- | --- | --- |
| `unit` | Tests of one component at a time that need headers of more than one module | `sr_functional_tests` | [unit/README.md](unit/README.md) |
| `integration` | Tests that run a graph through a `Runtime` or the execution engine | `sr_functional_tests` | [integration/README.md](integration/README.md) |
| `regression` | One sentinel test per defect that was found and fixed | `sr_functional_tests` | [regression/README.md](regression/README.md) |
| `allocation` | The steady-state allocation test, which replaces the global `operator new` | `sr_allocation_tests` | [allocation/README.md](allocation/README.md) |
| `benchmark` | Performance benchmarks and stress tests, built under `SR_BUILD_BENCHMARKS` | `sr_benchmark_tests` | [benchmark/README.md](benchmark/README.md) |
| `common` | The helpers and the entry point every test program shares | none; compiled into each program | [common/README.md](common/README.md) |
| `package` | A consumer project that builds against an installed SushiRuntime | `Package.InstallsToAPrefix`, `Package.RunsAConsumerWithNothingButThePrefix` | [package/README.md](package/README.md) |

`tests/CMakeLists.txt` declares the programs. One function,
`sushiruntime_add_test_program` in `cmake/SushiRuntimeTests.cmake`, builds every one of them, the
module programs included, so each is linked, placed and registered the same way.

## Labels

A test gets its CTest label from the prefix of its GoogleTest suite name, not from the folder its
file is in.

| Suite prefix | Label | Registered by | Time limit |
| --- | --- | --- | --- |
| `Unit_` | `unit` | `sr_functional_tests`, `sr_allocation_tests`, every `sr_<module>_tests` | 120 seconds |
| `Integration_` | `integration` | the same programs | 120 seconds |
| `Regression_` | `regression` | the same programs | 120 seconds |
| `Stress_` | `stress` | `sr_benchmark_tests` | 900 seconds |
| `Performance_` | `performance` | `sr_benchmark_tests` | 900 seconds |

The two `Package.*` tests are plain CTest tests and carry the label `package`.
`sr test --suite functional` runs the labels `unit`, `integration` and `regression`.

## Source comments

A source comment keeps one line. The `@see` line in a file's header gives the path of its
folder's README, and a `//` line that ends in `see README.md` refers to that same file.

The test names in the READMEs are the second argument of the `TEST` macro, so a heading can be
found in its source file by a plain search.
