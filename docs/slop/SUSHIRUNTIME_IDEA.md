# Running SushiBLAS, SushiAI, and SushiEngine on SushiRuntime

This document describes how three independent compute libraries — **SushiBLAS**
(a oneMKL wrapper / BLAS), **SushiAI** (an AI framework), and **SushiEngine**
(a physics library) — are meant to run their computations on top of
**SushiRuntime**. It is a design specification, not a tutorial. It states the
decisions that have been made, the contracts each component must honor, and the
reasoning behind them.

The goal is to be precise about ownership, instancing, calling conventions, and
coupling, so that the integration is unambiguous before any code is written.

The API references in this document (`HostWork`, the two `add_task` overloads,
`RuntimeContext::malloc_shared`/`free_usm`/`wait_all`, the `OpID` literal system,
`DependencyTracker`, `RangeTask::split`) reflect the current source tree under
`include/SushiRuntime/` and the integration tests under `tests/functional/`, not
an idealized API.

---

## 1. What SushiRuntime is, in this context

SushiRuntime is **not** a compute library. It does not implement GEMM, neural
network layers, or physics integrators. It is a **task-based orchestrator**: you
describe work as a graph of tasks, declare which memory each task reads and
writes, and the runtime computes a safe execution order and runs the independent
parts in parallel across CPU and GPU devices using SYCL.

The consequence for this design is the central organizing principle:

> SushiRuntime decides **when, where, and in what order** work runs.
> The libraries decide **what is computed**. These two concerns never mix.

SushiBLAS, SushiAI, and SushiEngine are therefore **compute domains** layered on
top of the runtime. The runtime never gains a notion of "BLAS" or "physics"; the
libraries never gain a notion of "scheduler" or "dependency graph."

---

## 2. Decisions (locked)

These were decided explicitly and the rest of the document follows from them.

| Topic                     | Decision                                                                 |
| ------------------------- | ------------------------------------------------------------------------ |
| **Coupling**              | Libraries are **pure**. They do not know SushiRuntime exists.            |
| **Queue**                 | Computations run on the **SYCL queue the runtime hands to the task**.    |
| **Runtime instance**      | A **single shared** `RuntimeContext` per application.                    |
| **Library instances**     | **None.** Libraries are stateless free functions.                        |
| **Inter-library coupling**| **None.** No library depends on, imports, or references another.         |
| **Adapter layer**         | **None.** The application wraps library calls into tasks directly.       |
| **Memory ownership**      | The **application** owns all USM memory. Libraries never allocate/free.  |

The defining requirement from the project owner: the three libraries must have
**no link between them whatsoever**. Sometimes all three are used together,
sometimes only BLAS, sometimes only Engine. There must never be a compile-time or
runtime dependency that ties one to another.

---

## 3. Layering and dependency direction

```
SushiBLAS      SushiAI      SushiEngine        pure compute libraries
      \           |           /                (know only SYCL + their own types)
       \          |          /
        v         v         v
   [ application: owns RuntimeContext, builds the task graph ]   the only binding point
                  |
                  v
            SushiRuntime                        schedules and runs; domain-agnostic
```

Dependency arrows point **downward only**:

- No library includes a SushiRuntime header.
- No library includes another library's header.
- The application is the **only** place where a library and the runtime meet,
  and even there they meet only through plain data (SYCL queues, raw USM
  pointers, SYCL events).

A "link" between two libraries, when it occurs, exists **only inside one task
graph, for the lifetime of that graph**, and only because the application chose
to feed the output pointer of one task into the input of another. It is a
runtime data relationship, never a code dependency.

---

## 4. The library contract

Every operation exposed by SushiBLAS, SushiAI, and SushiEngine follows the same
shape:

```cpp
sycl::event op(sycl::queue& q,
               /* input/output USM pointers + sizes/params */,
               const std::vector<sycl::event>& deps);
```

Example (SushiBLAS):

```cpp
namespace SushiBLAS {
    sycl::event gemm(sycl::queue& q,
                     const float* A, const float* B, float* C,
                     int m, int n, int k,
                     const std::vector<sycl::event>& deps);
}
```

Rules each library must obey:

1. **No runtime header.** Include only `<sycl/sycl.hpp>` and the library's own
   types. The library must compile and be usable with nothing but a SYCL queue.
