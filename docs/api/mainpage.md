# SushiRuntime Fluent API Reference {#mainpage}

SushiRuntime is an **orchestration runtime**: it decides *when, where, and in what
order* work runs across CPUs and GPUs, while your code decides *what* is computed.
This site is the reference for its one public surface — the **C++ fluent API** in
`include/SushiRuntime/api`. Everything you program against lives in namespace
`SushiRuntime::API`.

For the architecture beneath this layer (the scheduler, the lock-free graph, the
memory model) see `docs/guides/ARCHITECTURE.md`. For a guided tour with full
programs see `docs/guides/INTRODUCTION.md`.

## The model in one minute

The runtime follows a **describe → compile once → replay** pipeline:

1. **Allocate data as handles.** `SushiRuntime::API::Buffer` is a typed USM block;
   `SushiRuntime::API::State` is a double-buffered field for stepped updates.
   Choose `SushiRuntime::API::Residency::Device` to keep a large field resident on
   the GPU across steps.
2. **Describe work** with `SushiRuntime::API::Graph::add()`. Name the data each
   task reads and writes with `Reads(...)` / `Writes(...)`; the runtime derives the
   dependency order automatically — no manual edges.
3. **Run** with `SushiRuntime::API::Graph::run()` / `run(n)` / `run_for()` /
   `run_until()`. The graph compiles on the first run and the compiled plan is
   replayed for every step.

```cpp
#include <SushiRuntime/SushiRuntime.h>
using namespace SushiRuntime;

auto rt    = Runtime::create();
auto field = rt.state<float>(N);                 // double-buffered field
// ... seed field ...

auto sim = rt.graph();
sim.add(field, [](std::size_t i, const float* cur, float* next)
{
    next[i] = /* stencil over cur */;            // read current, write next
});

auto report = sim.run(10'000);                   // compile once, replay 10k steps
```

## Where to look

| You want to… | Start at |
|---|---|
| Allocate data | `SushiRuntime::API::Buffer`, `SushiRuntime::API::State` |
| Build and run work | `SushiRuntime::API::Graph`, `SushiRuntime::API::Runtime` |
| Vary size / enablement per step without recompiling | `SushiRuntime::API::Dynamic`, `when()`, `sized()` |
| Run plain CPU work in the graph | `SushiRuntime::API::Graph::add_host()` |
| Keep a field on the device | `SushiRuntime::API::Residency`, `Buffer::read_range` / `write_range` |
| Tune for real-time / determinism | `SushiRuntime::API::Runtime::rebalancer()` |
| Profile per-node / per-worker timing | `SushiRuntime::API::RuntimeConfig`, `SushiRuntime::Core::NodeTiming`, `WorkerTiming` |
| Read run results | `SushiRuntime::Core::RunReport` |

Browse the full list under **Classes** and **Namespaces** in the sidebar; every
public type and method carries its signature, parameters, and intent.
