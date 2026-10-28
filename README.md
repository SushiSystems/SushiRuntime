# SushiRuntime

SushiRuntime is a C++17 library for programs that run a graph of tasks on the CPUs and GPUs of
one machine through SYCL, ordering the work from the buffers each task reads and writes.

```cpp
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
```

Source: `applications/examples/quickstart.cpp`

## Build

Standalone, with Git, Python 3.11 and CMake:

```bash
git clone https://github.com/SushiSystems/SushiRuntime.git
cd SushiRuntime
pip install -e ./cli
sr setup                          # hwloc, a SYCL toolchain, SushiLog and SushiProfiler
sr build
sr test
```

With SushiHub: `hub add sushiruntime` and `hub install-cli sushiruntime`, or
`hub link sushiruntime <path>` for a checkout you already have.
[`docs/getting_started/INSTALL.md`](docs/getting_started/INSTALL.md) has the prerequisites for
both.

Every build and test in this repository goes through `sr`.
[The CLI guide](docs/guides/CLI_GUIDE.md) lists the requirements and every command, and
[the integration guide](docs/guides/INTEGRATION.md) shows how another project links the
installed package.

## Status

SushiRuntime is at 0.5.0, below 1.0.0, so a MINOR release may break its public interface. The
graph, the scheduler, the memory tiers and the hardware discovery work on the three supported
SYCL toolchains. Offload to other machines is optional (`sr build --distributed`) and has the
limits that [`KNOWN_ISSUES.md`](docs/backlog/README.md) lists. What is left is in
[`REMAINING_WORK.md`](docs/backlog/README.md).

## Documentation

[docs/README.md](docs/README.md) lists every page of the manual; start with
[the introduction](docs/getting_started/INTRODUCTION.md). Contributors read
[docs/CONTRIBUTING.md](docs/CONTRIBUTING.md).

## Licence

SushiRuntime is source-available and free for noncommercial use under the PolyForm Noncommercial
License 1.0.0; see [LICENSE](LICENSE). Commercial use needs a licence from Sushi Systems; see
[COMMERCIAL.md](COMMERCIAL.md). Third-party components keep their own licences, listed in
[NOTICE.md](NOTICE.md).
