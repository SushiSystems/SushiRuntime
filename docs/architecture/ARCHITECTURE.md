# SushiRuntime architecture

This page is the map across the modules: which modules exist, which of them a consumer sees, how
a failure travels, how a recorded rule becomes a finished step, and how the build is
configured. A fact about one module is in that module's `README.md`, and this page links to it.

To use the library, read [the introduction](../getting_started/INTRODUCTION.md) first.

## 1. What SushiRuntime is

SushiRuntime is a C++17 library that runs a graph of tasks on the CPUs and GPUs of one machine
through SYCL. A caller records each task with the memory it reads and writes. The runtime
derives the order from those declarations and runs what does not conflict in parallel.

It is an orchestration layer and computes nothing of its own. A reduction, a scan or a sort is
a node kind that a library above it records through the public `Recorder`.

## 2. Modules and tiers

The code is in modules under `modules/<tier>/<module>/`. A module includes lower tiers and the
same-tier modules that `cmake/SushiRuntimeTiers.cmake` lists, and the build refuses any other
include. Tiers, lowest first: `foundation`, `platform`, `engine`, `surface`, `extension`.

A module is public or internal.

- A **public** module's headers are installed, and its names are declared in `SushiRuntime`.
- An **internal** module is marked `INTERNAL` in its `CMakeLists.txt`. Its headers are not
  installed, a consumer cannot include them, and its names are in
  `SushiRuntime::Internal::<Module>`. The names of internal types on this page are for readers
  of the source.

| Tier | Module | Kind | Owns |
| --- | --- | --- | --- |
| foundation | [core](../../modules/foundation/core/README.md) | public | Assertions, `Error`, `Result`, `OperationID`, `span`, the intrusive pointer, the version |
| foundation | [report](../../modules/foundation/report/README.md) | public | `RunReport` and the records it carries |
| foundation | [region](../../modules/foundation/region/README.md) | public | `BufferRegion`, the byte window a node reads or writes, and the liveness token |
| foundation | [log](../../modules/foundation/log/README.md) | public | `LogLevel`, `set_log_level()`, `set_log_sink()` |
| foundation | [machine](../../modules/foundation/machine/README.md) | public | `DeviceFilter`, `PinPolicy`, `DeviceDescription`, `WorkerPool` |
| foundation | [logging](../../modules/foundation/logging/README.md) | internal | The logger and its macros |
| foundation | [sycl](../../modules/foundation/sycl/README.md) | internal | The differences between SYCL toolchains, and the translation of a SYCL exception to an `Error` |
| platform | [topology](../../modules/platform/topology/README.md) | internal | Hardware discovery, NUMA mapping, thread affinity |
| platform | [memory](../../modules/platform/memory/README.md) | internal | The USM allocator and registry, epoch-based reclamation |
| platform | [device](../../modules/platform/device/README.md) | internal | The device inventory and the SYCL queues of each device |
| platform | [observability](../../modules/platform/observability/README.md) | internal | The profiling ledger, event timestamps, transfer counters |
| engine | [task](../../modules/engine/task/README.md) | internal | The unit of work the scheduler dispatches, the graph node, the offload seam |
| engine | [scheduler](../../modules/engine/scheduler/README.md) | internal | The work-stealing scheduler, the event poller, the rebalancer |
| engine | [graph](../../modules/engine/graph/README.md) | internal | The task graph, the dependency tracker, the compiled plan |
| engine | [execution](../../modules/engine/execution/README.md) | internal | The runtime context, the execution engine, the in-order chain |
| surface | [interop](../../modules/surface/interop/README.md) | public | The descriptors exchanged with another graphics or compute API |
| surface | [memory_exchange](../../modules/surface/memory_exchange/README.md) | internal | Host export and device import of memory, behind `Interop` |
| surface | [api](../../modules/surface/api/README.md) | public | `Runtime`, `Buffer`, `State`, `Graph`, `DynamicGraph`, `Recorder`, the vocabulary, the umbrella header |
| extension | [distributed](../../modules/extension/distributed/README.md) | public, optional | The master and worker layer; built when `SR_ENABLE_DISTRIBUTED` is on |

