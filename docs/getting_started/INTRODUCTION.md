# Getting started with SushiRuntime

SushiRuntime runs a graph of tasks on the CPUs and GPUs of one machine through SYCL. You
allocate typed buffers, record each unit of work with the buffers it reads and writes, and run
the graph. The runtime orders the work from those declarations: a task that reads what another
wrote runs after it, and tasks that touch disjoint memory run in parallel.

This page builds the library, walks through a first program and then covers each part of the
public interface once. Every code block is taken from a file the build compiles, named under the
block. The module READMEs hold the full contract of each call, and each section links to its
own.

## 1. Build

```bash
hub install-cli sushiruntime
sr setup
sr toolchain intel-llvm
sr build
sr test
```

`sr setup` installs what `cli/sushistack.deps.toml` declares: hwloc and a SYCL toolchain. `sr build` makes a release build in `build/`, and `sr test` runs the functional suite.
The root [README](../../README.md#requirements) lists the requirements and the script that
installs everything on an empty machine. [The CLI guide](../guides/CLI_GUIDE.md) lists every
command.

## 2. A first program

A 1-D heat diffusion over 1024 cells, run for 10000 steps.

```cpp
#include <cstdio>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Runs the diffusion stencil and prints its total heat; returns 0 on success. */
    int run_quickstart()
    {
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

        // Heat is conserved by this closed diffusion; report it as a smoke check.
        double total = 0.0;
        for (std::size_t i = 0; i < N; ++i)
            total += heat[i];

        std::printf("quickstart: %zu steps in %.2f ms; total heat = %.4f (compiles = %zu)\n",
                    STEPS, report.total_duration_ms, total, sim.compile_count());
        return 0;
    }
} // namespace

int main()
{
    return run_quickstart();
}
```

Source: `applications/examples/quickstart.cpp`

Run it with `sr run sr_quickstart`.

The program has four parts, and every program on the library has the same four.

1. **A runtime.** `Runtime::create()` lists the devices, builds a SYCL queue for each and starts
   the worker threads. Make one and keep it.
2. **Data.** `rt.state<float>(N)` allocates a field with two buffers. `rt.buffer<T>(n)` allocates
   one buffer for data that does not step.
3. **A graph.** `rt.graph()` returns an empty graph, and each `add` verb records one rule on it.
4. **A run.** `sim.run(STEPS)` compiles the graph on its first call and replays the compiled
   plan once per step. `compile_count()` stays at 1 until a rule is added.

One header, `<SushiRuntime/sushiruntime.hpp>`, declares every public name, and every public name
is in the namespace `SushiRuntime`.

To build a program of your own inside this checkout, put the source file in
`applications/examples/` and add one line to `applications/examples/CMakeLists.txt`:

```cmake
sushiruntime_add_example(sr_quickstart             quickstart.cpp)
```

Source: `applications/examples/CMakeLists.txt`

A program in another repository links the
installed package; see [the integration guide](../guides/INTEGRATION.md).

## 3. Results and errors

No call of the library throws. A call that can fail for a reason outside the program returns a
`Result<T>`, which holds either the value or an `Error` with an `ErrorCode` and a message.

`value()` returns the value and stops the process when the result holds an error. That suits a
program that cannot continue without the value, which is why the quickstart writes
`Runtime::create().value()`. A program that handles the failure tests the result first:

```cpp
        // run() returns the report or an Error and throws nothing.
        const auto first = graph.run(K_STEPS);
        if (!first)
        {
            print_failure("the first run", first.error());
            return 1;
        }
```

Source: `applications/examples/operating_path_demo.cpp`

```cpp
    /** @brief Returns what a caller does about a failed run, chosen by its code. */
    const char* remedy_for(ErrorCode code) noexcept
    {
        switch (code)
        {
            case ErrorCode::InvalidGraph:
                return "record a rule before running";
            case ErrorCode::DanglingResource:
                return "keep every buffer alive as long as the graph that names it";
            case ErrorCode::KernelFailed:
            case ErrorCode::DeviceLost:
                return "the device failed; stop, or build a new runtime";
            default:
                return "read the message";
        }
    }
```

Source: `applications/examples/operating_path_demo.cpp`

The library separates three kinds of failure.

| Kind | Example | What happens |
| --- | --- | --- |
| A cause outside the program | A device returns no memory; a kernel faults | The call returns an `Error` |
| A recording the graph refuses | A rule over a `State` that holds no memory | The graph keeps the `Error`; `refusal()` reads it and the next run returns it |
| A rule the caller broke | A device index the runtime does not have; `value()` on an error | The process stops, through `SR_VERIFY`, `SR_ASSERT` or `SR_FATAL` |

A host program that must not stop replaces the handler with `set_assertion_handler()`.
`SR_ASSERT` is compiled out when `NDEBUG` is defined; the other two are always checked.
[The core README](../../modules/foundation/core/README.md) has the full contract of `Result`,
`Error` and the three macros.

## 4. Data: Buffer and State

`Runtime::buffer<T>(count)` returns a `Buffer<T>`, one USM allocation. `Runtime::state<T>(count)`
returns a `State<T>`, which holds two: the field a step reads (`current()`) and the field it
writes (`next()`). The runtime swaps the two after each step. Both handles free their memory
when they are destroyed, and a graph that names a handle must not outlive it: a run after that
returns `DanglingResource`.

Both calls take a device, a residency and an alignment after the count. The default residency is
`Residency::Shared`, which the host indexes directly, as `heat[i]` does above.
`Residency::Device` keeps the memory on the device, and the host then reads and writes it
through range calls:

```cpp
        // A device-resident buffer is not host-addressable, so write_range() seeds it.
        auto hits = runtime.buffer<float>(K_CAPACITY, DeviceIndex{0}, Residency::Device).value();
        const std::vector<float> zeros(K_CAPACITY, 0.0f);
        hits.write_range({0, K_CAPACITY}, zeros).value();
```

Source: `applications/examples/operating_path_demo.cpp`

```cpp
        // read_range() copies one window through the owning queue and maps nothing else.
        const float in_both_runs = hits.read_range({0, 1}).value()[0];
```

Source: `applications/examples/operating_path_demo.cpp`

`DeviceIndex{0}` is the first device SYCL lists, which is a CPU on one machine and a GPU on
another. `rt.advanced().devices()` describes each index.
[The data README](../../modules/surface/api/include/SushiRuntime/api/data/README.md) covers the
range calls, the residencies and what a moved-from handle is.

## 5. Recording work

A graph has one verb per kind of node.

| Verb | Records |
| --- | --- |
| `add` | A kernel over buffers it names |
| `add_rule` | A rule that steps one `State` |
| `add_coupled` | A rule that steps one `State` and reads others |
| `add_dynamic` | A kernel whose size, start or enablement is read before each step |
| `add_host` | Host C++ that submits no device work |
| `add_task` | A body that receives the `sycl::queue` and returns the event that ends its work |
| `add_untracked` | A kernel that declares no data and is ordered against nothing |
| `extend` | The nodes a library records through the `Recorder` |

Every call reads in one order: modifier, launch, access, callable, options.
[The graph README](../../modules/surface/api/include/SushiRuntime/api/graph/README.md) has the
table of which launch works with which access and the contract of each verb.

### Buffers: `add` with `In`, `Out` and `InOut`

`In(buffer)`, `Out(buffer)` and `InOut(buffer)` register the access and pass the kernel the
element pointer, in the order they are written.

```cpp
        // Out(a) and Out(b) register the writes and pass the kernel the element pointers.
        g.add(Extent{N}, Out(a), Out(b),
            [](sycl::id<1> id, float* a, float* b)
            {
                const std::size_t i = id[0];
                a[i] = static_cast<float>(i);
                b[i] = static_cast<float>(2 * i);
            });

        // Reading A and B, which the fill task wrote, orders this task after it.
        g.add(Extent{N}, In(a), In(b), Out(c),
            [](sycl::id<1> id, const float* a, const float* b, float* c)
            {
                const std::size_t i = id[0];
                c[i] = a[i] + b[i];
            });

        g.run().value();
```

Source: `applications/examples/vecadd_demo.cpp`

A kernel that captures its own pointers names the data with `Reads(...)` and `Writes(...)`
and takes a count as its launch:

```cpp
        auto activate = [&](std::uint64_t k)
        {
            int* p = tile[k].data();
            world.region(k).add(CELL_N, Reads(), Writes(tile[k]),
                                [p](std::size_t i) { p[i] += 1; });
        };
```

Source: `applications/examples/streaming_world_demo.cpp`

The runtime compares byte ranges, so two tasks conflict only where their ranges overlap.
`buffer.region({offset, count})` names part of a buffer for `Reads` or `Writes`, and tasks over
disjoint parts of one buffer run in parallel. A whole handle overlaps every part of itself.

### One field: `add_rule`

`add_rule(state, kernel)` covers the whole field, as the quickstart does. A launch before the
state covers part of it. `Extent<N>` is an N-dimensional box with an origin, and the kernel
receives the global index:

```cpp
        // The extent covers the (W-2) x (H-2) interior and offsets the kernel's index to (1,1).
        Extent<2> interior{ sycl::range<2>(W - 2, H - 2), sycl::id<2>(1, 1) };
        g.add_rule(interior, temp, [](sycl::id<2> idx, const float* cur, float* next)
        {
            const std::size_t x = idx[0];
            const std::size_t y = idx[1];
            const std::size_t c = y * W + x;
            const float lap = cur[c - 1] + cur[c + 1] + cur[c - W] + cur[c + W]
                              - 4.0f * cur[c];
            next[c] = cur[c] + ALPHA * lap;
        });
```

Source: `applications/examples/stencil_demo.cpp`

The rule never writes the border, so the
program seeds both buffers of the `State` before the run.

### Several fields: `add_coupled`

`Coupled(out, in...)` names the field a rule steps and the fields it reads. Every read sees the
previous step, because each `State` advances after all the kernels of the step have run. The
result does not depend on the order in which the rules were added.

```cpp
        // v_next = v_cur + 1.
        sim.add_rule(v, [](std::size_t i, const int* vc, int* vn)
        {
            vn[i] = vc[i] + 1;
        });

        // v_cur is the previous step's v, not the v_next of the rule above; see README.md.
        sim.add_coupled(Coupled(u, v), [](std::size_t i, const int* uc, int* un, const int* vc)
        {
            un[i] = uc[i] + vc[i];
        });
```

Source: `applications/examples/coupled_fields_demo.cpp`

### A size that changes per step: `add_dynamic`

A `Dynamic` holds callables the runtime reads before each step, so the launch changes and the
graph is not recompiled. `sized()` gives the number of elements, `based_at()` the first element
and `when()` whether the node runs at all.

```cpp
        // sized() is polled before each step, so the launch width changes with no recompile.
        std::size_t live_count = K_CAPACITY / 4;
        float* cells = hits.data();
        auto graph = runtime.graph();
        graph.add_dynamic(Dynamic().sized([&live_count] { return live_count; }), K_CAPACITY, Reads(),
                          Writes(hits), [cells](std::size_t index) { cells[index] += 1.0f; })
            .name("count_hits");
```

Source: `applications/examples/operating_path_demo.cpp`

When a kernel of the same step produces the count, `sized_from_device(counter)` reads it after
that kernel has finished. The counter is a `Residency::Host` buffer:

```cpp
    // Producer: derive the count on the device from data the kernel reads.
    g.add(std::size_t{1}, Reads(wanted), Writes(counter),
          [c, w](std::size_t) { c[0] = w[0] * 3u; });

    // Consumer: iterate exactly that many elements.
    g.add_dynamic(sized_from_device(counter), CAPACITY, Reads(), Writes(marks),
                  [m](std::size_t i) { m[i] = static_cast<int>(i); });
```

Source: `modules/surface/api/tests/test_device_count.cpp`

[The vocabulary README](../../modules/surface/api/include/SushiRuntime/api/vocabulary/README.md)
covers `based_at_device()`, `LateCount` and what happens when a live count exceeds the capacity.

### Host work: `add_host` and `add_task`

`add_host(reads, writes, body)` runs `body` on a worker thread with no SYCL submission, after
the tasks it depends on. Use it for host C++ that shares data with kernels.

```cpp
    auto g = rt.graph();
    g.add_host(Reads{}, Writes(buf), [&released, &saw_release, p]()
    {
        saw_release.store(wait_for_gate(released), std::memory_order_release);
        p[0] = 42;
    });
```

Source: `modules/surface/api/tests/test_async_step.cpp`

`add_task(reads, writes, body)` hands the body the queue and the events it must run after. The
body submits what it needs and returns the event that ends its work. A library that calls
another library with a queue records its work this way.

### Work that shares nothing: `add_untracked`

`add_untracked(launch, kernel)` records a kernel with no declared data. The runtime orders it
against nothing and runs it on device 0 unless `On(...)` names another. It is wrong as soon as
another rule of the graph touches the same memory.

### Names and options

`name("...")` after a verb names the rule it recorded; the name appears in the timings of a
profiled run. Every verb also takes a `NodeOptions` as its last argument, which holds a width, a
name, an operation and up to eight parameters.

```cpp
        world.add(Extent{N}, In(px), InOut(alive),
            [xmax = XMAX](sycl::id<1> id, const float* px, int* alive)
            {
                const std::size_t i = id[0];
                if (alive[i] && px[i] > xmax) alive[i] = 0;
            }).name("cull");
```

Source: `applications/examples/game_engine_demo.cpp`

An operation is an `OperationID`, written with the `_op` literal. A node that names one may run
on another machine when a cluster is attached; see
[the distributed guide](../guides/DISTRIBUTED_GUIDE.md).

## 6. Running

| Call | Runs |
| --- | --- |
| `run()`, `run(steps)` | One step, or a fixed number |
| `run_for(budget)` | Steps until a wall-clock budget elapses; at least one |
| `run_until(done)` | Steps until `done()` returns true |
| `run_async()` | One step, without waiting; returns a `RunHandle` |

Each of the first three has a form that takes a `RunReport&` last and fills it in place, for a
loop that should not allocate a report per tick. That form returns a pointer to the report.

`run_async()` starts a step on the worker pool and returns. `wait()` on the handle ends the
step and returns its report; the `State`s advance there.

```cpp
    auto step = g.run_async().value();

    // The caller is still running: this is the work a frame would overlap.
    released.store(true, std::memory_order_release);

    const RunReport report = step.wait().value();
```

Source: `modules/surface/api/tests/test_async_step.cpp`

A runtime runs one graph at a time. A run entry called while another run is in flight on the
same runtime returns `InvalidState`.

`before_each_step(hook)` adds a callable the graph calls before each step, on the thread that
called the run. It is where a program writes a value the next step reads.

```cpp
    std::vector<std::uint32_t> observed;
    g.before_each_step([w](std::size_t finished_steps)
     {
         w[0] = static_cast<std::uint32_t>(finished_steps);
         w[1] = K_BASE + static_cast<std::uint32_t>(finished_steps);
     })
     .before_each_step([w, &observed](std::size_t) { observed.push_back(w[1]); });

    g.run(K_STEPS).value();
```

Source: `modules/surface/api/tests/test_run_driver.cpp`

`compile()` builds the plan and runs nothing. `Graph::plan()` returns a `PlanView` of the nodes
in recording order. `cancel()` stops the run in flight of the graph it is called on, from any
thread. `Graph::clear()` removes every rule and keeps the graph.

## 7. A graph whose work changes: DynamicGraph

`rt.dynamic_graph()` returns a graph whose work is grouped into regions, each under a key you
choose. `region(key)` records with the same verbs as a `Graph`, and `drop(key)` removes a region.
A change takes effect at the next step, and only the regions that changed are rebuilt. Regions
that share a buffer are ordered across the boundary.

```cpp
        // Window 1: tiles {0,1,2,3} in view.
        for (std::uint64_t k = 0; k < 4; ++k) activate(k);
        world.run(STEPS).value();

        // Tiles 0 and 1 stream out, 4 and 5 in; tiles 2 and 3 are not rebuilt.
        world.drop(0);
        world.drop(1);
        activate(4);
        activate(5);
        world.run(STEPS).value();
```

Source: `applications/examples/streaming_world_demo.cpp`

## 8. Configuring the runtime

`Runtime::create()` takes one `RuntimeConfig`.

```cpp
        // Two floating workers leave the other cores to the program that hosts the runtime.
        RuntimeConfig config;
        config.worker_count = 2;
        config.pinning = PinPolicy::Float;
        auto runtime = Runtime::create(config).value();

        // advanced() answers what was built: the devices by index and the worker pool.
        const AdvancedControls built = runtime.advanced();
        for (const auto& device : built.devices())
            std::printf("  device %zu: %s\n", device.index, device.name.c_str());
        std::printf("  workers: %zu\n", built.worker_pool().count);
```

Source: `applications/examples/operating_path_demo.cpp`

| Field | Default | Meaning |
| --- | --- | --- |
| `devices` | Every device | A `DeviceFilter`: which devices the runtime drives |
| `worker_count` | 0 | Worker threads; 0 is one per logical core |
| `first_core` | 0 | First core of the range the workers pin into |
| `pinning` | `PinPolicy::Pin` | Whether workers bind to cores |
| `profiling` | `false` | Builds queues that record device timing |
| `rebalancer` | `false` | Starts the background thermal rebalancer |
| `force_scheduler` | `false` | Runs every graph on the work-stealing path |

A core range that does not fit the machine is clamped, and `advanced().worker_pool()` reports
the pool as built. [The runtime README](../../modules/surface/api/include/SushiRuntime/api/runtime/README.md)
describes each field and the calls of `AdvancedControls`.

Logging is process state: `set_log_level()` and `set_log_sink()` set it, and the environment
variable `SR_LOG_LEVEL` sets the level at startup.

## 9. Reading a run

A run returns a `RunReport`. `total_tasks_executed`, `total_duration_ms` and `layer` are always
filled. `node_timings` and `worker_timings` are filled when the runtime was created with
`config.profiling = true`. `node_events`, one record per dispatch, is filled for a graph that
called `set_profiling(true)`.

```cpp
    EXPECT_EQ(rep.layer, ExecutionLayer::Scheduler);
    ASSERT_FALSE(rep.worker_timings.empty());
    EXPECT_GE(rep.scheduling_overhead_ms, 0.0);

    // At least some time must be attributed somewhere across the workers.
    double total = 0.0;
    for (const auto& w : rep.worker_timings)
        total += w.busy_ms + w.stealing_ms + w.polling_ms + w.idle_ms;
```

Source: `tests/integration/test_observability.cpp`

A node without a name reports as `unnamed_task`.
[The report README](../../modules/foundation/report/README.md) lists the fields.

## 10. Determinism

A fixed graph gives the same result however its work was spread over the workers. That holds
for your kernels only while they compile with the floating-point flags the library was built
with, so the exported target carries those flags to every target that links it.
[The integration guide](../guides/INTEGRATION.md#4-what-you-inherit-and-what-you-supply) names
them.

## 11. Where to go next

- [ARCHITECTURE.md](../architecture/ARCHITECTURE.md): the modules, the two execution layers and
  the build options.
- [INTEGRATION.md](../guides/INTEGRATION.md): linking the installed package, and writing the one
  adapter module through which your code reaches the library.
- [CLI_GUIDE.md](../guides/CLI_GUIDE.md): every `sr` command.
- [applications/examples/README.md](../../applications/examples/README.md): what each example
  shows.
- `modules/surface/api/tests/`: one test file per part of the interface.
  `test_fluent_lifecycle.cpp` covers handles, stepping and coupling.
- `sr docs` builds the API reference from the headers.
