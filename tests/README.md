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

`tests/support/` holds the headers every test may include as `"support/<name>.hpp"`.
`tolerance.hpp` is copied from SushiSkills and derives a floating-point tolerance from a rounding
bound, so no comparison spells one. `runtime_environment.hpp` registers one `testing::Environment`
per program that makes the runtime and the device queue on first use and releases them after the
last test; a medium test that needs either derives its fixture from `TestSupport::RuntimeTest` and
never builds its own. A test that needs another configuration asks the environment for the runtime
of a `RuntimeShape` (forced onto the scheduler, a worker count, the rebalancer), made once per
shape; `LayerRuntimeTest` runs a row on the default runtime (false) and on the one forced onto the
scheduler (true). `buffer_contents.hpp` reads, fills and windows a buffer's contents,
`slot_buffers.hpp` makes one device, one shared and one host counter buffer, and
`adopted_handles.hpp` builds a `Buffer` or `State` over a made-up address with
`RecordingAllocationOwner`, `installed_offload.hpp` installs an offload coordinator on a runtime for one
scope, `accepting_offload_coordinator.hpp` takes every node it is offered and completes it from a thread it
owns, `nd_kernel_math.hpp` holds the index math and arithmetic of the `NdRange` test kernels as host functions
of plain ids, so a small test calls them and the launch test passes them values out of `sycl::nd_item`, `worker_wire.hpp` holds the frames and handshake builders a test of a worker plays its master with,
`heap_cache_allocator.hpp` is the heap adapter of the worker cache's allocator port,
`executor_fabric.hpp` joins a master and a worker endpoint on `ExecutorDrive`s that a test steps, and `recording_transport.hpp` is a `Transport` that records the frames sent and the peers closed and sends none, so a unit that sends control frames is tested without a connection, `digest_bytes.hpp` views text and byte vectors as byte spans and writes a
SHA-256 digest as hexadecimal text.

One function, `sushiruntime_add_test_program` in `cmake/SushiRuntimeTests.cmake`, builds every
program, so each is linked, placed and registered the same way.

A test unit that instantiates a kernel, by recording one through the API or by calling
`parallel_for` or `single_task` itself, is listed in `DEVICE_SOURCES` beside its program's
sources. Only those units compile with `-fsycl` and the device targets; the others compile for
the host, and a kernel recorded in one of them stops the build on the assertion of
`api/detail/device_pass.hpp`. `tools/layering/check_device_sources.py` checks the lists.

## Sizes

A test's size is its CTest label, taken from the prefix of its GoogleTest suite name, never from
a folder. Every `sr_<module>_tests` program and `sr_api_allocation_tests` registers the three
prefixes; `cmake/SushiRuntimeTests.cmake` sets the label and the time limit. A suite with no
size prefix would register no test, so `cmake/CheckTestSizes.cmake` fails the build of its
program.

| Suite prefix | Label | The test may use | Time limit |
| --- | --- | --- | --- |
| `Small_` | `small` | The memory of its own process, one thread, an injected clock and seed; no device, file, socket, sleep or child process | 1 second |
| `Medium_` | `medium` | The device the program made once (`tests/support/runtime_environment.hpp`), threads, a death test's child process, localhost, a per-test temporary folder | 10 seconds |
| `Large_` | `large` | A real GPU; the test skips when none is present | 120 seconds |

An intermittent test is quarantined by naming it in `SR_QUARANTINED_TESTS` in
`cmake/SushiRuntimeTests.cmake`, with an entry in `docs/backlog/README.md`. It keeps its
size and gains the label `quarantined`.

`sr test --label <label>` runs one label; `sr test` with no label runs the three sizes. Every run
but `--label quarantined` leaves the quarantined tests out. They run in the nightly workflow,
where they never gate. The two `Package.*` tests are plain CTest tests and carry the label
`package`.

The `sr_<module>_benchmarks` programs are not registered with CTest. `sr bench` runs them.

## What a test may reach

- A test reaches a module through its public headers. A promise only a private header or a
  surface module's internal name could show is a backlog entry, not a test.
- A port is tested through its in-memory adapter, which ships beside the port; no test writes a
  stand-in of a type the repository owns.
- A test's size follows what it touches: a bridge test that touches no device is small, and a
  death test is medium, since it starts a child process.
- A header whose promises are all compile-time has a `compile_<name>.cpp` beside its test path,
  which includes it alone and is never run.
- Trivial code, such as an empty default or a one-line comparison, has no test.

## Source comments

A source comment keeps one line. The `@see` line in a file's header gives the path of its module's
test README, and a `//` line that ends in `see README.md` refers to that same file.

The test names in the READMEs are the second argument of the `TEST` macro, so a heading can be
found in its source file by a plain search.