`tools/layering/check_layering.py` checks the same rules outside the build, and
`sushiruntime_add_module` refuses an internal module among the public dependencies of a module
that is installed.

## 3. The public surface

A consumer includes one header, `<SushiRuntime/sushiruntime.hpp>`, and writes every name as
`SushiRuntime::<name>`. A program that attaches a cluster also includes
`<SushiRuntime/distributed/distributed.hpp>`, whose names are in `SushiRuntime::Distributed`.

The installed headers of `api` hold the templates that a caller's kernel instantiates. Whatever
names a type of the engine is compiled into the shared library. Three consequences:

- `Runtime`, `Graph`, `Buffer` and the other handles hold a `ContextHandle` and name no engine
  type. The surface has no call that returns the engine's context.
- `SushiRuntime::Detail` holds types that the installed templates need and a consumer does not
  write. A `Detail` type that crosses into the library is part of the binary interface: both
  sides read it by its layout.
- The headers and the library must come from one release. `Runtime::create()` checks this and
  stops the process on a mismatch.

[The api README](../../modules/surface/api/README.md) lists what crosses the compiled boundary.

## 4. How a failure travels

No exception leaves the library.

| Cause | Mechanism |
| --- | --- |
| Outside the program: no memory, a faulted kernel, a lost device | The call returns `Result<T>` holding an `Error{ErrorCode, message}` |
| A recording the recorder refuses | The recorder keeps the `Error`; `refusal()` reads it, and `compile()` and every run verb return it |
| A rule the caller broke | `SR_VERIFY`, `SR_ASSERT` or `SR_FATAL` calls the assertion handler, which stops the process; `set_assertion_handler()` replaces it |

Inside the library, a SYCL call and a caller's hook or host task can still throw. One function,
`Detail::translate_exceptions` of `core/error_translation.hpp`, turns what was thrown into an
`Error`, and the internal `sycl` module adds the arm for a SYCL exception. A failure while the
plan is built is `CompileFailed`, a failure once a step may have started is `KernelFailed`, and
refused memory is `AllocationFailed` in both phases.

A late-bound count or window that does not fit its capacity cannot be reported from inside a
kernel. The kernel skips its work and writes a fault record, and the run returns `InvalidGraph`
after the step.

## 5. From a recorded rule to a finished step

### Record

Each verb of `Graph` or `Region` (`add`, `add_rule`, `add_coupled`, `add_dynamic`, `add_host`,
`add_task`, `add_untracked`, `extend`) is a template. It wraps the caller's kernel once, fills a
description of the node and hands it to the graph's `Recorder`. Nothing is built yet.

### Plan

The first run, or `compile()`, builds the plan. The library replays each description into the
engine's task graph (internal, `Internal::Graph`). The dependency tracker compares the byte
ranges the nodes declared and adds an edge for each read-after-write, write-after-read and
write-after-write pair that overlaps. Nodes whose ranges are disjoint get no edge.

A plan is rebuilt only after the graph changed. `compile_count()` counts the builds.

### Run

A runtime has one engine, so one run is in flight on it at a time. A plan runs on one of two
layers, and `RunReport::layer` names the one that ran.

- **The scheduler.** A pool of worker threads, each with its own deque, takes nodes whose
  predecessors have finished. An idle worker steals from another. An event poller watches the
  SYCL events in flight and releases the successors of each node that completes.
- **The chain.** The calling thread submits the nodes in the plan's order to an in-order queue
  per device, with no worker hand-off and no polling. `run(steps)` submits every step and waits
  once; `run_for()` and `run_until()` wait after each step.

The engine classifies each plan. It refuses the chain for a plan that holds native host work, a
node the offload coordinator acts on, a device value read at dispatch, a node with no USM
region, a region the runtime did not allocate, or regions of two devices. `run_async()` and a
`DynamicGraph` use the scheduler, and so does every graph of a runtime created with
`RuntimeConfig::force_scheduler`.
[The execution README](../../modules/engine/execution/README.md) has the classification and the
order of a run; [the scheduler README](../../modules/engine/scheduler/README.md) has the pool.

