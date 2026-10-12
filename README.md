# SushiRuntime

SushiRuntime is a C++17 library that runs a graph of tasks on the CPUs and GPUs of one machine
through SYCL. A program allocates typed buffers, records each unit of work with the buffers it
reads and writes, and runs the graph. The runtime orders the work from those declarations, runs
what does not conflict in parallel, and replays a graph it compiled once for every later step.

- **Dependencies come from the data.** A task that reads what another wrote runs after it. Two
  tasks that touch disjoint memory have no edge between them.
- **The runtime knows the machine.** It lists the SYCL devices, maps each to its NUMA node with
  hwloc, and pins its workers to cores.
- **It carries no numeric algorithm.** A reduction or a sort is a node kind a library above it
  records; SushiBLAS is that library.
- **Offload to other machines is optional.** The `distributed` module sends a named operation to
  a worker process over TCP. It is compiled only when `SR_ENABLE_DISTRIBUTED` is on.

## Documentation

[docs/README.md](docs/README.md) lists every page of the manual. Start with the
[introduction](docs/getting_started/INTRODUCTION.md); the modules and the execution model are in
[ARCHITECTURE.md](docs/architecture/ARCHITECTURE.md).

## Stability

SushiRuntime is below 1.0.0, and its public interface is not stable.

- A MINOR release (0.4, 0.5) may change or remove any public name, signature, header path or
  binary layout. A PATCH release does not.
- Pin an exact tag, such as `v0.4.0`. Do not follow a branch.
- Reach SushiRuntime through one adapter of your own: a single module that includes
  `<SushiRuntime/sushiruntime.hpp>` and exposes only what your code needs, in your own types.
  When a release breaks the interface, that module is the only place you edit.

