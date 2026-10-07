# SushiRuntime

SushiRuntime is a C++17 runtime library for task-based parallel computing on heterogeneous hardware. You describe your work as a graph of tasks, declare which memory each task reads and writes, and the runtime figures out a safe execution order, then runs the independent parts in parallel across CPU and GPU devices using SYCL.

It is built around four ideas:

- **Automatic dependency tracking.** You don't wire up dependencies by hand. Tasks declare their memory accesses, and the runtime detects read-after-write, write-after-read, and write-after-write hazards for you.
- **Topology awareness.** The runtime discovers your hardware (CPUs, GPUs, NUMA nodes) and maps devices to the NUMA nodes they are physically attached to, so work and memory can stay close together.
- **Lock-free hot paths.** Scheduling, dependency resolution, reference counting, and memory reclamation use atomics instead of mutexes wherever it matters, to keep contention and latency low under heavy load.
- **Optional distributed offload.** An opt-in master-worker layer can offload individual tasks to remote machines over a thin TCP layer (no MPI). It is off by default — the runtime is a fully offline, single-machine library unless you explicitly turn it on. See [Distributed execution](#distributed-execution-optional).

## Documentation

The manual is indexed in [docs/README.md](docs/README.md). Start with the
[introduction](docs/getting_started/INTRODUCTION.md); the layers are described in
[ARCHITECTURE.md](docs/architecture/ARCHITECTURE.md).

## Requirements

- A SYCL 2020 compiler. Three toolchains are supported, selected with
  `SR_SYCL_TOOLCHAIN` (or `sr toolchain`):
  - **intel-llvm** (`clang++ -fsycl` from the intel/llvm nightly bundle) — the
    primary, vendor-neutral toolchain. Recommended; what the Docker image uses.
  - **AdaptiveCpp** (`acpp`) — secondary, vendor-neutral.
  - **Intel oneAPI DPC++** (`icpx` on Linux, `icx-cl` on Windows) — supported,
    kept for Intel profiling tools (VTune/Advisor).
- **CMake** 3.20 or newer (3.25 or newer required for the Intel oneAPI toolchain).
- **hwloc** (hardware locality library) for topology discovery.
- **GoogleTest** for the test suite.
- A C++17 standard library. The codebase targets C++17 and ships its own shims
  for `<bit>`, `<span>`, `std::expected`-style results, and portable SYCL
  `host_task` invocation; on a C++20/23 toolchain these thin out to standard
  aliases.

`sr setup` installs the C++ libraries and the SYCL toolchain this repo declares
in `cli/sushistack.deps.toml` into `~/.sushisystems`.
[SushiStack](https://sushisystems.io) (the `hub` CLI) provisions several
checkouts at once.

## Quick start (one script)

On a fresh machine you can go from nothing to a working build with a single
command. SushiStack's bootstrap script installs Python/Git, installs `hub`, then
runs `hub install` to fetch every toolchain and library the workspace's modules
declare (SushiRuntime declares its own in `cli/sushistack.deps.toml`):

```bash
curl -fsSL https://sushisystems.io/install.sh | bash
```

```powershell
irm https://sushisystems.io/install.ps1 | iex
```

With only this checkout, install the CLI and run `sr setup`; it installs what
`cli/sushistack.deps.toml` declares without a workspace.

On Windows use `irm`, not `curl` — in PowerShell `curl` is an alias for
`Invoke-WebRequest` and does not pipe a script the same way.

See the [SushiStack](https://sushisystems.io) docs for linking an existing
checkout of this repo into a workspace (`hub link`) instead of a fresh clone,
and for `hub doctor` / `hub status` to inspect what's installed.

### Adding a dependency

See [CONTRIBUTING.md §7](docs/CONTRIBUTING.md#7-adding-a-c-dependency) — dependencies
are declared in this repo's `cli/sushistack.deps.toml` fragment, which `sr setup`
installs.

### Using SushiRuntime from another project

`find_package(SushiRuntime REQUIRED)` and link `SushiRuntime::SushiRuntime`.
Read [INTEGRATION.md](docs/guides/INTEGRATION.md) first — the runtime's kernels are header
templates instantiated in *your* translation units, which decides how your build
has to be shaped, and it is the one thing that is not obvious from the API.
`tests/package/` is a complete working consumer.

## Building

The fastest path is the bundled CLI, which loads your compiler/toolchain paths from config and then drives CMake. Install it through the umbrella — the single install seam for the whole stack (it sets up an isolated pipx venv and wires in the shared `sushicore` presentation layer):

```bash
hub install-cli sushiruntime           # always an editable install
```

This gives you two equivalent commands on your PATH: **`sr`** (short) and **`sushiruntime`** (long). Toolchain paths are read from `cli/config.toml` (committed, shared defaults). Machine-specific absolute paths (oneAPI/VS roots, vcpkg, compiler locations) go in a gitignored `cli/config.local.toml` using the same `[tool.<platform>]` section layout — copy the commented Windows block from `config.toml` as a starting point.

Pick which SYCL toolchain to build with (persisted to `cli/config.local.toml`):

```bash
sr toolchain                  # list choices, show the active one
sr toolchain intel-llvm       # clang++ -fsycl (intel/llvm nightly) — primary (default)
sr toolchain adaptivecpp      # AdaptiveCpp (acpp) — secondary, vendor-neutral
sr toolchain oneapi           # Intel oneAPI (icpx/icx-cl) — supported, profiling
```

The selection flows to CMake as `-DSR_SYCL_TOOLCHAIN`, and the build derives the
matching compiler unless you pin one in config. Then build:

```bash
sr build                      # release (default)
sr build --type debug         # debug build
sr build --type relwithdebinfo
sr build --type asan          # isolated build-asan tree, CUDA off
sr build --distributed        # enable the master-worker layer
sr build --no-cuda            # build the portable CPU/OpenCL path
sr build --no-test            # skip compiling the test suite
sr build --force              # fall back to clang++/clang when the
                                      # selected toolchain compiler is missing
```

Add `--clean` to wipe the build tree first. `sr clean` removes both
`build/` and `build-asan/` trees entirely. To uninstall: `pipx uninstall sushiruntime-cli`.

### Building with CMake directly

If you prefer to skip the wrapper:

```bash
# intel-llvm (primary). A SYCL-enabled clang++ is picked up automatically.
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSR_SYCL_TOOLCHAIN=intel-llvm \
  -DSR_BUILD_TESTS=ON            # tests are OFF by default here; the `sr` wrapper turns them ON
cmake --build build --config Release
```

> **Note:** `SR_BUILD_TESTS` defaults to **OFF** for a bare library build, so a
> direct `cmake` invocation without `-DSR_BUILD_TESTS=ON` produces no test
> binaries and `ctest` finds nothing to run. (`sr build` enables it for you;
> `sr build --no-test` opts out.)

Use a CMake preset to switch toolchains: `cmake --preset intel-llvm` (`clang++
-fsycl`), `--preset adaptivecpp`, or `--preset oneapi` (run the oneAPI `setvars`
first).
On Windows, vcpkg is discovered from the `VCPKG_ROOT` environment variable
(falling back to `C:/vcpkg` only if it exists); the oneAPI compiler is resolved
from `CMPLR_ROOT` — no paths are hardcoded.

The GPU backend is selected by `-DSR_GPU_BACKEND=<auto|cuda|rocm|intel|cpu|none>`
(default `auto`, which detects the installed toolkit — NVIDIA CUDA, AMD ROCm,
Intel — and otherwise builds the portable CPU/OpenCL path). To force NVIDIA GPUs,
configure with `-DSR_GPU_BACKEND=cuda`. The architecture comes from
`-DSR_CUDA_ARCH=<cc>` (e.g. `61` for Pascal, `86` for Ampere), which has **no
default**. It is resolved from `nvidia-smi`: one capability builds for that one,
several different ones build for all of them (announced in a `STATUS` line —
pass a single value to cut build time), and none either falls back to the CPU
path with a warning, if the backend was auto-selected, or fails the configure
step, if you named the backend yourself. `SR_AMD_ARCH` works the same way
against `rocm_agent_enumerator`. The older
`-DSR_ENABLE_CUDA=ON` still works as a legacy alias for `-DSR_GPU_BACKEND=cuda`.

To build the optional distributed master-worker layer, configure with `-DSR_ENABLE_DISTRIBUTED=ON`, or pass `--distributed` to the CLI (`sr build --distributed`). It is **OFF by default**; when off, none of the distributed code or its sources are compiled, so the standalone build is unaffected. See [Distributed execution](#distributed-execution-optional).

Two options govern code generation. `-DSR_DETERMINISTIC_FP` (default **ON**)
forbids fast-math and FMA contraction in every translation unit — the runtime's
and, through the installed package, the consumer's — which is what makes replay
byte-equal between two builds of the same source; turn it off only for a
throughput workload that never replays. `-DSR_NATIVE_ARCH` (default **OFF**)
compiles for the build machine's exact ISA; it is a developer convenience for
benchmarking and must stay off for any binary that leaves the machine, since the
result faults on older CPUs. See
[Build-time configuration](docs/architecture/ARCHITECTURE.md#14-build-time-configuration).

### Docker

A `Dockerfile` based on `nvidia/cuda` (Ubuntu 22.04) with a pre-built **intel/llvm** nightly SYCL bundle is provided, so you can build and run — including the CUDA backend — without installing a toolchain locally. The image ships the `intel-llvm` (primary) and `adaptivecpp` toolchains; the `intel-llvm` path is the tested default (`sr toolchain intel-llvm`). It also installs the Intel OpenCL CPU runtime so the host CPU shows up as a SYCL device. For GPU passthrough you need the NVIDIA Container Toolkit.

```bash
sr container build
sr container build --oneapi    # also install the Intel oneAPI DPC++ compiler (icpx; several GB)
sr container build --no-cache  # rebuild every layer from scratch
sr container run               # add --admin for --privileged (profiling)
sr container run --no-gpu      # omit --gpus all (headless / CI environments)
```

The `oneapi` toolchain is opt-in because `icpx` is large: build with `--oneapi`, then `sr toolchain oneapi && sr build` works inside the container (the CLI sources oneAPI's `setvars.sh` automatically).

### Other CLI commands

```bash
sr config         # print resolved config with per-field source annotation
sr env            # print the environment cmake/ctest/run subprocesses see
sr env --asan     # same for the build-asan/ tree
sr docs           # build the fluent API reference site into build/docs/api-site/html
sr package        # install the built tree to a prefix (default build/dist) and
                  # archive it with CPack; --no-archive installs only
sr --version      # print the CLI's distribution name and version
sr --describe     # print every command and its options as JSON
```

`sr test --suite package` checks the result: it installs to a prefix, then builds
and runs `tests/package/` against it with nothing else on the path.

## Running the tests

The test suite is split into two binaries:

- `sr_functional_tests` — fast, deterministic unit, regression, and integration tests. Run these on every change.
- `sr_benchmark_tests` — heavier stress and performance tests (large task graphs, allocator latency probes). Built by default; the CI fast lane turns it off with `-DSR_BUILD_BENCHMARKS=OFF`.

Run them through the CLI or with `ctest`:

```bash
# via CLI — umbrella suites
sr test --suite functional           # unit + integration + regression
sr test --suite benchmark            # stress + performance
sr test --suite all

# fine-grained suite labels (subset of functional or benchmark)
sr test --suite unit
sr test --suite integration
sr test --suite regression
sr test --suite stress
sr test --suite performance

sr test --suite functional --filter 'Steal.*'   # ctest -R over test names
sr test --suite functional --asan    # run against the build-asan/ tree
sr test --suite functional --repeat 5  # re-run until first failure (flaky hunt)

# or run a binary directly
sr run sr_functional_tests
sr run sr_benchmark_tests -- --gtest_filter='AllocatorBench.*'
sr run --sort                        # pick an executable interactively
sr run sr_functional_tests --asan    # run from build-asan/, with ASAN env set

# via ctest, by label
ctest --test-dir build -L functional
ctest --test-dir build -L benchmark
ctest --test-dir build -L unit
ctest --test-dir build -L integration
ctest --test-dir build -L regression
```

### Continuous integration

GitHub Actions (`.github/workflows/ci.yml`) runs the functional suite on every push/PR and the benchmark suite on a nightly schedule, both on Ubuntu inside the `intel/oneapi-basekit` image (the Intel oneAPI DPC++ `icx`/`icpx` toolchain). The local Docker image (`Dockerfile`) uses the `intel-llvm` toolchain (intel/llvm nightly bundle), the project's primary path for host development. AdaptiveCpp is the secondary toolchain. All three are supported.

## How it works

A typical flow looks like this:

1. Create a `Runtime` (or a raw `RuntimeContext`). It probes hardware, builds SYCL queues per device, sets up USM allocators, and starts the worker pool.
2. Add work to a graph. Through the fluent API you allocate typed handles (`Buffer<T>` / `State<T>`) and the runtime tracks the memory each unit reads and writes; through the low-level API each task declares those accesses itself.
3. Run the graph once or for many steps. Dependencies are resolved automatically; the runtime executes independent work in parallel and, for stepped runs, replays a graph it compiled once.

### Fluent API (`api/`)

The `api/` layer is the intended entry point. You allocate data as typed
handles, describe each unit of work as plain C++ over those handles, and run the
graph. The runtime tracks the reads and writes, places the work, and — for
stepped workloads — compiles the graph once and replays it.

```cpp
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

auto rt = Runtime::create();

// A time-evolving field; the runtime double-buffers it across steps.
auto field = rt.state<float>(1024);
for (std::size_t i = 0; i < field.size(); ++i) field[i] = 0.0f;

auto sim = rt.graph();

// One update rule: read the current field, write the next. The runtime
// supplies the pointers, tracks the dependency, and swaps the buffers.
sim.add(field, [](std::size_t i, const float* cur, float* next) {
    next[i] = cur[i];   // a stencil over cur goes here
});

sim.run(1'000'000);                          // compile once, replay a million steps
// sim.run_for(std::chrono::seconds(8));     // or run for a wall-clock budget
// sim.run_until([&] { return converged(); }); // or until a condition holds
// field.current() now holds the latest state
```

`run()` is a single step; `run(n)` replays `n` steps; `run_for` and `run_until`
bound the run by wall-clock time or a predicate. The graph is compiled the first
time it runs and after any change to it, then replayed without recompiling.

Other ways to add work. A per-element or raw kernel captures its USM pointers, so
nothing in its type says what it touches and the runtime cannot infer the ordering
— those overloads are spelled `add_untracked` to say so at the call site. Name the
data with `Reads{...}`/`Writes{...}` and the same shapes are ordered:

```cpp
auto g = rt.graph();
g.add_untracked(1024, [](std::size_t i) { /* per-element, ordered against nothing */ });
g.add_untracked([](sycl::handler& h) { /* raw SYCL kernel, ordered against nothing */ });

// The same two shapes, ordered by the data they declare:
g.add(Reads(src), Writes(dst), 1024, [ps, pd](std::size_t i) { pd[i] = ps[i]; });
g.add(Reads(src), Writes(dst), [ps, pd](sycl::handler& h) { /* ... */ });

auto report = g.run();      // throws SushiException on error
auto result = g.try_run();  // non-throwing: Result<RunReport, Error>

Core::RunReport reused;     // and the allocation-free form for a frame loop:
g.run(reused);              // reset and refilled in place, no per-tick allocation

// report.total_tasks_executed, report.total_duration_ms,
// report.numa_statistics[i].tasks_stolen_intra_numa, etc.
```

A step can also be started without blocking. `run_async()` hands the step to the
worker pool and returns a `RunHandle`; `wait()` on the handle returns the
`RunReport` (or fills one you own), `try_wait()` is the non-throwing form, and the
destructor waits for an unwaited step rather than abandoning it. Only one step per
graph may be in flight.

```cpp
auto step = sim.run_async();
// ... host-side work that touches nothing the step writes ...
Core::RunReport r = step.wait();
```

Name a node with `name_last()` chained onto any `add()`. The name is what a task
shows up as in `RunReport` and in profiling output, so a graph worth measuring is
worth naming:

```cpp
sim.add(Reads(a), Writes(b), n, integrate).name_last("integrate");
```

Deterministic reductions are first-class. `add_reduce(input, output, n, op)`
folds the first `n` elements of a `Buffer<T>` into `output[0]` in fixed-size tiles,
so the result is bit-identical regardless of worker count, device, or steal
pattern. `add_segmented_reduce(values, segment_offsets, output, segment_count, op)`
does the same per segment over a CSR-style offsets array, one work-item per
segment. Both take an optional explicit identity for the empty case.

```cpp
g.add_reduce(values, total, n, Sum<float>{});
g.add_segmented_reduce(contributions, offsets, per_vertex, vertex_count, Sum<float>{});
```

Coupled fields read one another within a step. Every read sees the previous
step's data (a Jacobi update): each field advances only after the whole step's
kernels have run, so writing one field never changes what another reads in the
same step.

```cpp
auto u = rt.state<float>(N);
auto v = rt.state<float>(N);

sim.add(v, [](std::size_t i, const float* vc, float* vn) { /* vn[i] = ... */ });
sim.add(u, v, [](std::size_t i, const float* uc, float* un, const float* vc) {
    /* un[i] = f(uc, vc) */
});
```

#### Multidimensional, tiled, and bordered work

The same `add()` describes 2-D/3-D grids, tiled work-group kernels, and
interior-only updates without dropping to a raw `sycl::handler`. `Extent<N>`
names a global iteration box (optionally with an origin offset, which becomes
the halo it skips); `NdRange<N>` names a global range plus a local work-group
size for tiling and local memory.

```cpp
// 2-D per-element map: the kernel sees a sycl::id<2> over the whole box.
g.add_untracked(Extent{W, H}, [p](sycl::id<2> idx) { p[idx[1] * W + idx[0]] = /* ... */; });

// Tiled work-group kernel: global WxH, local 4x4, kernel sees sycl::nd_item<2>.
// Pass Reads/Writes first (as below) to have it ordered against the rest of the graph.
g.add(Reads(src), Writes(dst),
      NdRange{ sycl::range<2>(W, H), sycl::range<2>(4, 4) },
      [p](sycl::nd_item<2> it) { /* it.get_global_id(0), it.get_local_id(1), ... */ });

// Interior-only (halo) stencil over a State: iterate the (W-2)x(H-2) box at (1,1);
// the one-cell border stays fixed. See examples/stencil_demo.cpp.
Extent<2> interior{ sycl::range<2>(W - 2, H - 2), sycl::id<2>(1, 1) };
g.add(field, interior, [](sycl::id<2> idx, const float* cur, float* next) { /* ... */ });
```

Dependencies can also be declared explicitly when the kernel touches buffers the
argument list does not name. `Reads{...}`/`Writes{...}` register the hazards by
handle; `In(buf)`/`Out(buf)`/`InOut(buf)` both register the access and hand the
kernel the raw pointer as an argument.

```cpp
g.add(Reads{ src }, Writes{ dst }, N, [p](std::size_t i) { /* ... */ });
g.add(Extent{N}, In(src), Out(dst),
      [](sycl::id<1> i, const int* s, int* d) { d[i[0]] = s[i[0]] * 2; });
```

### Low-level API

The lower layers are available directly when you need full control:

```cpp
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/task_graph.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Graph;

RuntimeContext ctx;
TaskGraph graph(ctx);

int data = 0;
void* ptr = &data;

graph.add_task(TaskMetadata{"writer"}, /*reads*/{}, /*writes*/{ptr},
    [&](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event {
        data = 42; return {};
    });

graph.add_task(TaskMetadata{"reader"}, /*reads*/{ptr}, /*writes*/{},
    [&](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event {
        /* use data */ return {};
    });

auto compiled = graph.compile();
ctx.get_execution_engine().execute(std::move(compiled));
```

Tasks come in two flavors: **kernel** tasks that submit work through a `sycl::handler` (run on a device), and **host** tasks that run plain C++/library code on a queue. You can provide both, so a task can fall back to host execution when no device is selected.

## Architecture

The code is organized into layers under `include/SushiRuntime/` and `src/`.

### Public API (`api/`)
The fluent entry point that most users should start from:
- `Runtime` — static `create()` factory; owns the `RuntimeContext`. Exposes
  `graph()`, `dynamic_graph()`, `advanced()` (a read-only view of internal state),
  `unsafe_context()` (the deliberate escape hatch), and the typed data factories
  `buffer<T>()` / `state<T>()`.
- `Buffer<T>` / `State<T>` — owning handles to shared USM. `State<T>` is a
  double-buffered field for stepped workloads. Passing a handle to `add()`
  registers the task's reads and writes automatically.
- `API::Graph` — chainable builder. `add()` overloads take per-element, range,
  or raw kernels, `State` / `Buffer` handles (including coupled multi-field
  rules), multidimensional `Extent<N>` boxes (with halo offsets) and tiled
  `NdRange<N>` work-groups, and explicit `Reads`/`Writes`/`In`/`Out`/`InOut`
  dependency wrappers. `add_host()` runs native CPU work ordered by the data it
  touches; a leading `API::when()`/`sized()` (`Dynamic`) late-binds a task's
  enablement and iteration size without recompiling, with `sized_from_device()`
  and `based_at()` / `based_at_device()` taking the live count or the window's
  first element from the host or from a slot a predecessor kernel wrote; an
  `OpID` overload marks a task offloadable with a local fallback.
  `add_reduce()` / `add_segmented_reduce()` fold a `Buffer<T>` with a
  schedule-independent, bit-reproducible result. `name_last()` names the node just
  added, which is the name it carries in `RunReport` and profiling output.
  `run_async()` starts a step on the worker pool and returns a `RunHandle` whose
  `wait()` / `try_wait()` settles it. `buffer()`/`state()` take an optional
  `Residency` for device-resident data. `run()` / `run(n)` / `run_for()` /
  `run_until()` compile once and replay; `cancel()` stops a run from another
  thread; `try_run()` / `try_run(n)` are the non-throwing forms.
- `API::DynamicGraph` — the mutable counterpart for streaming, open-world
  workloads. `dynamic_graph()` returns one; `region(key)` records work on a
  partition with the same `add()` surface and `drop(key)` removes one. Regions
  added or dropped between steps recompose only what changed — an unchanged region
  is never rebuilt — and regions sharing a buffer are ordered across the boundary.
- `RunReport` — timing and steal statistics returned after each run. `run(report)`
  and `wait(report)` fill one the caller owns instead, so a steady-state tick
  allocates nothing; `RunReport::reset()` is the clear-but-keep-capacity step
  that makes it work.
- `Errc` / `Error` / `SushiException` — the error type hierarchy. `run()`
  throws; `try_run()` returns `Result<RunReport, Error>` without throwing.

### Core (`core/`)
Foundational utilities used everywhere:
- `RefCounted` + `sushi_ptr` — intrusive, atomic reference counting and a matching smart pointer. Cheaper than `shared_ptr` because the count lives inside the object.
- `logger` — an async, thread-safe logger (`SR_LOG_INFO`, `SR_LOG_ERROR`, etc.) with a synchronous mode for tests. Log level is compile-time gated.
- `common` — shared constants (cache line size, max workers, default alignment) and the `Backend` enum (SYCL, CUDA, HOST).
- `error` / `result` / `run_report` — `Errc`, `Error`, `SushiException`,
  `Result<T,E>`, and `RunReport` (see above; promoted into `SushiRuntime` namespace).
- Compatibility shims — `bit.hpp` (`popcount_u64`, `countr_zero_u64`), `span.hpp`
  (`SushiRuntime::span<T>`), `sycl_compat.hpp` (portable `host_task` invocation):
  C++17 stand-ins that alias standard facilities on C++20/23.
- `export` — the `SUSHIRUNTIME_API` macro for shared-library symbol visibility.

### Topology (`topology/`)
Discovers and describes the machine:
- `HardwareDiscovery` — enumerates SYCL devices and records name, memory size, compute units, GPU flag, and PCI address for each.
- `NUMAMapper` — uses hwloc to map each device to its nearest NUMA node and report distances between nodes.
- `affinity` — pins a thread to a specific CPU core (Windows and POSIX).
- `ITopologyProvider` / `INumaDistanceModel` — the topology source and the NUMA distance source are injected into `RuntimeContext` behind interfaces. Left null, it uses the production defaults `HwlocTopologyProvider` and `HwlocDistanceModel`.
- `MatrixDistanceModel` / `RemappingTopologyProvider` — test/diagnostic implementations that let multi-NUMA routing and NUMA-aware work stealing be exercised deterministically on single-node hardware: a hand-built distance matrix plus a decorator that spreads the real devices across a synthetic NUMA layout.

### Memory (`memory/`)
Allocation tuned for parallel, heterogeneous workloads:
- `UsmRegistry` — records which device each USM allocation belongs to and how long it is, so a pointer is freed against the queue that owns it, a kernel runs where its data lives, and an interior pointer can be traced back to its allocation. SYCL has no portable way to ask a pointer that, so the answer is recorded at allocation.
- `epoch_reclamation` — epoch-based reclamation (EBR) for safely freeing memory that other threads might still be reading, without locks. Retired nodes are pooled per thread to avoid hitting the global allocator on the hot path.

### Graph (`graph/`)
Builds and analyzes the task DAG:
- `TaskGraph` — the public builder. You add nodes/tasks; it analyzes them and `compile()`s them into a runnable form.
- `Node` — a single task. Tracks its incoming dependency count and successor list using atomics, with an inline successor buffer to avoid heap allocations for small fan-out.
- `DependencyTracker` — keeps a lock-free, sharded access history per memory pointer and resolves RAW/WAR/WAW hazards as tasks are added.
- `task_types` — task metadata, including a compile-time hashed `OpID` (so any compute domain can register named operations like `"blas.gemm"_op` without rigid enums).

> Graph construction (`add_node`/`add_task`) and `compile()` are not thread-safe relative to each other. Build the whole graph first, then compile.

### Scheduler (`scheduler/`)
Runs the ready tasks:
- `TaskScheduler` — a work-stealing scheduler. Each worker has its own deque; idle workers steal from busy ones. A global injector queue handles external submissions and a priority queue lets high-priority tasks jump ahead. Worker threads are persistent and reused across runs.
- `Task` — the base unit of work, with kernel and host execution modes and an optional `split()` for divisible workloads (see `RangeTask`).
- `EventPolling` — watches in-flight SYCL events and, when one completes, decrements its successors' dependency counts and enqueues the ones that became ready. Uses adaptive backoff to avoid spinning.
- `WorkloadCoordinator` + `rebalancer_interfaces` + `ThermalProxyStrategy` — an optional background load balancer, **off unless `RuntimeConfig::rebalancer` asks for it**. It watches per-worker queue length and completion rate and migrates tasks away from a worker whose queue is growing while its throughput stalls (a sign of thermal throttling or a slow core). The strategy is swappable at runtime.

### Execution (`execution/`)
Ties hardware and graphs together:
- `RuntimeContext` — the top-level owner. Holds devices, queues, USM allocators, and the execution engine. Uses the Pimpl pattern for a stable ABI. Gives you queues by index or by NUMA node, and tears everything down in the right order on shutdown.
- `IExecutionEngine` / `ExecutionEngine` — the execution strategy. The default engine drives a persistent `TaskScheduler`: it injects the graph's root nodes, runs until a sentinel completion node fires, and blocks until the whole graph is done. The interface makes it possible to plug in alternative strategies (dry-run, profiling, replay) without touching `TaskGraph` or `RuntimeContext`.

### Distributed (`distributed/`, optional — `SR_ENABLE_DISTRIBUTED`)
An opt-in master-worker layer that offloads individual tasks to remote workers. It is guarded so a standalone build compiles none of it.
- The key idea: **compiled code can't travel the wire**, so nothing is serialized except references. A task ships its `OpID` (a kernel already compiled into every node) plus `GlobalBufferID`+version handles; each worker resolves those to its own local USM. Closures and pointers never cross the network.
- `ITransport` abstracts the wire: `InProcessTransport` runs the full multi-node protocol on a single machine (the network analogue of `RemappingTopologyProvider`), and `TcpTransport` is a dependency-free raw-socket transport (POSIX + Winsock) for a real cluster.
- `DistributedCoordinator` (master) decides what to offload via a swappable `IOffloadPolicy`, serves a stateful, pull-based, master-authoritative buffer cache (a write auto-bumps a version, so stale worker caches re-pull), and releases a node's successors when its remote result arrives. `WorkerAgent` (worker) is a thin network shell around a normal local `RuntimeContext`, so the existing NUMA-aware scheduler runs the offloaded work.
- Integration with the core is minimal and guarded: `TaskGraph::compile()` stamps which nodes are offloadable, and the scheduler's hot path adds a single `[[unlikely]]` branch — non-offloaded tasks pay nothing. An offloadable task whose policy declines falls back to local execution transparently.

See [Distributed execution](#distributed-execution-optional) for how to run it.

## Project layout

```
include/SushiRuntime/   Public headers (this is the API surface)
  api/                  Fluent entry point: Runtime, Buffer/State handles,
                        API::Graph, RunReport, Error, Errc
  core/                 Ref counting, smart pointer, logger, constants,
                        error/result/run_report types, compat shims (bit, span, sycl_compat)
  topology/             Hardware discovery, NUMA mapping, thread affinity,
                        injectable topology/distance providers
  memory/               USM allocation ownership, the USM registry, epoch reclamation
  graph/                Task graph, nodes, dependency tracker, task types
  scheduler/            Work-stealing scheduler, tasks, event polling, rebalancer
  execution/            Runtime context, execution engine, ExecutionMetrics
  interop/              Host memory export and device memory import (API::Interop)
  distributed/          Optional master-worker offload (protocol, ByteWriter/ByteReader,
                        transports, KernelRegistry, BufferRegistry, coordinator, worker agent,
                        IOffloadPolicy, WorkerStats)
src/                    Implementations of the above
examples/               Tutorial apps, all on the fluent API: quickstart, vecadd_demo,
                        stencil_demo, coupled_fields_demo, game_engine_demo,
                        streaming_world_demo (always built); distributed_demo
                        (distributed builds only)
tests/
  common/               Shared GoogleTest entry point and helpers
  functional/           unit / regression / integration (fast, CI)
  benchmark/            stress / performance (heavy, nightly)
cli/                    Python `sr` / `sushiruntime` CLI package (Typer + rich)
.github/workflows/      CI: functional tests per push, benchmarks nightly
docs/                   The manual, indexed by docs/README.md; api/ holds the Doxygen main page
Doxyfile                Doxygen configuration for the fluent API reference
tools/                  Manual checkers and the packaging oracle; see tools/README.md
Dockerfile              CUDA + Clang/LLVM SYCL build environment
```

## Distributed execution (optional)

A master-worker offload layer lets the master ship individual tasks to remote workers, without MPI. It is **off by default** (`-DSR_ENABLE_DISTRIBUTED=ON` / `sr build --distributed` turns it on) and uses a raw-socket TCP transport — no extra third-party dependency. See the distributed subsection under [Architecture](#architecture) for the design.

The same binary runs in either role; the `sr` CLI forwards `--distributed=master|worker` to your application's `main()`, which constructs the `RuntimeContext` accordingly. A runnable demo (`examples/distributed_demo.cpp` → `sr_distributed_demo`) offloads a vector-add across one or more workers:

```bash
sr build --distributed

# Two processes on one host (worker retries until the master is up):
sr run sr_distributed_demo --distributed=worker --master=127.0.0.1:5555 &
sr run sr_distributed_demo --distributed=master --port=5555 --workers=1

# Across machines: run the master on one box and point each worker's
# --master at the master's IP.
```

The fluent API can drive the same offload without touching the protocol. A
`State` marked `distributed()` registers both of its double-buffered allocations
with the cluster, and `rt.cluster(transport, kernels, policy)` returns a handle
that hands tasks to workers under the chosen `IOffloadPolicy`. Because a `State`
swaps buffers every step, the offload handles are resolved at dispatch time
rather than baked at compile time, so the same `add(op, state, fallback)` step
runs remotely when a worker is available and falls back to local execution when
none is. `cluster.offloaded()` / `declined()` report what actually happened; see
`tests/functional/integration/test_distributed_e2e.cpp`
(`FluentDistributedStateParity`) for a worked end-to-end example.

The distributed tests (protocol, transports, buffer coherence, and SYCL end-to-end offload including multi-worker fan-out and TCP loopback) are part of the functional suite when the layer is enabled:

```bash
sr run sr_functional_tests --gtest_filter='Distributed*'
```

## License

SushiRuntime is source-available, free for non-commercial use under the PolyForm Noncommercial License 1.0.0; see [LICENSE](LICENSE). Commercial use needs a licence from Sushi Systems, see [COMMERCIAL.md](COMMERCIAL.md). Third-party components keep their own licences, listed in [NOTICE.md](NOTICE.md).
