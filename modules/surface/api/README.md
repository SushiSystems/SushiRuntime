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
| `vocabulary/` | The data-flow DSL (Reads/Writes, In/Out/InOut, Dynamic) and the reduction combiners (Sum, Minimum, Maximum) |
| `graph/` | The chainable builders (Graph, Region, DynamicGraph) and RunHandle, the receipt for a step started asynchronously |
| `runtime/` | The Runtime entry point and RuntimeConfig |
| `detail/` | Callable traits, the per-step StepMachinery, report building |

### Promoted names

fluent.hpp promotes the entry points into the SushiRuntime namespace so users write
`Runtime::create()` after `using namespace SushiRuntime;`. The graph type stays API::Graph (reached
through auto) because the name Graph already denotes a namespace at this scope. The data and launch
handles carry their own promotions in their leaf headers.