Two libraries built on SushiRuntime show parts of such an adapter. SushiBLAS records every
operation through one class and keeps the graph in another (`TaskRecorder` and `RecordedGraph`
in `sushiblas/modules/engine/engine`), and SushiAI turns every `SushiRuntime::Error` into its own
`Status` in one file (`sushiai/modules/core/core/source/seam.cpp`).
[The integration guide](docs/guides/INTEGRATION.md#6-write-one-adapter) shows how to write one.

## Requirements

- A SYCL 2020 compiler. `sr toolchain` or `-DSR_SYCL_TOOLCHAIN` selects one of three:
  - `intel-llvm`: `clang++ -fsycl` from the intel/llvm nightly bundle. The default, and what the
    container image builds with.
  - `adaptivecpp`: AdaptiveCpp (`acpp`).
  - `oneapi`: Intel oneAPI DPC++ (`icpx` on Linux, `icx-cl` on Windows).
- CMake 3.20 or newer.
- hwloc.
- GoogleTest, for the tests.
- A C++17 standard library.

`sr setup` installs the libraries and the SYCL toolchain that `cli/sushistack.deps.toml`
declares. [SushiStack](https://sushisystems.io) (the `hub` CLI) does the same for several
checkouts at once.

## Install

On a machine with nothing installed, SushiStack's bootstrap script installs Python, Git and
`hub`, then runs `hub install`:

```bash
curl -fsSL https://sushisystems.io/install.sh | bash
```

```powershell
irm https://sushisystems.io/install.ps1 | iex
```

In PowerShell `curl` is an alias for `Invoke-WebRequest`, so use `irm` there.

With only this checkout, install the CLI and let it install the dependencies:

```bash
hub install-cli sushiruntime
sr setup
```

The CLI is on the path as `sr` and as `sushiruntime`. It reads `cli/config.toml`, which is
committed, and `cli/config.local.toml`, which is ignored by git and holds the paths of one
machine.

## Build and test

```bash
sr toolchain intel-llvm
sr build
sr test
```

`sr build` makes a release build in `build/`. `sr test` runs the `functional` suite: the unit,
integration and regression tests. [The CLI guide](docs/guides/CLI_GUIDE.md) lists every command,
build type and suite, and [the architecture page](docs/architecture/ARCHITECTURE.md#7-build-time-configuration)
lists the CMake options.

Every build and test in this repository goes through `sr`. A project that consumes the library
calls CMake itself; see [Use it from another project](#use-it-from-another-project).

## A first program

```cpp
        constexpr std::size_t N     = 1024;
        constexpr std::size_t STEPS = 10000;

        auto rt = Runtime::create().value();          // discovers hardware, starts the workers

        // The runtime keeps two buffers and swaps them; a rule reads one, writes the other.
        auto heat = rt.state<float>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            heat[i] = (i == N / 2) ? 1.0f : 0.0f;   // a single hot cell in the middle

        auto sim = rt.graph();

        // The kernel receives the index and both buffers; the runtime tracks the access.
        sim.add_rule(heat, [](std::size_t i, const float* cur, float* next)
        {
            const float left  = (i == 0)     ? cur[i] : cur[i - 1];
            const float right = (i == N - 1) ? cur[i] : cur[i + 1];
            next[i] = cur[i] + 0.25f * (left - 2.0f * cur[i] + right);
        });

        // The graph compiles once and replays STEPS times.
        const auto report = sim.run(STEPS).value();
```

Source: `applications/examples/quickstart.cpp`

The program includes `<SushiRuntime/sushiruntime.hpp>` and writes
`using namespace SushiRuntime;`. `sr run sr_quickstart` runs it.

`Runtime::create()`, `state()` and `run()` return a `Result`. `value()` stops the process when
the result holds an error; a program that handles the error tests the result first, as
`applications/examples/operating_path_demo.cpp` does. No call of the library throws.

[applications/examples/README.md](applications/examples/README.md) describes the other examples.

## Use it from another project

```cmake
find_package(SushiRuntime REQUIRED)
target_link_libraries(my_simulation PRIVATE SushiRuntime::SushiRuntime)
```

A kernel is a template that your compiler instantiates, so every source file that records a
graph is compiled by a SYCL compiler. [The integration guide](docs/guides/INTEGRATION.md) says
what that means for a build, and `tests/package/` is a complete consumer.

## Distributed execution

`sr build --distributed` compiles the `distributed` module and two example programs. A master
attaches a cluster to its runtime and names an operation in a node's `NodeOptions`; a worker
that registered a kernel for that operation runs the node, and the master runs it itself when no
worker did.

```bash
sr run sr_distributed_demo
sr run sr_distributed_worker -- --master 127.0.0.1:5555
```

[The distributed guide](docs/guides/DISTRIBUTED_GUIDE.md) covers two machines, the shared
secret and the limits of the layer.

## Project layout

```
modules/                One folder per module, grouped by tier, lowest first.
  foundation/           core, report, region, log, machine (public);
                        logging, sycl (internal)
  platform/             topology, memory, device, observability (internal)
  engine/               task, scheduler, graph, execution (internal)
  surface/              interop, context, api (public);
                        memory_exchange, context_access (internal)
  extension/            distributed (public, optional)
applications/examples/  Nine example programs on the public API
tests/                  Tests that cross module boundaries, and tests/package/, a consumer
                        built against the installed package
cmake/                  Build functions and the tier table (SushiRuntimeTiers.cmake)
cli/                    The sr command line interface, a Python package
tools/                  Checkers that read the tree; see tools/README.md
docs/                   The manual, indexed by docs/README.md
.config/                Tool configuration: doxygen/Doxyfile, docker/Dockerfile
.github/workflows/      Continuous integration and the release job
```

A public module's headers are installed and its names are in `SushiRuntime::`. An internal
module's headers are not installed and its names are in `SushiRuntime::Internal::`. Each module
has a `README.md` that states what it owns.

## License

SushiRuntime is source-available and free for noncommercial use under the PolyForm Noncommercial
License 1.0.0; see [LICENSE](LICENSE). Commercial use needs a licence from Sushi Systems; see
[COMMERCIAL.md](COMMERCIAL.md). Third-party components keep their own licences, listed in
[NOTICE.md](NOTICE.md).
