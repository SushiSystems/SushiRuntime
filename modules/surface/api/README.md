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
| `vocabulary/` | The data-flow DSL (Reads/Writes, In/Out/InOut, Dynamic) and LateCount |
| `graph/` | The chainable builders (Graph, Region, DynamicGraph), the Recorder they record onto, and RunHandle, the receipt for a step started asynchronously |
| `runtime/` | The Runtime entry point and RuntimeConfig |
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
    const Core::ResourceRegion staged{stage, 0, count * sizeof(float)};
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

fluent.hpp promotes the entry points into the SushiRuntime namespace so users write
`Runtime::create()` after `using namespace SushiRuntime;`. The graph type stays API::Graph (reached
through auto) because the name Graph already denotes a namespace at this scope. The data and launch
handles carry their own promotions in their leaf headers.