After each step every `State` advances: the field the step wrote becomes the field the next
step reads. A failed run of one step advances nothing;
[the graph README of the api](../../modules/surface/api/include/SushiRuntime/api/graph/README.md)
has the rule for a failed run of several steps.

### What changes between steps without a rebuild

A node recorded with `add_dynamic` carries controls the runtime refreshes before each step: how
many elements it covers, where it starts and whether it runs. A count or a start can also come
from a `Residency::Host` slot that a kernel of the same step wrote; the consuming kernel reads
it on the device, and the host does not read it mid-step.
[The vocabulary README](../../modules/surface/api/include/SushiRuntime/api/vocabulary/README.md)
has the rules.

### A graph whose set of work changes

A `DynamicGraph` groups work into regions under caller keys. Each region compiles to its own
sub-graph. Edges inside a region are built once; edges between regions that share bytes of a
buffer are kept in a separate layer that the composer maintains. Adding or dropping a region
rebuilds that region and the boundary edges of the allocations it shares. For a buffer two
regions share, the region with the lower key is the producer.
[The graph README of the api](../../modules/surface/api/include/SushiRuntime/api/graph/README.md)
has the costs of a commit.

### Memory

A `Buffer` or a `State` allocates USM through the runtime's allocator, which records the device
of each allocation. A node runs on the queue of the device that owns its regions.

| Residency | Memory | Host access |
| --- | --- | --- |
| `Residency::Shared` (default) | Shared USM | Direct |
| `Residency::Device` | Device USM | Through `read_range()` and `write_range()` |
| `Residency::Host` | Pinned host USM in the device's context | Direct, also while kernels run; the residency a device-written count or start requires |

[The data README](../../modules/surface/api/include/SushiRuntime/api/data/README.md) and
[the memory README](../../modules/platform/memory/README.md) hold the detail.

## 6. The distributed extension

The `distributed` module is compiled only with `SR_ENABLE_DISTRIBUTED`, which also defines
`SR_DISTRIBUTED` for consumers. No header of another module names it.

- A node that states an operation in `NodeOptions::operation` is a candidate for offload when a
  cluster is attached, a worker registered a kernel for that operation, and every buffer the
  node touches was shared with `Cluster::share()`.
- Only references cross the wire: the `OperationID`, the node's name and parameters, and the
  ids and versions of the buffers. A closure or a pointer does not.
- The engine knows one interface, the offload coordinator of the internal `task` module.
  `Distributed::Cluster::attach()` installs its one implementation. With none installed, a node
  that names an operation runs as any other node does.
- A declined or failed offload runs the node on the master.

[The distributed guide](../guides/DISTRIBUTED_GUIDE.md) says how to run it, and
[the module README](../../modules/extension/distributed/README.md) describes the protocol, the
handshake and the classes.

## 7. Build-time configuration

Builds in this repository go through `sr`; [the CLI guide](../guides/CLI_GUIDE.md) maps its
flags to these options.

| CMake option | Default | Effect |
| --- | --- | --- |
| `SR_SYCL_TOOLCHAIN` | `intel-llvm` | The SYCL toolchain: `intel-llvm`, `adaptivecpp` or `oneapi` |
| `SR_GPU_BACKEND` | `auto` | The GPU backend: `auto`, `cuda`, `rocm`, `intel`, `cpu` or `none` |
| `SR_CUDA_ARCH` | none | CUDA compute capabilities, one (`86`) or a list (`61;86`) |
| `SR_AMD_ARCH` | none | AMD GPU ISAs, one (`gfx1030`) or a list (`gfx1030;gfx1100`) |
| `SR_ENABLE_CUDA` | `OFF` | Legacy switch with the effect of `SR_GPU_BACKEND=cuda` |
| `SR_ENABLE_DISTRIBUTED` | `OFF` | Builds the `distributed` module |
| `SR_BUILD_TESTS` | `OFF` | Builds the test programs; `sr build` turns it on and `--no-test` leaves it off |
| `SR_BUILD_BENCHMARKS` | `ON` | Builds the stress and performance programs; read only with `SR_BUILD_TESTS` |
| `SR_BUILD_EXAMPLES` | `ON` at top level | Builds `applications/examples`; off when a parent project adds the tree |
| `SR_INSTALL` | `ON` | Adds the install rules and the package files |
| `SR_DETERMINISTIC_FP` | `ON` | Forbids fast-math and FMA contraction in the library and in every consumer target |
| `SR_NATIVE_ARCH` | `OFF` | Compiles for the build machine's instruction set |

