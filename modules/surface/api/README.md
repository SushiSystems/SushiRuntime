# api

The fluent v1 API: data handles, launch geometry, the data-flow vocabulary, the graph builders and
the Runtime entry point.

| Folder | Page |
| --- | --- |
| `data/` | [data/README.md](include/SushiRuntime/api/data/README.md) |
| `detail/` | [detail/README.md](include/SushiRuntime/api/detail/README.md) |
| `graph/` | [graph/README.md](include/SushiRuntime/api/graph/README.md) |
| `interop/` | [interop/README.md](include/SushiRuntime/api/interop/README.md) |
| `launch/` | [launch/README.md](include/SushiRuntime/api/launch/README.md) |
| `runtime/` | [runtime/README.md](include/SushiRuntime/api/runtime/README.md) |
| `vocabulary/` | [vocabulary/README.md](include/SushiRuntime/api/vocabulary/README.md) |

`source/` holds the part of the layer that names an engine type; the section "The compiled
boundary" says what crosses into it.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

The module depends on `core`, `report`, `region`, `log`, `machine` and `interop`, the other
public modules, through its headers. Its sources depend on `memory`, `device`, `observability`,
`task`, `graph`, `execution` and `memory_exchange`.

## The compiled boundary

The headers under `include/` hold the templates a caller's kernel instantiates. Whatever names a
type of the engine is compiled into the library, from `source/`.

| Source | Holds |
| --- | --- |
| `context_handle.cpp` | `Detail::ContextAccess`, the way from a `ContextHandle` to the context |
| `runtime.cpp` | `Runtime`: building the context, the rebalancer switch and the wait |
| `advanced_controls.cpp` | `AdvancedControls`: every answer it reads from the context |
| `buffer.cpp` | The allocation of a `Buffer` and its window copies |
| `interop.cpp` | `Interop`: the capabilities, the host export and the device import |
| `recorder.cpp` | `Recorder::record()`, scratch, the fault records and the refusals that ask the runtime |
| `node_emitters.cpp` | One function per `NodeKind` that adds a described node to the engine's graph |
| `late_window.cpp` | The per-step controls of a late-bound node: creation, update and read |
| `plan_cache.cpp` | `PlanCache`: the build and the compiled plan of one recorder |
| `region_composer.cpp` | `RegionComposer`: the sub-graph of each region and the stitched plan |
| `step_run.cpp` | The scheduler loop, the chain loop and the report of a run |

A recording verb is a template. It wraps the caller's kernel once in the `std::function` the
engine runs and fills a `Detail::NodeDescription` (detail/README.md); `Recorder::record()` stores
it, and each plan build hands it to the function of its kind. The engine ran a node's work
through that same `std::function` type before the boundary existed, so a step calls what it
called then. The per-element path is a template inside the SYCL command group and is compiled
into the caller, as before.

A data handle caches its address and its size, so element access touches the handle alone.
Buffer, State, Runtime, AdvancedControls and Interop hold a `ContextHandle`
(runtime/README.md) and name no type of the engine.

A run entry is a template too, over the graph and over the stop test. It hands the library a
`Detail::StepTarget` and a `Detail::StepPredicate`, and the loops run in `step_run.cpp`.

## fluent.hpp

### Folder layout

fluent.hpp is the umbrella for the fluent v1 API, the surface most users program against. The fluent
layer is split by concern into folders of the same shape, and fluent.hpp is the single public
include that pulls them together.

| Folder | Holds |
| --- | --- |
| `data/` | The data handles (Buffer, State) |
| `launch/` | The iteration geometry (Extent, NdRange) |
| `vocabulary/` | The data-flow DSL (Reads/Writes, In/Out/InOut, Coupled, On, Dynamic) and LateCount |
| `graph/` | The chainable builders (Graph, Region, DynamicGraph), the Recorder they record onto, and RunHandle, the receipt for a step started asynchronously |
| `interop/` | The Buffer-typed side of memory pairing with another API: the Interop facade and HostExport |
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
void record_scaled_copy(Recorder& recorder, const Buffer<float>& in,
                        const Buffer<float>& out, std::size_t count, float factor)
{
    float* stage = recorder.scratch<float>(count, in.device_index(), in.residency()).value();
    const BufferRegion staged{stage, 0, count * sizeof(float)};
    const float* source = in.data();
    float* target = out.data();

    auto rule = recorder.rule();
    recorder.kernel(count, Access(Reads(in), Writes(staged)),
                    [source, stage, factor](std::size_t i) { stage[i] = source[i] * factor; });
    recorder.kernel(count, Access(Reads(staged), Writes(out)),
                    [stage, target](std::size_t i) { target[i] = stage[i]; });
}
```

A caller writes `record_scaled_copy(graph.recorder(), in, out, n, 2.0f);`, and the same call takes
`world.region(key).recorder()`.

### Promoted names

Each leaf header promotes the public names it declares into the SushiRuntime namespace, in a
closing `using` block. fluent.hpp promotes none. A program that includes
`SushiRuntime/sushiruntime.hpp` writes `SushiRuntime::Runtime`, `SushiRuntime::Buffer<T>`,
`SushiRuntime::Reads` and `SushiRuntime::RunReport`.

Two headers under `api/` declare nothing and only promote. `runtime/errors.hpp` promotes
`Error`, `ErrorCode` and `Result`; `runtime/logging.hpp`
promotes `LogLevel`, `set_log_level` and `set_log_sink`.

`OperationID` is declared in `SushiRuntime` and the `_op` literal in `SushiRuntime::Literals`, an
inline namespace, by `core/operation_id.hpp` of the core module, outside `api/`.
`graph/node_options.hpp` includes it. `TaskMetadata` is declared in
`SushiRuntime::Internal::Graph` by `task/task_types.hpp`. `RunReport` is declared in
`SushiRuntime` by `report/run_report.hpp` of the report module.

The graph type stays `Graph`, because `SushiRuntime::Internal::Graph` is the engine's namespace. `when`,
`sized`, `based_at` and their device forms stay in `API`, so `using namespace SushiRuntime` does not
bring `when` into scope.

## sushiruntime.hpp

`SushiRuntime/sushiruntime.hpp` includes `core/version.hpp` and `api/fluent.hpp`, and nothing else.
Up to v0.3 it also included seventeen engine headers. A program that used the scheduler, the
dependency tracker, epoch reclamation, hardware discovery or the logger through it now includes that
module's header.
