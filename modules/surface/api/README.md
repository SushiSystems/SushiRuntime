# api

The fluent v1 API: data handles, launch geometry, the data-flow vocabulary, the graph builders and
the Runtime entry point.

| Folder | Page |
| --- | --- |
| `data/` | [data/README.md](data/README.md) |
| `detail/` | [detail/README.md](detail/README.md) |
| `graph/` | [graph/README.md](graph/README.md) |
| `launch/` | [launch/README.md](launch/README.md) |
| `runtime/` | [runtime/README.md](runtime/README.md) |
| `vocabulary/` | [vocabulary/README.md](vocabulary/README.md) |

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## fluent.hpp

### Folder layout

fluent.hpp is the umbrella for the fluent v1 API, the surface most users program against. The fluent
layer is split by concern into folders of the same shape, and fluent.hpp is the single public
include that pulls them together.

| Folder | Holds |
| --- | --- |
| `data/` | The data handles (Buffer, State) |
| `launch/` | The iteration geometry (Extent, NdRange) |
| `vocabulary/` | The data-flow DSL (Reads/Writes, In/Out/InOut, Coupled, On, Dynamic), LateCount and the `_op` literal |
| `graph/` | The chainable builders (Graph, Region, DynamicGraph), the Recorder they record onto, and RunHandle, the receipt for a step started asynchronously |
| `runtime/` | The Runtime entry point, RuntimeConfig, AdvancedControls, and the error and logging names promoted for it |
| `detail/` | Callable traits, the replay callable of each node shape, the per-step StepMachinery, report building |

### Adding a node kind from a domain library

The runtime records and orders work and computes nothing of its own. A library above it adds
a node kind as a free function over the Recorder, which Graph::recorder() and Region::recorder()
return. `rule()` groups the nodes of one call under one registration index. `kernel(...)` records
a launch from a count, an Extent, an NdRange, a Dynamic or a DeviceSized, ordered by the Access it
is given. `scratch(...)` hands out USM the recorder frees with itself. `resolve(...)` turns
LateCounts into live words on the device, and a DeviceSized launch reads its width from such a
word. `graph/README.md` gives the contract of each.

```cpp
void record_scaled_copy(API::Recorder& recorder, const API::Buffer<float>& in,
                        const API::Buffer<float>& out, std::size_t count, float factor)
{
    float* stage = recorder.scratch<float>(count, in.device_index(), in.residency());
    const Memory::ResourceRegion staged{stage, 0, count * sizeof(float)};
    const float* source = in.data();
    float* target = out.data();

    auto rule = recorder.rule();
    recorder.kernel(count, API::Access(Reads(in), Writes(staged)),
                    [source, stage, factor](std::size_t i) { stage[i] = source[i] * factor; });
    recorder.kernel(count, API::Access(Reads(staged), Writes(out)),
                    [stage, target](std::size_t i) { target[i] = stage[i]; });
}
```

A caller writes `record_scaled_copy(graph.recorder(), in, out, n, 2.0f);`, and the same call takes
`world.region(key).recorder()`.

### Promoted names

Each leaf header promotes the public names it declares into the SushiRuntime namespace, in a
closing `using` block. fluent.hpp promotes none. A program that includes
`SushiRuntime/SushiRuntime.h` writes `SushiRuntime::Runtime`, `SushiRuntime::Buffer<T>`,
`SushiRuntime::Reads` and `SushiRuntime::RunReport`.

Three headers declare nothing and only promote. `runtime/errors.hpp` promotes `Error`, `Errc`,
`Result` and `SushiException`; `runtime/logging.hpp` promotes `LogLevel`, `set_log_level` and
`set_log_sink`; `task/task_types.hpp` of the task module promotes the `_op` literal, `OpID` and `TaskMetadata`. `RunReport` is promoted by
`observability/run_report.hpp`, the header that declares it.

The graph type stays `API::Graph`, because `SushiRuntime::Graph` is the engine's namespace. `when`,
`sized`, `based_at` and their device forms stay in `API`, so `using namespace SushiRuntime` does not
bring `when` into scope.

## SushiRuntime.h

`SushiRuntime/SushiRuntime.h` includes `core/version.hpp` and `api/fluent.hpp`, and nothing else.
Up to v0.3 it also included seventeen engine headers. A program that used the scheduler, the
dependency tracker, epoch reclamation, hardware discovery or the logger through it now includes that
module's header.