### Compile policy

Four INTERFACE targets carry the compile settings. `cmake/BuildPolicy.cmake` composes them and
`sushiruntime_apply_build_policy(<target>)` applies them.

| Target | Carries | Reaches a consumer |
| --- | --- | --- |
| `sushiruntime_warnings` | `-Wall -Wextra -Werror -pedantic`, or `/W4 /permissive-` | No |
| `sushiruntime_perf` | `-ftls-model=initial-exec`; `-march=native` or `/arch:AVX2` under `SR_NATIVE_ARCH` | No |
| `sushiruntime_sanitizers` | ThreadSanitizer in Debug and RelWithDebInfo builds, outside Windows | No |
| `sushiruntime_deterministic_fp` | `-ffp-contract=off -fno-fast-math`, or `/fp:precise` | Yes, through the installed package |

The kernels are templates a consumer's translation units compile, so a replay is byte-equal
only when those units compile under the same floating-point rules. That is why the fourth
target is exported and the other three are not. `tests/package/CMakeLists.txt` fails its
configure step when a flag of the first three appears in the exported interface.

Under an MSVC-style driver that is not `cl.exe`, `/fp:precise` does not turn contraction off.
`cmake/Determinism.cmake` probes `-ffp-contract=off` and then `/Qfp-contract=off`, adds the one
the driver accepts and warns when it accepts neither.

The tests named `Integration_Determinism` in `tests/integration/test_determinism.cpp` check the
result.

### GPU backend

`SR_GPU_BACKEND` resolves in `cmake/gpu/` to one backend.

- `auto` looks for an installed toolkit and otherwise builds the CPU path.
- `cuda` and `rocm` build for the architectures in `SR_CUDA_ARCH` and `SR_AMD_ARCH`. Neither
  has a default. When one is not given, `cmake/gpu/Cuda.cmake` asks `nvidia-smi` and
  `cmake/gpu/Rocm.cmake` asks `rocm_agent_enumerator`.
- When the probe finds no device, a backend that `auto` selected falls back to the CPU path
  with a warning, and a backend the caller named fails the configure step.

## 8. Invariants

1. **Declarations are truthful.** The tracker knows what a rule declared. A write that was not
   declared is a race the runtime cannot see.
2. **Memory a graph touches comes from the runtime.** A node runs on the queue of the device
   that owns its regions, and the allocator is what records the owner.
3. **A handle outlives the graphs that name it.** A run after a recorded handle was freed
   returns `DanglingResource`.
4. **One run per runtime at a time.** A run entry during another run returns `InvalidState`.
5. **No public header includes an internal one.** `rule_public_headers` of
   `tools/layering/check_layering.py` reports a breach.
6. **The distributed module costs nothing when it is off.** No header of another module names
   it.
7. **A replay on the same binary and device is byte-equal.** The schedule does not decide which
   values a kernel combines, and the floating-point flags of §7 forbid reassociation and
   contraction. Equality across architectures is not claimed.

## 9. Directory map

```
modules/<tier>/<module>/   README.md, CMakeLists.txt, include/SushiRuntime/<module>/,
                           source/, tests/
cmake/                     Build functions; SushiRuntimeTiers.cmake states the tier order and
                           the edges allowed inside a tier
tests/                     Tests that cross modules: unit/, integration/, regression/,
                           allocation/, benchmark/, package/
applications/examples/     Example programs on the public API
cli/                       The sr command line interface
tools/                     Checkers; see tools/README.md
docs/                      The manual, indexed by docs/README.md
```