2. **Use the given queue.** Submit work to `q`. Do not create or own a queue.
   This is what makes "run on the runtime's queue" true: the runtime supplies the
   device queue, oneMKL (and the others) run on it. **Important:** USM is bound to
   the device/context it was allocated on. The library must submit to the queue it
   is handed and must not move the data to another device's queue — touching CPU
   USM from a CUDA queue (or vice versa) is an illegal cross-context access. The
   scheduler already routes a task to the queue that owns its data (see
   `Node::io_reads_/io_writes_` and `RuntimeContext::get_queue_for_ptr`), so the
   library simply honoring `q` is sufficient.
3. **Chain the given dependencies.** Wire `deps` into the kernel's dependency
   list, and return the operation's own `sycl::event` so the next task can depend
   on it.
4. **Do not block.** Never call `q.wait()` or otherwise synchronize.
   Synchronization is the runtime's responsibility.
5. **Do not allocate or free device memory.** All USM pointers come from the
   outside (the application's allocator). This is what keeps the libraries
   stateless and guarantees the zero-coupling rule.
6. **Be stateless.** Hold no handles, no buffers, no global state between calls.

A library that honors this contract is independently testable: hand it a bare
`sycl::queue` and pointers, with no runtime present at all.

---

## 5. The application's responsibilities

The application is the single integration point. It does four things.

### 5.1 Instancing

Construct exactly **one** `RuntimeContext` at startup:

```cpp
RuntimeContext ctx;   // probes hardware, builds per-device queues, starts workers
```

- It belongs to the **application**, not to any library.
- It is **heavy** (hardware discovery, USM allocators, worker pool), so it is
  created **once** for the process lifetime and torn down at shutdown — never per
  call, never per library.
- All three usage scenarios share this same `ctx`.

The libraries themselves are **never instantiated**. They are stateless free
functions; there is nothing to construct.

### 5.2 Memory ownership

The application allocates all USM, passes **raw pointers** to the libraries, and
frees them when done. The runtime exposes allocation directly on the context, and
this is the preferred path because it records which queue (device + context) owns
each allocation:

```cpp
float* A = ctx.malloc_shared<float>(N, /*numa_id=*/0);   // pinned to a NUMA-local queue
// ... use A in tasks ...
ctx.free_usm(A);
```

- Prefer `ctx.malloc_shared/malloc_device` over a bare `sycl::malloc_*`. The
  runtime remembers the owning queue, so the scheduler can dispatch each task to
  the exact queue that owns its data and never trigger a cross-context fault.
- The `numa_id` argument pins the allocation to a NUMA-local compute queue. Tasks
  that read/write that pointer should run on the same node — which the scheduler
  arranges automatically from the `reads`/`writes` sets.

The same pointer is the shared vocabulary of three parties:

1. the runtime's dependency tracker (via the task's `reads`/`writes` sets),
2. the SYCL kernel (the actual computation),
3. the library function (the pure signature).

Because memory lives in no library, two tasks from two different libraries can
operate on the same pointer without the libraries knowing about each other.

> One real constraint, observed in the integration tests: a buffer allocated on
> one device's queue is **not valid on another device's queue**. If a workload
> spans multiple devices, each chain must keep its allocation and its kernels on
> the same device/context. This is a property of SYCL USM, not of the runtime.

### 5.3 Building the graph

For each unit of work, create a `TaskGraph` bound to `ctx`, then wrap each
library call in a task. The task's `reads`/`writes` pointer sets are how the
application expresses data flow; the runtime derives ordering from them.

```cpp
TaskGraph g(ctx);

g.add_task(TaskMetadata{"gemm"}, /*reads*/{A, B}, /*writes*/{C},
    [=](sycl::queue& q, const std::vector<sycl::event>& deps) -> sycl::event {
        return SushiBLAS::gemm(q, A, B, C, m, n, k, deps);
    });
```

Note what each piece does:

- `q` — the runtime's device queue; satisfies "run on the runtime's queue."
- `SushiBLAS::gemm` — the pure call; it never sees the runtime.
- `{A, B}` / `{C}` — the runtime uses these to detect RAW/WAR/WAW hazards and
  order this task relative to others automatically.
- `deps` — the SYCL event chain the application threads into the library call.

`add_task` has two overloads. The example above is the **host-work** form, which
is the right one for wrapping a pure library call (the library submits to the
queue itself and returns the event):

```cpp
void add_task(const TaskMetadata& meta,
              const std::vector<void*>& read_access,
              const std::vector<void*>& write_access,
              HostWork fallback_work,
              const std::vector<sycl::event>& dependencies = {});
```

The second overload also takes a `device_work` (`std::function<void(sycl::handler&)>`)
for tasks that want the runtime to drive the kernel submission directly, with the
host work kept as a CPU fallback:

