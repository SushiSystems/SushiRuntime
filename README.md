# SushiRuntime

SushiRuntime is a C++20 runtime library for task-based parallel computing on heterogeneous hardware. You describe your work as a graph of tasks, declare which memory each task reads and writes, and the runtime figures out a safe execution order, then runs the independent parts in parallel across CPU and GPU devices using SYCL.

It is built around three ideas:

- **Automatic dependency tracking.** You don't wire up dependencies by hand. Tasks declare their memory accesses, and the runtime detects read-after-write, write-after-read, and write-after-write hazards for you.
- **Topology awareness.** The runtime discovers your hardware (CPUs, GPUs, NUMA nodes) and maps devices to the NUMA nodes they are physically attached to, so work and memory can stay close together.
- **Lock-free hot paths.** Scheduling, dependency resolution, reference counting, and memory reclamation use atomics instead of mutexes wherever it matters, to keep contention and latency low under heavy load.

## Requirements

- A SYCL compiler. Two toolchains are supported:
  - **Intel oneAPI DPC++** (`icpx` on Linux, `icx-cl` on Windows), or
  - an **intel/llvm** open-source build (`clang++ -fsycl`), which is what the Docker image uses.
- **CMake** 3.20 or newer.
- **hwloc** (hardware locality library) for topology discovery.
- **GoogleTest** for the test suite.
- A C++20 standard library (the code uses `<format>`, `<source_location>`, `<bit>`, `<concepts>`, etc.).

On Windows, dependencies are resolved through **vcpkg**. On Linux they can come from the system package manager or vcpkg.

## Building

The fastest path is the bundled CLI wrapper, which loads your compiler/toolchain paths from a config file and then drives CMake.

1. Copy the config template and fill in your local paths:
   - Windows: copy `.tools/config.ini.template` to `.tools/config.ini`
   - Linux/macOS: copy `.tools/config.cfg.template` to `.tools/config.cfg`
2. Build:

```bash
# Linux / macOS
python3 .tools/sushiruntime.py project build

# Windows
python .tools\sushiruntime.py project build
```

Add `--clean` to wipe the `build/` directory first.

You can also install a short `sr` command that wraps the same CLI: run `.tools/install.sh` (Linux/macOS) or `.tools/install.ps1` (Windows), optionally with `--permanent` to add it to your PATH via your shell profile. Then the commands above become e.g. `sr project build`. Use the matching `uninstall` script to remove it.

### Building with CMake directly

If you prefer to skip the wrapper:

```bash
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=icpx \
  -DCMAKE_C_COMPILER=icx
cmake --build build --config Release
```

On Windows, also pass the vcpkg toolchain file and the `icx-cl` compiler. The `CMakeLists.txt` will try to auto-detect a vcpkg install at `C:/vcpkg` and Intel oneAPI under `C:/Program Files (x86)/Intel/oneAPI` if you don't override them.

To target NVIDIA GPUs through SYCL's CUDA backend, configure with `-DSUSHI_ENABLE_CUDA=ON` (currently tuned for Pascal / `sm_61`).

### Docker

A `Dockerfile` based on `nvidia/cuda` (Ubuntu 22.04) with a pre-built **intel/llvm** nightly SYCL toolchain is provided, so you can build and run — including the CUDA backend — without installing a toolchain locally. It also installs the Intel OpenCL CPU runtime so the host CPU shows up as a SYCL device. For GPU passthrough you need the NVIDIA Container Toolkit (`.tools/install_nvidia_container_toolkit.sh` can set it up).

```bash
python3 .tools/sushiruntime.py docker build
python3 .tools/sushiruntime.py docker run
```

## Running the tests

The test suite is split into two binaries:

- `sr_functional_tests` — fast, deterministic unit, regression, and integration tests. Run these on every change.
- `sr_benchmark_tests` — heavier stress and performance tests (large task graphs, allocator latency probes). Built by default; the CI fast lane turns it off with `-DSUSHI_BUILD_BENCHMARKS=OFF`.

Run them through the wrapper or with `ctest`:

```bash
# via wrapper (runs the default target from your config)
python3 .tools/sushiruntime.py project run sr_functional_tests

# via ctest, by label
ctest --test-dir build -L functional
ctest --test-dir build -L benchmark
```

You can forward GoogleTest flags through the runner, e.g. `... project run sr_benchmark_tests --gtest_filter='AllocatorBench.*'`.

### Continuous integration

