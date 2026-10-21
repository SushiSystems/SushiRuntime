# BB-2: Co-tenancy, thread and scheduling policy

**The problem.** `Runtime::create()` today spawns `hardware_concurrency()` workers and pins
worker *i* to core *i* (`scheduler.cpp:63-67, 343`): worker 0 lands on the same core as the
engine's main thread; the render thread, audio callback and job system fight the full-width pool.
A background rebalancer thread runs a 5 ms heartbeat by default and is documented as a
determinism/jitter hazard (`runtime.hpp:191-197`). Idle workers wake every 10 ms regardless.
There is no knob for any of it: `RuntimeConfig` has exactly one field.

**The change: `RuntimeConfig` becomes the co-tenancy contract:**

```cpp
struct RuntimeConfig
{
    bool profiling = false;
    std::size_t worker_count = 0;      // 0 = all cores (today's behavior)
    std::size_t first_core   = 0;      // pin workers to [first_core, first_core + count)
    PinPolicy   pinning      = PinPolicy::Pin;   // Pin | Float
    bool        rebalancer   = false;  // BREAKING: default flips to off (P3 — sim is the
                                       // primary consumer; batch throughput opts in)
};
```

- The engine already turns the rebalancer off at every solver construction and *asserts the
  runtime default is on* in a test (`test_runtime_graph_builder.cpp:266-277`), that test
  inverts, deliberately, and the engine deletes its workaround. Pre-1.0, no shim (P1).
- Worker priority stays untouched in this package (no cross-platform RT-priority pretense);
  what BB-2 guarantees is *placement*: the engine can reserve cores for its main/render/audio
  threads and give the runtime the rest.
- Idle wake: the 10 ms `wait_for` park and the 100 µs spin-sleep are re-examined once workers
  can be fewer than cores; a parked worker on a reserved-core config must cost nothing.
- `RunReport` allocation on every run (`numa_statistics.push_back`:
`run_report_builder.hpp:92`) gets a caller-owned variant: `run(RunReport& reuse)` /
  `wait(RunReport& reuse)` clearing and refilling capacity-retained vectors. Zero-allocation
  steady-state ticks become measurable, then enforceable.

**Proof.** A two-runtime co-tenancy test (engine-shaped: N workers on cores 4…11, a busy "main
thread" pinned to core 0) shows no runtime thread ever schedules on the reserved cores; replay
determinism test passes with the new defaults; steady-state tick allocation count == 0 under the
reuse API (asserted with the benchmark suite's counting allocator).
