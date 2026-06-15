# SushiRuntime

SushiRuntime is a C++20 runtime library for task-based parallel computing on heterogeneous hardware. You describe your work as a graph of tasks, declare which memory each task reads and writes, and the runtime figures out a safe execution order, then runs the independent parts in parallel across CPU and GPU devices using SYCL.

It is built around three ideas:

- **Automatic dependency tracking.** You don't wire up dependencies by hand. Tasks declare their memory accesses, and the runtime detects read-after-write, write-after-read, and write-after-write hazards for you.
- **Topology awareness.** The runtime discovers your hardware (CPUs, GPUs, NUMA nodes) and maps devices to the NUMA nodes they are physically attached to, so work and memory can stay close together.
- **Lock-free hot paths.** Scheduling, dependency resolution, reference counting, and memory reclamation use atomics instead of mutexes wherever it matters, to keep contention and latency low under heavy load.
- **Optional distributed offload.** An opt-in master-worker layer can offload individual tasks to remote machines over a thin TCP layer (no MPI). It is off by default — the runtime is a fully offline, single-machine library unless you explicitly turn it on. See [Distributed execution](#distributed-execution-optional).

## Requirements

- A SYCL compiler. Two toolchains are supported:
  - **Intel oneAPI DPC++** (`icpx` on Linux, `icx-cl` on Windows), or
  - an **intel/llvm** open-source build (`clang++ -fsycl`), which is what the Docker image uses.
- **CMake** 3.20 or newer.
- **hwloc** (hardware locality library) for topology discovery.
- **GoogleTest** for the test suite.
- A C++20 standard library (the code uses `<format>`, `<source_location>`, `<bit>`, `<concepts>`, etc.).

On Windows, dependencies are resolved through **vcpkg**. On Linux they can come from the system package manager or vcpkg.

## Quick start (one script)

On a fresh machine you can go from nothing to a working build with a single
command. The script bootstraps Python/Git, installs the `sr` CLI, then runs the
installer pipeline (`sr setup all`) which installs every remaining dependency,
generates `cli/config.local.toml`, and builds + smoke-tests the project.

Linux (Debian/Ubuntu):

```bash
bash install.sh            # add --gpu for CUDA, --dry-run to preview
```

Windows (PowerShell):

```powershell
powershell -ExecutionPolicy Bypass -File install.ps1   # add -Gpu / -DryRun
```

Both also work straight from the web on a bare machine:

```bash
curl -fsSL https://raw.githubusercontent.com/<org>/sushiruntime/main/install.sh | bash
```

```powershell
irm https://raw.githubusercontent.com/<org>/sushiruntime/main/install.ps1 | iex
```

The installer is a SOLID, dependency-injected pipeline
(`detect -> install -> configure -> verify`). You can run any phase on its own:

```bash
sr setup detect       # inventory tools, compiler, dependencies, GPU
sr setup install      # install missing deps (apt | winget + vcpkg)
sr setup configure    # write config.local.toml from probed tool paths
sr setup verify       # build + functional smoke test
```

### Adding a dependency

All C++ library dependencies live in a single manifest, `cli/dependencies.toml`
(the installer's source of truth). To add one, add a table:

```toml
[mylib]
description   = "What it is"
required      = true
linux_apt     = ["libmylib-dev"]
windows_vcpkg = ["mylib"]
check_cmd     = ["pkg-config", "--exists", "mylib"]   # optional idempotency check
```

The installer picks it up automatically; no installer code changes. If the build
itself needs it, also add the `find_package`/`pkg_check_modules` call in
`CMakeLists.txt`, and if the Docker image needs it, add it to the `Dockerfile`
apt line (those are build/image logic, intentionally outside the manifest).

## Building

The fastest path is the bundled CLI, which loads your compiler/toolchain paths from config and then drives CMake. The easiest install is the helper script (Windows → pipx, Linux/macOS → pip into the current environment):

```bash
python cli/install.py                 # add --editable for a dev install
```

Or install the package directly:

```bash
pipx install ./cli                    # or: pip install ./cli  inside a venv/conda env
```

Either way you get two equivalent commands on your PATH: **`sr`** (short) and **`sushiruntime`** (long). Toolchain paths are read from `cli/config.toml` (committed, shared defaults). Machine-specific absolute paths (oneAPI/VS roots, vcpkg, compiler locations) go in a gitignored `cli/config.local.toml` using the same `[tool.<platform>]` section layout — copy the commented Windows block from `config.toml` as a starting point.

Then build:

```bash
sr project build                  # release (default)
sr project build --type asan      # isolated build-asan tree, CUDA off
sr project build --distributed    # enable the master-worker layer
```

Add `--clean` to wipe the build tree first. To uninstall: `pipx uninstall sushiruntime-cli`.

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

To build the optional distributed master-worker layer, configure with `-DSUSHI_ENABLE_DISTRIBUTED=ON`, or pass `--distributed` to the CLI (`sr project build --distributed`). It is **OFF by default**; when off, none of the distributed code or its sources are compiled, so the standalone build is unaffected. See [Distributed execution](#distributed-execution-optional).

### Docker

A `Dockerfile` based on `nvidia/cuda` (Ubuntu 22.04) with a pre-built **intel/llvm** nightly SYCL toolchain is provided, so you can build and run — including the CUDA backend — without installing a toolchain locally. It also installs the Intel OpenCL CPU runtime so the host CPU shows up as a SYCL device. For GPU passthrough you need the NVIDIA Container Toolkit.

```bash
sr docker build
sr docker run            # add --admin for --privileged (profiling)
```

## Running the tests

The test suite is split into two binaries:

- `sr_functional_tests` — fast, deterministic unit, regression, and integration tests. Run these on every change.
- `sr_benchmark_tests` — heavier stress and performance tests (large task graphs, allocator latency probes). Built by default; the CI fast lane turns it off with `-DSUSHI_BUILD_BENCHMARKS=OFF`.

Run them through the CLI or with `ctest`:

```bash
# via CLI
sr project test --suite functional
sr project test --suite benchmark
sr project test --suite all
sr project test --suite functional --filter 'Steal.*'   # ctest -R over test names

# or run a binary directly (default target from config)
sr project run sr_functional_tests

# via ctest, by label
ctest --test-dir build -L functional
ctest --test-dir build -L benchmark
```

You can forward GoogleTest flags to a binary after `--`, e.g. `sr project run sr_benchmark_tests -- --gtest_filter='AllocatorBench.*'`.

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

### Distributed (`distributed/`, optional — `SUSHI_ENABLE_DISTRIBUTED`)
An opt-in master-worker layer that offloads individual tasks to remote workers. It is guarded so a standalone build compiles none of it.
- The key idea: **compiled code can't travel the wire**, so nothing is serialized except references. A task ships its `OpID` (a kernel already compiled into every node) plus `GlobalBufferID`+version handles; each worker resolves those to its own local USM. Closures and pointers never cross the network.
- `ITransport` abstracts the wire: `InProcessTransport` runs the full multi-node protocol on a single machine (the network analogue of `RemappingTopologyProvider`), and `TcpTransport` is a dependency-free raw-socket transport (POSIX + Winsock) for a real cluster.
- `DistributedCoordinator` (master) decides what to offload via a swappable `IOffloadPolicy`, serves a stateful, pull-based, master-authoritative buffer cache (a write auto-bumps a version, so stale worker caches re-pull), and releases a node's successors when its remote result arrives. `WorkerAgent` (worker) is a thin network shell around a normal local `RuntimeContext`, so the existing NUMA-aware scheduler runs the offloaded work.
- Integration with the core is minimal and guarded: `TaskGraph::compile()` stamps which nodes are offloadable, and the scheduler's hot path adds a single `[[unlikely]]` branch — non-offloaded tasks pay nothing. An offloadable task whose policy declines falls back to local execution transparently.

See [Distributed execution](#distributed-execution-optional) for how to run it, and `Idea.md` for the feature tracker.

## Project layout

```
include/SushiRuntime/   Public headers (this is the API surface)
  core/                 Ref counting, smart pointer, logger, constants
  topology/             Hardware discovery, NUMA mapping, thread affinity, injectable topology/distance providers
  memory/               USM allocator, memory pool, epoch reclamation
  graph/                Task graph, nodes, dependency tracker, task types
  scheduler/            Work-stealing scheduler, tasks, event polling, rebalancer
  execution/            Runtime context, execution engine
  distributed/          Optional master-worker offload (protocol, transports, registries, coordinator, worker agent)
src/                    Implementations of the above
examples/               Sample apps (distributed master/worker demo; distributed builds only)
tests/
  common/               Shared GoogleTest entry point and helpers
  functional/           unit / regression / integration (fast, CI)
  benchmark/            stress / performance (heavy, nightly)
cli/                    Python `sr` / `sushiruntime` CLI package (Typer + rich)
.github/workflows/      CI: functional tests per push, benchmarks nightly
docs/                   Changelog, TODO, and guides
Dockerfile              CUDA + intel/llvm SYCL build environment
Idea.md                 Distributed master-worker feature tracker (status + remaining work)
```

## Distributed execution (optional)

A master-worker offload layer lets the master ship individual tasks to remote workers, without MPI. It is **off by default** (`-DSUSHI_ENABLE_DISTRIBUTED=ON` / `sr project build --distributed` turns it on) and uses a raw-socket TCP transport — no extra third-party dependency. See the distributed subsection under [Architecture](#architecture) for the design, and `Idea.md` for the feature tracker and remaining hardening work.

The same binary runs in either role; the `sr` CLI forwards `--distributed=master|worker` to your application's `main()`, which constructs the `RuntimeContext` accordingly. A runnable demo (`examples/distributed_demo.cpp` → `sr_distributed_demo`) offloads a vector-add across one or more workers:

```bash
sr project build --distributed

# Two processes on one host (worker retries until the master is up):
sr project run sr_distributed_demo --distributed=worker --master=127.0.0.1:5555 &
sr project run sr_distributed_demo --distributed=master --port=5555 --workers=1

# Across machines: run the master on one box and point each worker's
# --master at the master's IP.
```

The distributed tests (protocol, transports, buffer coherence, and SYCL end-to-end offload including multi-worker fan-out and TCP loopback) are part of the functional suite when the layer is enabled:

```bash
sr project run sr_functional_tests --gtest_filter='Distributed*'
```

## Roadmap

The core distributed layer is implemented and verified; remaining work is production hardening (worker-death resilience, reconnect, pipelining, and running offloaded work through the worker's own NUMA-aware scheduler). `Idea.md` tracks the full status and the prioritized remaining items.

## License

This software is the proprietary information of Mustafa Garip & Sushi Systems. All rights reserved. Unauthorized copying, disclosure, or use of this code is prohibited without explicit written permission from the copyright holders.