GitHub Actions (`.github/workflows/ci.yml`) runs the functional suite on every push/PR and the benchmark suite on a nightly schedule, both on Ubuntu inside the `intel/oneapi-basekit` image (the Intel oneAPI DPC++ `icx`/`icpx` toolchain). The local Docker image (`Dockerfile`) instead uses an intel/llvm open-source build — both SYCL toolchains are supported.

## How it works

A typical flow looks like this:

1. Create a `RuntimeContext`. It probes the hardware, builds SYCL queues per device, sets up USM allocators, and starts the worker pool.
2. Create a `TaskGraph` and add tasks. Each task declares its read and write pointers.
3. Call `graph.compile()`. This resolves dependencies and produces a `CompiledGraph`.
4. Hand the compiled graph to the execution engine, which runs it and blocks until completion.

```cpp
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/task_graph.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Graph;

RuntimeContext ctx;          // discovers hardware, starts workers
TaskGraph graph(ctx);

int data = 0;
void* ptr = &data;

// Writer runs first; the reader is automatically ordered after it
// because they touch the same pointer (read-after-write hazard).
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

### Core (`core/`)
Foundational utilities used everywhere:
- `RefCounted` + `sushi_ptr` — intrusive, atomic reference counting and a matching smart pointer. Cheaper than `shared_ptr` because the count lives inside the object.
- `logger` — an async, thread-safe logger (`SR_LOG_INFO`, `SR_LOG_ERROR`, etc.) with a synchronous mode for tests. Log level is compile-time gated.
- `common` — shared constants (cache line size, max workers, default alignment) and the `Backend` enum (SYCL, CUDA, HOST).
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
- `USMAllocator` — allocates Unified Shared Memory via SYCL. Allocation behavior is pluggable through `IAllocatorStrategy` (Shared, Device, Host).
- `memory_pool` — a bitmask-based pool with per-thread "magazines" so the common allocate/free path takes a couple of instructions with no locks or atomics. Bulk page claims reduce atomic contention; per-page ownership routes remote frees back to the owning slot.
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
- `WorkloadCoordinator` + `rebalancer_interfaces` + `ThermalProxyStrategy` — an optional background load balancer. It watches per-worker queue length and completion rate and migrates tasks away from a worker whose queue is growing while its throughput stalls (a sign of thermal throttling or a slow core). The strategy is swappable at runtime.

### Execution (`execution/`)
Ties hardware and graphs together:
- `RuntimeContext` — the top-level owner. Holds devices, queues, USM allocators, and the execution engine. Uses the Pimpl pattern for a stable ABI. Gives you queues by index or by NUMA node, and tears everything down in the right order on shutdown.
- `IExecutionEngine` / `ExecutionEngine` — the execution strategy. The default engine drives a persistent `TaskScheduler`: it injects the graph's root nodes, runs until a sentinel completion node fires, and blocks until the whole graph is done. The interface makes it possible to plug in alternative strategies (dry-run, profiling, replay) without touching `TaskGraph` or `RuntimeContext`.

## Project layout

```
include/SushiRuntime/   Public headers (this is the API surface)
  core/                 Ref counting, smart pointer, logger, constants
  topology/             Hardware discovery, NUMA mapping, thread affinity, injectable topology/distance providers
  memory/               USM allocator, memory pool, epoch reclamation
  graph/                Task graph, nodes, dependency tracker, task types
  scheduler/            Work-stealing scheduler, tasks, event polling, rebalancer
  execution/            Runtime context, execution engine
src/                    Implementations of the above
tests/
  common/               Shared GoogleTest entry point and helpers
  functional/           unit / regression / integration (fast, CI)
  benchmark/            stress / performance (heavy, nightly)
.tools/                 Build, run, docker, and install scripts + Python CLI (`sr`)
.github/workflows/      CI: functional tests per push, benchmarks nightly
docs/                   Changelog, TODO, and guides
Dockerfile              CUDA + intel/llvm SYCL build environment
Idea.md                 Design notes for a future distributed (master-worker) mode
```

## Roadmap

`Idea.md` sketches a planned distributed execution mode: a lightweight master-worker design that offloads individual tasks to remote workers over a thin network layer (ZeroMQ or Asio) when local resources are saturated, without pulling in a heavyweight dependency like MPI. This is a design proposal, not yet implemented.

## License

This software is the proprietary information of Mustafa Garip & Sushi Systems. All rights reserved. Unauthorized copying, disclosure, or use of this code is prohibited without explicit written permission from the copyright holders.
