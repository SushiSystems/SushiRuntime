# Getting started with SushiRuntime

SushiRuntime is a C++17 runtime library for **task-based parallel computing on
heterogeneous hardware**. You describe your work as a graph of tasks, declare
which memory each task reads and writes, and the runtime works out a safe order
and runs the independent parts in parallel across your CPU and GPU using SYCL.

The big idea: **you never wire up task dependencies by hand.** You just say
"this task reads buffer A and writes buffer B." The runtime detects the hazards
between tasks (read-after-write, write-after-read, write-after-write) and
serializes only what must be serialized — everything else runs concurrently.

This guide takes you from an empty machine to a working first program, then
gives you a tour of the core API. Every code sample here uses the real, current
API and mirrors what the project's own integration tests do.

> For the full command reference, see [CLI_GUIDE.md](../guides/CLI_GUIDE.md). For the
> architecture and design, see the top-level [README](../../README.md).

---

## 1. Quick start

### Install and build in one command

On a fresh machine you can go from nothing to a built project with a single
script. [SushiStack](https://sushisystems.io) owns dependency provisioning for
the whole stack: its bootstrap script installs Python/Git, installs the `hub`
CLI, then runs `hub install` to fetch every toolchain and library the workspace
needs (SushiRuntime declares its own in `cli/sushistack.deps.toml`).

**Linux (Debian/Ubuntu):**

```bash
curl -fsSL https://sushisystems.io/install.sh | bash
```

**Windows (PowerShell):**

```powershell
irm https://sushisystems.io/install.ps1 | iex
```

### Or, if you already have a SYCL toolchain

Install the CLI and build directly:

```bash
hub install-cli sushiruntime  # puts `sr` and `sushiruntime` on your PATH
sr toolchain intel-llvm      # pick a SYCL toolchain (the default; or adaptivecpp / oneapi)
sr build             # Release build (the default)
sr test              # run the fast functional test suite
```

You'll need a SYCL 2020 compiler — intel-llvm (`clang++ -fsycl`, primary),
AdaptiveCpp (`acpp`), or Intel oneAPI DPC++ — plus CMake 3.20+, hwloc, and
GoogleTest. Pick the toolchain with `sr toolchain`. The CLI reads your toolchain
paths from `cli/config.local.toml`; `sr setup` generates that file for you, and
`hub install` does the same for every checkout in a workspace.
See the [README](../../README.md) for the full requirements list and the CMake
options if you want to skip the CLI.

Verify everything works:

```bash
sr test --suite functional
```

If the functional suite passes, you're ready to write code.

---

## 2. The quick path — the fluent API

The `api/` layer is the intended entry point. You allocate data as typed
handles, describe each unit of work as plain C++ over those handles, and run the
graph. The runtime tracks the reads and writes, places the work on a device, and
— for stepped workloads — compiles the graph once and replays it. You never
touch a `sycl::handler` or wire up a dependency by hand.

Here is a complete program: a 1-D heat-diffusion step run for many timesteps.

```cpp
#include <cstdio>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

int main()
{
    auto rt = Runtime::create();

    constexpr std::size_t N = 1024;

    // A time-evolving field. The runtime keeps two buffers and swaps them each
    // step, so a rule reads the current field and writes the next.
    auto heat = rt.state<float>(N);
    for (std::size_t i = 0; i < N; ++i) heat[i] = (i == N / 2) ? 1.0f : 0.0f;

    auto sim = rt.graph();

    // One update rule. fn gets the index plus the current and next buffers;
    // the runtime supplies the pointers and tracks the read/write for you.
    sim.add(heat, [N](std::size_t i, const float* cur, float* next)
    {
        const float left  = (i == 0)     ? cur[i] : cur[i - 1];
        const float right = (i == N - 1) ? cur[i] : cur[i + 1];
        next[i] = cur[i] + 0.25f * (left - 2.0f * cur[i] + right);
    });

    // Compile once, replay 100000 steps. The worker pool, NUMA placement,
    // device dispatch, and buffer swapping are all handled for you.
    auto report = sim.run(100000);

    std::printf("ran %zu task-steps in %.1f ms\n",
                report.total_tasks_executed, report.total_duration_ms);
    // heat.current() now holds the final field.
    return 0;
}
```

`run()` is a single step; `run(n)` replays `n` steps; `run_for(duration)` and
`run_until(predicate)` bound a run by wall-clock time or a condition. `try_run()`
and `try_run(n)` are the non-throwing forms, returning `Result<RunReport, Error>`
instead of throwing `SushiException`. The graph is compiled the first time it
runs and after any change to it, then replayed without recompiling.

Every one of those has a form that writes into a report you already own —
`run(report)`, `run(n, report)`, `try_run(report)` — for a loop that runs
thousands of times and would rather not allocate a report on each one:

```cpp
Core::RunReport report;
while (running)
    sim.run(report);               // refilled in place, no per-tick allocation
```

The report is cleared before each run, so nothing from the previous tick
survives into it, but the storage it already holds is kept.

**Overlapping a step with other work.** `run()` blocks for the whole step, which
is right for a simulation thread with nothing else to do — but a frame usually
does have something else: extracting the previous step's state for a renderer or
an audio system, which needs no worker threads at all. `run_async()` starts the
step and returns an `API::RunHandle` instead:

```cpp
API::RunHandle<API::Graph> step = sim.run_async();
extract_previous_frame();          // overlaps with the step
Core::RunReport report = step.wait();
```

`wait()` is where the step is finished: every State advances there, so an
asynchronous step ends in exactly the same place a synchronous one would.
`wait(report)` settles into a report you own, matching `run(report)`. The
handle is move-only and waits in its destructor if you drop it, because the
compiled plan cannot be torn down while workers are still in it — which also
means a dropped handle drops the outcome, including a task exception. `ready()`
answers "would waiting return immediately" without blocking, and `try_wait()` is
the non-throwing form. One step may be in flight at a time; starting another
while a handle is pending is refused rather than silently interleaved.

For data that does not evolve step to step, use `rt.buffer<T>(n)` instead of
`rt.state<T>(n)`. Both are owning handles to shared USM; both register their
reads and writes automatically when passed to `add()`.

**Everything spelled `add()` is ordered by the data it names.** A bare kernel is
not: a per-element callable is `void(std::size_t)` capturing raw USM pointers, so
nothing in its type says what it touches and the runtime cannot infer it. Those
shapes are spelled `add_untracked()` — they record no regions, so the task is
both a root and a leaf, ordered against nothing:

```cpp
g.add_untracked(N, [p](std::size_t i) { p[i] = 0; });   // ordered against nothing
g.add(API::Reads(src), API::Writes(dst), N,             // ordered by src and dst
      [ps, pd](std::size_t i) { pd[i] = ps[i]; });
```

`add_untracked` is the right choice for genuinely self-contained work and the
wrong one the moment another task in the same graph shares data with it. Every
launch shape has both forms, so reaching for the untracked one is a decision, not
an oversight.

**Coupled fields** read one another within a step. Every read sees the previous
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

**Late-bound tasks** vary their size or whether they run each step, without
recompiling the graph. Pass `API::when(predicate)` and/or `API::sized(count)` as
the leading argument to `add()`: `when()` skips the task on steps its predicate
is false (a sleeping subsystem), and `sized()` iterates only a live prefix of the
buffers' capacity (a changing number of active elements). The graph still
compiles once — `g.compile_count()` stays 1 across the whole run.

```cpp
std::size_t active = 0;     // updated by your code between steps
bool        enabled = true;

sim.add(API::when([&] { return enabled; }).and_sized([&] { return active; }),
        API::Reads(in), API::Writes(out), CAPACITY,
        [p = out.data()](std::size_t i) { /* runs only while enabled, over active */ });
```

`sized()` is polled on the host before each step, so it can only carry a count
your code already has. When the count is produced *by a kernel earlier in the same
step* — a broadphase writing how many collision pairs it found, and a narrowphase
that must iterate exactly that many — use `API::sized_from_device(counter)`
instead. The count is read when the task is dispatched, which is after its
predecessors have completed, so the value is the one this step just wrote:

```cpp
auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host);   // must be Host

g.add(API::Reads(world), API::Writes(counter), N,
      [c = counter.data()](std::size_t) { /* ... c[0] = pairs_found; */ });

g.add(API::sized_from_device(counter), API::Reads(), API::Writes(pairs), CAPACITY,
      [p = pairs.data()](std::size_t i) { /* runs exactly pairs_found times */ });
```

The counter is registered as a read of the consuming task, so you do not have to
name it in `Reads(...)` to get the ordering — forgetting it is not possible. Size
the task's buffers for the worst case: a count above that capacity fails the run
with a message naming both numbers rather than quietly dropping the surplus.

`sized()` says *how many* elements a task iterates; `API::based_at(provider)`
says *from where*. Together they name a moving window into one buffer, which is
what a graph-coloured solver wants — each colour is a contiguous slice whose
offset shifts whenever the constraint set changes:

```cpp
g.add(API::based_at([&] { return colour_offset; })
          .and_sized([&] { return colour_size; }),
      API::Reads(), API::Writes(constraints), CAPACITY,
      [c = constraints.data()](std::size_t i) { /* i is the ABSOLUTE index */ });
```

The runtime applies the offset, so your kernel still receives absolute indices
and does not need to know a base exists. `API::based_at_device(slot)` takes the
offset from a slot a kernel wrote, exactly as `sized_from_device` does for the
count. The window has to fit the capacity — one that runs off the end fails the
run rather than reading past it.

**Dynamic region graphs** let the *set* of work change between steps, for
streaming open worlds where cells appear and disappear. `rt.dynamic_graph()`
returns a graph partitioned into **regions** keyed by an id of your choosing (a
packed cell coordinate, a chunk id). Record work on a region with the same `add()`
surface, and drop a region when it streams out — mutations take effect at the next
step boundary, and only the regions that changed are rebuilt:

```cpp
auto world = rt.dynamic_graph();
world.region(cell_id).add(API::Reads(terrain), API::Writes(height), N,
                          [p = height.data()](std::size_t i) { /* ... */ });
world.run(steps);            // composes the live regions into one plan, then replays

world.region(new_cell).add(/* ... */);   // streams a cell in
world.drop(old_cell);                     // streams a cell out
world.run(steps);            // recomposes once; unchanged cells are not rebuilt
```

Regions that share a buffer are ordered automatically across the boundary, exactly
as tasks within one graph are. `world.compile_count()` stays 1 across an unmutated
run and rises by one per mutation, not per region.

**Reductions with a fixed combination order** give a sum, minimum or maximum
whose bit pattern is reproducible. An ordinary parallel reduce combines values in
whatever order the schedule happened to produce, so its floating-point result can
differ between two runs of the same input — fine for a diagnostic, fatal for a
simulation whose replay must reproduce state exactly. `add_reduce` folds fixed
256-element tiles left to right and repeats over the partials, so which values
meet which depends only on the element count:

```cpp
auto total = rt.buffer<float>(1);
g.add_reduce(values, total, N, Sum<float>{});   // also Minimum<T>, Maximum<T>
```

`add_segmented_reduce` gives the same guarantee over a ragged array — per-body,
per-cell, per-vertex accumulation — using the usual CSR boundary array of
`count + 1` offsets, where segment `s` covers `values[offsets[s] .. offsets[s+1])`:

```cpp
g.add_segmented_reduce(contributions, offsets, per_body, body_count, Sum<float>{});
```

Both are ordinary tracked tasks, so a consumer that reads the output is ordered
after the whole reduction. Pass an explicit identity as the last argument when
the combiner is a lambda rather than one of the three built-ins.

**Native CPU tasks** run plain host C++ — gameplay logic, AI, navigation — with
no SYCL submission, ordered into the graph by the data they touch. Use
`add_host()` and name the data with `Reads(...)`/`Writes(...)`:

```cpp
sim.add_host(API::Reads(world), API::Writes(decisions),
             [&] { /* arbitrary C++; runs on a worker thread, no kernel launch */ });
```

The task is dispatched only after its predecessors complete, so the shared USM it
reads is already up to date; its successors then run once it returns. Do not
submit SYCL work from an `add_host` body — use a normal `add()` for that.

**Naming a node** makes its timings readable. Chain `name_last()` onto any
`add()`; without a name the node reports as `"unnamed_task"`, which is fine until
a report has forty rows and you need to know which is which:

```cpp
sim.add_host(API::Reads(world), API::Writes(decisions),
             [&] { /* ... */ }).name_last("ai_decide");
```

It is a separate call rather than a last argument because two of the launch
shapes end in a parameter pack whose last element is the callable, so a trailing
name would not reach them — one spelling works for every shape instead.

The name is what `RunReport::node_timings[i].name` carries, so it is worth
setting on anything you intend to profile. It is a `const char*` stored as-is —
pass a string literal or something that outlives the graph.

**Device-resident data** keeps a large field on the GPU across steps instead of
in host-addressable shared USM. Pass `Residency::Device`; the host then seeds and
reads it through `write_range`/`read_range` (it is no longer directly indexable):

```cpp
auto field = rt.state<float>(N, DeviceIndex{0}, Residency::Device);  // stays on the device
std::vector<float> seed(N, 0.0f);
field.write_range({0, N}, seed.data());                  // seed from the host
sim.run(10000);
auto window = field.read_range({0, 256});                // pull a window back
```

**Sharing the machine.** `Runtime::create()` takes an optional `RuntimeConfig`.
Its defaults reproduce the sole-tenant behaviour — a worker per logical core,
worker *i* pinned to core *i* — which is right for a batch process and wrong
inside an application that also has a main thread, a render thread and an audio
callback. Reserve cores for those by naming the range the runtime may use:

```cpp
API::RuntimeConfig config;
config.worker_count = 8;                       // 8 workers, not one per core
config.first_core   = 4;                       // on cores 4..11; 0..3 stay yours
config.rebalancer   = false;                   // the default; see below
config.profiling    = false;                   // the default

auto rt = Runtime::create({}, config);
```

Verify it took effect with `rt.advanced().worker_pool()`, which reports the pool
as built. A range that does not fit the machine is clamped and logged rather than
obeyed, because the OS affinity calls accept an out-of-range mask and simply do
not bind — an unclamped request would look like a reservation that never
happened. `config.pinning = API::PinPolicy::Float` turns binding off entirely,
which is the better choice inside a container whose CPU allowance is enforced by
quota rather than by affinity.

`config.rebalancer` is **off by default**. It runs a background thread on a 5 ms
heartbeat that migrates tasks between workers; that trades run-to-run
reproducibility and the jitter floor for throughput on long batch work, which is
a bad trade for a simulation frame and a good one for a batch job. Turn it on
when you are the batch job.

**Reading a run.** Every run form returns a `Core::RunReport`. Without profiling
it carries the cheap totals — `total_tasks_executed`, `total_duration_ms` — which
cost nothing to maintain. `config.profiling = true` is what fills in the rest,
and it is a *construction-time* switch: the runtime builds its queues with SYCL
profiling enabled, so it cannot be turned on later and costs nothing when off.

```cpp
API::RuntimeConfig config;
config.profiling = true;
auto rt = Runtime::create({}, config);
// ... build and run a graph ...
for (const auto& node : report.node_timings)
    std::printf("%-24s %8.3f ms over %zu runs\n",
                node.name.c_str(), node.device_ms, node.invocations);
```

`node_timings` is one row per graph node, keyed by the name you gave it — this is
what `name_last()` is for. `worker_timings` splits each
worker's time into busy, stealing, polling and idle, which is how you tell a graph
that is too small to fill the pool from one that is genuinely saturated.

**Determinism is a contract with two sides.** The runtime guarantees that a fixed
graph topology produces schedule-independent results: the same inputs give the
same outputs regardless of how the work happened to be distributed across workers.
That holds only while the floating-point semantics stay put, so the runtime
compiles with contraction and fast-math off and exports that setting to you
through the `sushiruntime_deterministic_fp` target. If your own translation units
build with `-ffast-math` or `/fp:fast`, kernels you write are outside the
guarantee even though the runtime's scheduling is inside it. See
[INTEGRATION.md](../guides/INTEGRATION.md) for the exact flags.

The fluent API is enough for most programs. The rest of this guide documents the
lower-level task-graph API it is built on, for when you need full control over
task bodies, host/device fallback, or explicit dependency lists.

---

## 3. The lower-level mental model

A program written against the lower-level API has four moving parts:

1. **`RuntimeContext`** — the top-level object. When you create it, it discovers
   your hardware (CPUs, GPUs, NUMA nodes), builds a SYCL queue per device, sets
   up memory allocators, and starts a pool of worker threads. You make one and
   keep it for the life of the program.

2. **Memory** — you allocate Unified Shared Memory (USM) that both the CPU and
   the GPU can see. Allocate it *through the context* so the runtime knows which
   device owns it.

3. **`TaskGraph`** — you add tasks to it. Each task is a piece of work plus two
   lists: the pointers it **reads** and the pointers it **writes**. That's how
   the runtime infers ordering.

4. **Compile and execute** — you call `graph.compile()` to turn your tasks into a
   runnable DAG, then hand it to the execution engine, which runs it and blocks
   until everything is done.

The lifecycle is always: **build the whole graph → compile → execute.** Graph
construction and `compile()` are *not* thread-safe relative to each other, so
finish adding every task before you compile.

---

## 4. Your first program

Here is a complete program: it adds two vectors, `C = A + B`. It uses one host
task to fill the input arrays and one device kernel to do the addition. The
runtime sees that the kernel reads `A` and `B` (which the init task wrote) and
automatically runs the init task first.

```cpp
#include <vector>
#include <iostream>
#include <sycl/sycl.hpp>

#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/graph/task_types.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Graph;

int main()
{
    // 1. Create the runtime. This discovers hardware and starts the workers.
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    constexpr size_t N = 1024;

    // 2. Allocate USM *through the context* so the scheduler knows which
    //    device/queue owns this memory and dispatches the kernel there.
    float* A = ctx.malloc_shared<float>(N);
    float* B = ctx.malloc_shared<float>(N);
    float* C = ctx.malloc_shared<float>(N);

    // 3. First task: a host task that fills A and B and zeroes C.
    //    It WRITES A, B, C and reads nothing.
    TaskMetadata init_meta{"init"};
    graph.add_task(init_meta, /*reads*/ {}, /*writes*/ {A, B, C},
        [A, B, C](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
        {
            for (size_t i = 0; i < N; ++i)
            {
                A[i] = static_cast<float>(i);
                B[i] = static_cast<float>(i * 2);
                C[i] = 0.0f;
            }
            return {};   // host work returns an empty event
        });

    // 4. Second task: a SYCL kernel that computes C = A + B.
    //    It READS A and B, WRITES C. Because it reads what `init` wrote,
    //    the runtime orders it after `init` — you didn't have to say so.
    TaskMetadata add_meta{"vector_add"};
    graph.add_task(add_meta, /*reads*/ {A, B}, /*writes*/ {C},
        [A, B, C](sycl::queue& q, const std::vector<sycl::event>& deps) -> sycl::event
        {
            return q.submit([&](sycl::handler& h)
            {
                h.depends_on(deps);
                h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i)
                {
                    C[i] = A[i] + B[i];
                });
            });
        });

    // 5. Compile the graph and run it. execute() blocks until the graph finishes.
    auto compiled = graph.compile();
    ctx.get_execution_engine().execute(std::move(compiled));

    // 6. Results are ready. Check a couple of values.
    std::cout << "C[1]   = " << C[1]   << "  (expected 3)\n";
    std::cout << "C[100] = " << C[100] << "  (expected 300)\n";

    // 7. Free what you allocated through the context.
    ctx.free_usm(A);
    ctx.free_usm(B);
    ctx.free_usm(C);
    return 0;
}
```

### Compiling and running it

The project links example programs against the `sushiruntime` library and
applies the SYCL flags for you. Drop your `.cpp` into `examples/` and add it to
`applications/examples/CMakeLists.txt`, following the same pattern the project already uses:

```cmake
add_executable(my_first_program my_first_program.cpp)
target_link_libraries(my_first_program PRIVATE sushiruntime)
set_target_properties(my_first_program PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
add_sycl_to_target(TARGET my_first_program SOURCES my_first_program.cpp)
```

Then build and run through the CLI:

```bash
sr build
sr run my_first_program
```

`sr run` matches the target name exactly first, then by substring, so a
partial name works too.

### Abstracting the task bodies into functions

The first program writes its task bodies as inline lambdas. That's fine for a
one-off, but once a task grows — or you want to reuse it, name it, or unit-test
it on its own — you'll want to pull it out into a proper function. You can,
because `add_task` doesn't actually require a lambda. It requires a *callable*
with the right signature, and a lambda is just one kind of callable.

Recall the two type aliases the API is built on (both live in
`SushiRuntime::Graph`, so they're already in scope with `using namespace
SushiRuntime::Graph;`):

```cpp
// Host / library work — what the 4-argument add_task takes:
using HostWork = std::function<sycl::event(sycl::queue&, const std::vector<sycl::event>&)>;

// Dedicated device-kernel body — taken by the 5-argument add_task overload:
using TaskWork = std::function<void(sycl::handler&)>;
```

`std::function` accepts **anything** that matches its signature: a lambda, a
plain free function, or a functor (an object with `operator()`). So the question
"can I use a function instead of a lambda?" has a yes — with one catch worth
understanding.

**The catch: state.** Our task bodies don't just compute; they need the data
pointers (`A`, `B`, `C`) and the size (`N`). The inline lambdas got those by
*capturing* them (`[A, B, C]`). A plain free function captures nothing — it can
only see its two parameters (`sycl::queue&` and the dependency vector) plus
globals. So to move a body into a function you have to get that state to it some
other way. There are three idiomatic patterns, from lightest to most reusable.

#### Pattern 1 — Free function for the logic, thin adapter at the call site

Put the actual computation in an ordinary, testable free function that takes
whatever it needs as parameters, and keep a one-line lambda that forwards to it.
The lambda is now trivial; all the real code lives in a normal function.

```cpp
// Plain function — easy to read, easy to unit-test in isolation.
void fill_inputs(float* A, float* B, float* C, size_t N)
{
    for (size_t i = 0; i < N; ++i)
    {
        A[i] = static_cast<float>(i);
        B[i] = static_cast<float>(i * 2);
        C[i] = 0.0f;
    }
}

// The lambda is just an adapter that supplies the captured state.
graph.add_task(init_meta, {}, {A, B, C},
    [A, B, C, N](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
    {
        fill_inputs(A, B, C, N);
        return {};
    });
```

#### Pattern 2 — A factory that returns a `HostWork`

This is the pattern SushiRuntime's own test suite uses (see
`make_order_recorder` in `tests/common/test_helpers.hpp`). Write a function that
takes the state and *returns* the callable. The capturing is done once, inside
the factory, and the call site becomes a clean one-liner.

```cpp
// Returns a ready-to-use task body that closes over the buffers.
Graph::HostWork make_init(float* A, float* B, float* C, size_t N)
{
    return [A, B, C, N](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
    {
        for (size_t i = 0; i < N; ++i)
        {
            A[i] = static_cast<float>(i);
            B[i] = static_cast<float>(i * 2);
            C[i] = 0.0f;
        }
        return {};
    };
}

// Call site: no lambda in sight.
graph.add_task(init_meta, {}, {A, B, C}, make_init(A, B, C, N));
```

#### Pattern 3 — A functor (a struct with `operator()`)

If a task has real identity — configuration, several parameters, or you want it
in a header to reuse across programs — make it a small struct. It stores its
state as members and implements `operator()` with the `HostWork` signature.

```cpp
struct VectorAdd
{
    float* A;
    float* B;
    float* C;
    size_t N;

    sycl::event operator()(sycl::queue& q, const std::vector<sycl::event>& deps) const
    {
        return q.submit([&](sycl::handler& h)
        {
            h.depends_on(deps);
            // NOTE: the kernel body must capture by value [=]; see below.
            float* a = A; float* b = B; float* c = C; size_t n = N;
            h.parallel_for(sycl::range<1>(n), [=](sycl::id<1> i)
            {
                c[i] = a[i] + b[i];
            });
        });
    }
};

// Pass an instance — it's convertible to HostWork.
graph.add_task(add_meta, {A, B}, {C}, VectorAdd{A, B, C, N});
```

> **The device-kernel subtlety.** The *outer* callable (the lambda, factory
> result, or functor) runs on a normal CPU thread, so it can capture by
> reference. But the **inner** kernel lambda passed to `parallel_for` runs on the
> device, so it must capture everything **by value** (`[=]`) — the runtime copies
> those values into the kernel. When you move a kernel into a functor, don't let
> the inner lambda capture `this` or members by reference; copy the members into
> local variables first (as `a`, `b`, `c`, `n` above) and let the kernel capture
> those by value. This is a SYCL rule, not a SushiRuntime one, but it's the most
> common mistake when refactoring a kernel out of `main`.

#### The first program, refactored

Putting Pattern 2 and Pattern 3 together, the body of `main` becomes a clean
description of the pipeline — *what* runs, not *how* each step is implemented:

```cpp
Graph::HostWork make_init(float* A, float* B, float* C, size_t N); // defined above
struct VectorAdd { /* defined above */ };

int main()
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    constexpr size_t N = 1024;
    float* A = ctx.malloc_shared<float>(N);
    float* B = ctx.malloc_shared<float>(N);
    float* C = ctx.malloc_shared<float>(N);

    graph.add_task(TaskMetadata{"init"},       {},     {A, B, C}, make_init(A, B, C, N));
    graph.add_task(TaskMetadata{"vector_add"}, {A, B}, {C},       VectorAdd{A, B, C, N});

    ctx.get_execution_engine().execute(graph.compile());

    std::cout << "C[1] = " << C[1] << "  C[100] = " << C[100] << '\n';

    ctx.free_usm(A);
    ctx.free_usm(B);
    ctx.free_usm(C);
    return 0;
}
```

The same three patterns apply to the 5-argument overload's `TaskWork`
(`void(sycl::handler&)`) device body — a free function `void my_kernel(sycl::handler&)`
plugs in directly when it needs no captured state, and a factory or functor
supplies the state when it does.

---

## 5. The core API, in detail

### `RuntimeContext`

```cpp
RuntimeContext ctx;                           // discover everything, start workers
```

Constructing it with no arguments uses production defaults (it probes all
devices via hwloc). You can pass a `Topology::HardwareFilterConfig` to limit
discovery to certain device classes:

```cpp
Topology::HardwareFilterConfig cfg;
cfg.include_gpu = false;                       // CPU-only
cfg.require_fp64 = true;                       // drop devices without double precision
RuntimeContext ctx{cfg};
```

`require_fp64` is worth setting whenever your kernels use `double`. Without it, a
device lacking `sycl::aspect::fp64` is selected happily and the failure surfaces
later, inside the driver, with a message that names neither the device nor the
reason. With it, the device is skipped at discovery and the skip is logged.

If the filter excludes every device the machine has — or the machine genuinely
exposes no SYCL device, which is the common case on a box with no GPU driver and
no OpenCL CPU runtime installed — construction throws a `SushiException` carrying
`Core::Errc::unsupported` and the filter that produced the empty list.

The context is **non-copyable and non-movable** — create one and pass it around
by reference. It cleans itself up in the correct order on destruction; you can
also call `ctx.shutdown()` for explicit, ordered teardown, or `ctx.wait_all()`
to wait for all asynchronous work to finish.

Useful accessors:

```cpp
sycl::queue& q  = ctx.get_queue(0);            // queue for device index 0
sycl::queue& nq = ctx.get_numa_local_queue(0); // queue closest to NUMA node 0
IExecutionEngine& eng = ctx.get_execution_engine();
```

### Memory

Allocate USM through the context whenever the data will be touched by graph
tasks. The context remembers which queue (device + context) owns each
allocation, so the scheduler can dispatch a task to the *exact* queue that owns
its data — avoiding an illegal cross-context access (for example, running a
kernel on a CUDA queue against memory that lives on the CPU).

```cpp
float*  A   = ctx.malloc_shared<float>(N);     // shared USM (CPU + device visible)
double* sum = ctx.malloc_shared<double>(1);
void*   raw = ctx.malloc_shared(N * sizeof(float)); // untyped byte form

void*   D   = ctx.malloc_device(N * sizeof(float)); // device-only USM (byte size)

ctx.free_usm(A);                               // always free what you allocated
```

Both `malloc_shared` and `malloc_device` take an optional NUMA node id
(`ctx.malloc_shared<float>(N, /*numa_id=*/1)`) that selects the owning queue.
`free_usm(nullptr)` is safe.

### `TaskGraph` and adding tasks

```cpp
TaskGraph graph(ctx);
```

There are two ways to add work. The one you'll use most is `add_task`, which
takes metadata plus read/write pointer lists so the runtime can track
dependencies:

```cpp
// Host / library work (also fine for submitting SYCL kernels, as shown above):
graph.add_task(meta, reads, writes, work);
graph.add_task(meta, reads, writes, work, dependencies);   // + manual event deps
```

Here `work` is a `HostWork` callable with this exact signature:

```cpp
sycl::event work(sycl::queue& q, const std::vector<sycl::event>& deps);
```

- For pure CPU work, do your computation and `return {};` (an empty event).
- To run a SYCL kernel, `return q.submit(...)` and call `h.depends_on(deps)`
  inside so the kernel waits on its inputs.

There is also an overload that takes a dedicated **device** body (a
`TaskWork = std::function<void(sycl::handler&)>`) plus a host fallback, for
tasks that should run as a kernel when a device is selected and fall back to the
host otherwise:

```cpp
graph.add_task(meta, reads, writes,
               device_work,      // void(sycl::handler&)
               fallback_work);   // sycl::event(sycl::queue&, const std::vector<sycl::event>&)
```

If you don't need dependency tracking, the lower-level `add_node` lets you submit
raw work directly, optionally with read/write pointers or manual event
dependencies:

```cpp
graph.add_node(work);                              // work is void(sycl::handler&)
graph.add_node(work, reads, writes);
graph.add_node(work, reads, writes, dependencies);
```

### `TaskMetadata`

Every `add_task` takes a `TaskMetadata`. The only field you must set is a name
(used in profiling); the rest are optional scheduling hints:

```cpp
TaskMetadata meta{"my_task"};       // name via aggregate init
meta.priority      = 10;            // higher number = scheduled ahead of others
meta.numa_affinity = 0;             // prefer NUMA node 0 (-1 = any, the default)
meta.task_type     = TaskType::MATH_OP;   // optional high-level category
```

`TaskType` is one of `NONE`, `MATH_OP`, `MEMORY_COPY`, `CUSTOM_HOST`, or
`CONTROL_FLOW`.

You can also attach up to 8 small scalar parameters (each ≤ 8 bytes) — handy for
passing sizes or coefficients into a kernel without capturing them:

```cpp
meta.set_param<uint64_t>(0, N);
// ...later, inside the work lambda:
uint64_t n = meta.get_param<uint64_t>(0);
```

**Named operations (`OpID`).** Instead of rigid enums, tasks can carry a
compile-time hashed operation id created with the `_op` literal. This is what the
optional distributed layer uses to identify which kernel to run on a remote
worker:

```cpp
using namespace SushiRuntime::Graph::Literals;

TaskMetadata meta{"gemm"};
meta.op_id = "blas.gemm"_op;        // a stable 32-bit FNV-1a hash, computed at compile time
```

### How dependencies are inferred

The runtime compares the read/write pointer lists across tasks and serializes
only the pairs that conflict:

- **Read-after-write (RAW):** task B reads what task A wrote → B waits for A.
- **Write-after-read (WAR):** task B overwrites what task A read → B waits for A.
- **Write-after-write (WAW):** two tasks write the same memory → ordered.

Tasks that touch *disjoint* memory have no dependency and run in parallel. So a
chain of tasks all touching the same pointer runs strictly in order, while ten
tasks touching ten different buffers all run at once. You can also pass explicit
`sycl::event` dependencies as the last argument if you need ordering the pointer
analysis can't see.

The analysis works on **regions**, not whole buffers: two tasks are serialized
only when their byte ranges actually overlap. Name a sub-range with
`buffer.region({offset, count})` and pass it to `Reads(...)`/`Writes(...)`, and the
runtime will run tasks on disjoint slices of one buffer in parallel — an array's
two halves, or a grid's interior and its halo. A whole buffer (the default when
you pass a handle) overlaps every slice of itself, so it stays conservative.

### Compile and execute

```cpp
auto compiled = graph.compile();                       // resolve the DAG (does not run anything)
ctx.get_execution_engine().execute(std::move(compiled)); // run it; blocks until done
```

`compile()` links the graph's leaf tasks to an internal completion sentinel,
finds the root tasks (those with no dependencies), and hands ownership to a
`CompiledGraph`. It does **not** submit anything to the hardware. `execute()`
injects the roots into the scheduler and blocks until the whole graph finishes.
Build the entire graph before calling `compile()` — the two phases are not
thread-safe relative to each other.

---

## 6. A second example: a three-stage pipeline

This shows dependencies doing real work. Three tasks share the same buffer, so
they're forced into order: initialize → scale (on the device) → reduce on the
host. Each stage reads what the previous one wrote.

```cpp
RuntimeContext ctx;
TaskGraph graph(ctx);

constexpr size_t N     = 2048;
constexpr float  SCALE = 3.0f;

float*  data = ctx.malloc_shared<float>(N);
double* sum  = ctx.malloc_shared<double>(1);

// Stage 1: data[i] = i   (host write of `data`)
graph.add_task(TaskMetadata{"init"}, {}, {data},
    [data](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
    {
        for (size_t i = 0; i < N; ++i) data[i] = static_cast<float>(i);
        return {};
    });

// Stage 2: data[i] *= SCALE   (device kernel: reads AND writes `data`)
graph.add_task(TaskMetadata{"scale"}, {data}, {data},
    [data](sycl::queue& q, const std::vector<sycl::event>& deps) -> sycl::event
    {
        return q.submit([&](sycl::handler& h)
        {
            h.depends_on(deps);
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i)
            {
                data[i] = data[i] * SCALE;
            });
        });
    });

// Stage 3: sum = reduce(data)   (host read of `data`, write of `sum`)
graph.add_task(TaskMetadata{"reduce"}, {data}, {sum},
    [data, sum](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
    {
        double acc = 0.0;
        for (size_t i = 0; i < N; ++i) acc += data[i];
        *sum = acc;
        return {};
    });

auto compiled = graph.compile();
ctx.get_execution_engine().execute(std::move(compiled));

// *sum == SCALE * (0 + 1 + ... + (N-1)) == SCALE * (N-1)*N/2
std::cout << "sum = " << *sum << '\n';

ctx.free_usm(data);
ctx.free_usm(sum);
```

Notice that the *scale* task lists `data` as both a read and a write — that's how
you express an in-place update, and it keeps the task correctly ordered between
*init* and *reduce*.

---

## 7. Where to go next

- **[ARCHITECTURE.md](../architecture/ARCHITECTURE.md)** — what is underneath all of this: the
  layers, the work-stealing scheduler, the dependency tracker, the memory model,
  and the determinism contract. Read it before changing anything.
- **[INTEGRATION.md](../guides/INTEGRATION.md)** — consuming the runtime from your own
  project with `find_package(SushiRuntime)`, and which compiler flags you must
  match for the determinism guarantee to hold.
- **[CLI_GUIDE.md](../guides/CLI_GUIDE.md)** — every `sr` command: build types, test
  suites, running binaries, Docker, and diagnostics.
- **[README](../../README.md)** — requirements, the full architecture (memory
  pool, work-stealing scheduler, topology mapping), CMake options, and the
  optional distributed master-worker layer.
- **`tests/functional/integration/`** — runnable, real-world usage of every
  feature in this guide. `test_fluent_lifecycle.cpp` covers the fluent API
  (handles, stepping, coupling); `test_sycl_execution.cpp` and `test_graph.cpp`
  are the best starting points for the lower-level API.
- **`sr docs`** — generate the full API reference from the headers.
