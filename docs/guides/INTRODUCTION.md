# Getting started with SushiRuntime

SushiRuntime is a C++23 runtime library for **task-based parallel computing on
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

> For the full command reference, see [CLI_GUIDE.md](CLI_GUIDE.md). For the
> architecture and design, see the top-level [README](../../README.md).

---

## 1. Quick start

### Install and build in one command

On a fresh machine you can go from nothing to a built, smoke-tested project with
a single script. It bootstraps Python/Git, installs the `sr` CLI, then runs the
installer pipeline (`sr setup all`), which installs every dependency, writes your
machine config, and builds and tests the project.

**Linux (Debian/Ubuntu):**

```bash
bash install.sh            # add --gpu for CUDA, --dry-run to preview
```

**Windows (PowerShell):**

```powershell
powershell -ExecutionPolicy Bypass -File install.ps1   # add -Gpu / -DryRun
```

### Or, if you already have a SYCL toolchain

Install the CLI and build directly:

```bash
python cli/install.py        # puts `sr` and `sushiruntime` on your PATH
sr toolchain intel-llvm      # pick a SYCL toolchain (the default; or adaptivecpp / oneapi)
sr project build             # Release build (the default)
sr project test              # run the fast functional test suite
```

You'll need a SYCL 2020 compiler — intel-llvm (`clang++ -fsycl`, primary),
AdaptiveCpp (`acpp`), or Intel oneAPI DPC++ — plus CMake 3.20+, hwloc, and
GoogleTest. Pick the toolchain with `sr toolchain`. The CLI reads your toolchain
paths from `cli/config.local.toml`; `sr setup configure` generates that file for you.
See the [README](../../README.md) for the full requirements list and the CMake
options if you want to skip the CLI.

Verify everything works:

```bash
sr project test --suite functional
```

If the functional suite passes, you're ready to write code.

---

## 2. The mental model

A SushiRuntime program has four moving parts:

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

## 3. Your first program

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
`examples/CMakeLists.txt`, following the same pattern the project already uses:

```cmake
add_executable(my_first_program my_first_program.cpp)
target_link_libraries(my_first_program PRIVATE sushiruntime)
set_target_properties(my_first_program PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON)
add_sycl_to_target(TARGET my_first_program SOURCES my_first_program.cpp)
```

Then build and run through the CLI:

```bash
sr project build
sr project run my_first_program
```

`sr project run` matches the target name exactly first, then by substring, so a
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

## 4. The core API, in detail

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
RuntimeContext ctx{cfg};
```

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

## 5. A second example: a three-stage pipeline

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

## 6. Where to go next

- **[CLI_GUIDE.md](CLI_GUIDE.md)** — every `sr` command: build types, test
  suites, running binaries, Docker, and diagnostics.
- **[README](../../README.md)** — requirements, the full architecture (memory
  pool, work-stealing scheduler, topology mapping), CMake options, and the
  optional distributed master-worker layer.
- **`tests/functional/integration/`** — runnable, real-world usage of every
  feature in this guide. `test_sycl_execution.cpp` and `test_graph.cpp` are the
  best starting points.
- **`sr project doxygen`** — generate the full API reference from the headers.