```cpp
void add_task(const TaskMetadata& meta,
              const std::vector<void*>& read_access,
              const std::vector<void*>& write_access,
              TaskWork  device_work,
              HostWork  fallback_work,
              const std::vector<sycl::event>& dependencies = {});
```

For the "pure libraries" design, the host-work overload is the natural fit: the
library owns its own kernel submission, so wrapping it in a single `HostWork`
lambda is enough.

### 5.4 Running

```cpp
auto compiled = g.compile();
ctx.get_execution_engine().execute(std::move(compiled));
```

`compile()` resolves dependencies into a runnable form; `execute()` runs it and
blocks until the whole graph completes. (Per the runtime's rule, build the entire
graph first, then compile — graph construction and `compile()` are not
thread-safe relative to each other.)

`execute()` blocks on the graph's sentinel completion node. If the host then
reads device-side results directly, call `ctx.wait_all()` first to guarantee all
device kernels are visible to the host — the integration tests follow this
pattern. For graphs whose outputs are consumed by later graphs (not read on the
host immediately), the sentinel block is sufficient.

---

## 6. The three usage scenarios

The same `ctx` serves all three. The libraries remain mutually unaware in every
case.

### 6.1 BLAS only

```cpp
RuntimeContext ctx;
TaskGraph g(ctx);
g.add_task({"gemm"}, {A, B}, {C},
    [=](sycl::queue& q, auto& deps) { return SushiBLAS::gemm(q, A, B, C, m, n, k, deps); });
auto c = g.compile();
ctx.get_execution_engine().execute(std::move(c));
```

### 6.2 Engine only

Identical structure; only the call differs:

```cpp
g.add_task({"step"}, {state}, {next},
    [=](sycl::queue& q, auto& deps) { return SushiEngine::step(q, state, next, dt, deps); });
```

### 6.3 All three together (one graph, temporary chain)

```cpp
g.add_task({"physics"}, {state},  {forces},
    [=](auto& q, auto& d) { return SushiEngine::step(q, state, forces, dt, d); });

g.add_task({"solve"},   {forces}, {x},
    [=](auto& q, auto& d) { return SushiBLAS::gemm(q, /* uses forces -> x */, d); });

g.add_task({"infer"},   {x},      {out},
    [=](auto& q, auto& d) { return SushiAI::infer(q, x, out, d); });
```

The chain `physics -> solve -> infer` arises **only from the shared pointers**
(`forces`, then `x`), not from any link between the libraries. Tasks that do not
share a pointer run in parallel. SushiEngine, SushiBLAS, and SushiAI still know
nothing about one another; the relationship exists only in this graph, only while
it runs.

---

## 7. Guarantees this design provides

- **Zero inter-library coupling.** Delete any one library and the others still
  compile. Each can be exercised on its own with nothing but a `sycl::queue`.
- **Single hardware probe.** One `RuntimeContext` means hardware is discovered
  once; all three domains share the same worker pool, queues, and NUMA map.
- **Runtime stays domain-agnostic.** No BLAS/AI/physics concept ever enters the
  runtime. The `OpID` mechanism (`"blas.gemm"_op`, etc.) is only naming and
  diagnostics — and a future hook for distributed offload — not a dependency.
- **Stateless, independently testable libraries.** Because memory ownership and
  scheduling both live in the application, the libraries hold no state and have
  no hidden ordering assumptions.

---

## 8. Direct answers to the framing questions

- **How do I run them?** Wrap the pure library call in a lambda task, add it to a
  `TaskGraph`, then `compile()` and `execute()`. The library never sees the
  runtime.
- **How do I call them?** Through the pure signature
  `op(sycl::queue& q, <pointers + sizes>, const std::vector<sycl::event>& deps)`.
  The task lambda forwards `q` and `deps`.
- **How do I set things up?** You do **not** set dependencies manually. You
  provide each task's `reads`/`writes` pointers and the runtime derives ordering.
  You thread the SYCL event chain through `deps`.
- **Do I create instances, or not?**
  - Libraries: **no instance** — stateless free functions.
  - Runtime: **one** shared `RuntimeContext` for the process.
  - Graph: **one** lightweight `TaskGraph` per unit of work.

---

## 8a. Correctness model and the raw-pointer trade-off

Ordering correctness in this design rests entirely on one thing: the
`reads`/`writes` pointer sets each task declares. The runtime trusts them. It
does not, and cannot, verify that a task actually touches only the memory it
declared.

This has a precise, important consequence:

