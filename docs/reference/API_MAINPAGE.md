# SushiRuntime API reference {#mainpage}

SushiRuntime decides when, where and in what order work runs on the CPUs and GPUs of one
machine. Your code decides what is computed. This site is the reference of the headers under
`modules/surface/api/include/SushiRuntime/api`, which a program includes through
`<SushiRuntime/sushiruntime.hpp>`. Every public name is in the namespace `SushiRuntime`.

The modules beneath this surface are described in `docs/architecture/ARCHITECTURE.md`, and
`docs/getting_started/INTRODUCTION.md` walks through complete programs.

## The model

1. **Allocate data as handles.** `SushiRuntime::Buffer` is a typed USM block.
   `SushiRuntime::State` is a field with two buffers that the runtime swaps after each step.
   `SushiRuntime::Residency::Device` keeps a field on its device.
2. **Record work** on a `SushiRuntime::Graph`. Each verb names the data a kernel reads and
   writes, and the runtime derives the order from it.
3. **Run** with `run()`, `run_for()`, `run_until()` or `run_async()`. The graph compiles on the
   first run and the compiled plan is replayed for every later step. Every run verb returns a
   `SushiRuntime::Result`.

```cpp
    SushiRuntime::Runtime runtime = SushiRuntime::Runtime::create().value();

    SushiRuntime::State<float> heat = runtime.state<float>(N).value();
    for (std::size_t i = 0; i < N; ++i)
        heat[i] = (i == N / 2) ? 1.0f : 0.0f;

    SushiRuntime::Graph graph = runtime.graph();
    graph.add_rule(heat, [](std::size_t i, const float* cur, float* next)
    {
        const float left  = (i == 0)     ? cur[i] : cur[i - 1];
        const float right = (i == N - 1) ? cur[i] : cur[i + 1];
        next[i] = cur[i] + 0.25f * (left - 2.0f * cur[i] + right);
    });

    const SushiRuntime::RunReport report = graph.run(STEPS).value();
```

Source: `tests/package/main.cpp`

## Where to look

| To | Start at |
| --- | --- |
| Create a runtime and configure it | `SushiRuntime::Runtime`, `SushiRuntime::RuntimeConfig` |
| Allocate data | `SushiRuntime::Buffer`, `SushiRuntime::State` |
| Record a kernel over buffers | `add()` with `SushiRuntime::Reads` and `SushiRuntime::Writes`, or `SushiRuntime::In`, `SushiRuntime::Out`, `SushiRuntime::InOut` |
| Step one field, or one field that reads others | `add_rule()`, `add_coupled()` with `SushiRuntime::Coupled` |
| Vary the size, the start or the enablement per step | `add_dynamic()` with `SushiRuntime::Dynamic`, `when()`, `sized()`, `based_at()` |
| Take a count or a start from a value a kernel wrote | `sized_from_device()`, `based_at_device()`, `SushiRuntime::LateCount` |
| Run host C++ in the graph | `add_host()`, `add_task()` |
| Record work the runtime does not order | `add_untracked()`, `SushiRuntime::On` |
| Record node kinds of your own | `extend()`, `SushiRuntime::Recorder` |
| State a name, a width, an operation or parameters of a node | `SushiRuntime::NodeOptions`, `name()` |
| Change the set of work between steps | `SushiRuntime::DynamicGraph`, `SushiRuntime::Region` |
| Overlap a step with the caller's work | `run_async()`, `SushiRuntime::RunHandle` |
| Call a function before each step | `before_each_step()` |
| Build or read the plan without running | `compile()`, `SushiRuntime::Graph::plan()`, `SushiRuntime::PlanView` |
| Read devices, queues and counters | `SushiRuntime::AdvancedControls` |
| Exchange memory with another API | `SushiRuntime::Interop` |
| Read what a run did | `SushiRuntime::RunReport` |
| Read the timing of a run | `SushiRuntime::Runtime::add_run_observer()`, `SushiRuntime::RunObserver`, `SushiRuntime::RunObservation` |
| Put the runtime on a SushiProfiler timeline | `SushiRuntime::register_profiler_parts()` |
| Handle a failure | `SushiRuntime::Result`, `SushiRuntime::Error`, `SushiRuntime::ErrorCode` |
| Consume the library from another project | `docs/guides/INTEGRATION.md` |