> If a task's declared `reads`/`writes` are wrong (a missing pointer, a wrong
> pointer, a forgotten output), the runtime will still produce a valid-looking
> schedule — just the wrong one. The result is a **silent ordering error**, not a
> crash.

Why this matters specifically here:

- The error class is **silent**. A missing dependency manifests as a data race
  or stale read under certain interleavings, often only under load. The compiler
  cannot catch it; many tests will not either. The program keeps running and
  returns a result that is simply incorrect.
- For a correctness-critical pipeline (e.g. a physics step feeding a solver
  feeding inference), a wrong order means a wrong number, propagated downstream
  and trusted. An incorrect result is more dangerous than a crash, because
  nothing signals that anything went wrong.

Two mechanism-level details that follow from how the tracker actually works
(`DependencyTracker`):

- **Identity is the pointer value.** Dependencies are tracked per pointer. Two
  tasks are ordered only if they name the *same* pointer in their read/write
  sets. If two tasks operate on overlapping regions through *different* base
  pointers (e.g. a buffer and an interior offset into it), the tracker does not
  see them as the same resource and will not order them. The application must
  declare a consistent base pointer for aliasing accesses.
- **Pointers are hashed into a fixed number of shards** (1024). Distinct
  pointers can collide into the same shard. A collision never causes a missed
  dependency (the correctness-relevant direction), but it can introduce a
  *spurious* dependency — two unrelated tasks serialized unnecessarily. The
  effect is lost parallelism, not a wrong result; worth knowing when profiling
  unexpected serialization.

Where the raw-pointer model is fine:

- It is the cheapest possible interop vocabulary — a pointer is the one thing the
  dependency tracker, the SYCL kernel, and the pure library function can all
  share with zero translation.
- Within a small team that knows the rule, the risk is controllable through code
  review and disciplined declaration of every input and output.

Mitigation direction (additive, not a redesign):

- A **typed buffer/handle abstraction** layered above raw pointers can move the
  declaration of inputs and outputs to the point of allocation, so the
  read/write sets are derived from the handle rather than hand-written per task.
  This closes most of the silent-ordering class at registration/compile time.
- This is an additive layer on top of the existing pointer model. It does not
  require changing the runtime core; the application keeps owning memory, and the
  libraries keep their stateless pointer-based contract underneath.

This is the single largest piece of technical debt in the model, and the highest
-leverage improvement to its correctness story.

---

## 8b. Determinism and reproducibility

The scheduler is, by design, **non-deterministic in execution order**. Work
stealing, adaptive event polling, and the optional thermal rebalancer all make
run-to-run scheduling vary. This is the right default for throughput.

The implication to be explicit about:

> Two runs of the same graph on the same inputs are guaranteed to produce
> **dependency-correct** results, but not necessarily the **identical**
> scheduling, timing, or — where floating-point reduction order is
> order-sensitive — the bit-identical numeric result.

For most workloads this is irrelevant. It becomes relevant when a result must be
**reproduced exactly** — for example, re-running an experiment and expecting the
same numbers, or diffing two runs to isolate a change. Non-deterministic
reduction order can make a previously observed result hard to reproduce bit-for
-bit.

What already exists toward this:

- The execution layer is interface-driven (`IExecutionEngine`). The README
  explicitly anticipates alternative strategies, including a **replay** engine,
  pluggable without touching `TaskGraph` or `RuntimeContext`.

Direction (optional, additive):

- An **optional deterministic / replay mode** could fix the execution order (and,
  where needed, the reduction order) so a given graph + inputs reproduces the
  same result every time. The point of such a mode here is **reproducibility of
  results**, not regulatory certification.
- It would be a mode, not the default: throughput stays the normal path;
  determinism is opted into only when exact reproduction is required.

---

## 9. Open considerations (not yet decided)

These are out of scope for the locked design but worth recording:

- **OpID registration.** If distributed offload is ever wanted for these
  domains, each operation would register a named `OpID` so the kernel is
  resolvable on remote workers. Until then, `OpID`s are optional and serve only
  naming/diagnostics.
- **Buffer registration for offload.** Distributed mode resolves data by
  `GlobalBufferID`+version rather than raw pointers. Application-owned USM would
  need to be registered with the coordinator for any task intended to be
  offloadable. This is additive and does not affect the standalone path.
- **Splittable operations.** Large, divisible library operations (e.g. a big
  GEMM) could expose a `split()` to take advantage of the runtime's `RangeTask`
  parallelism. This is an optimization, not a requirement of the contract.
