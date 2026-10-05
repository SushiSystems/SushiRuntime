# src

The translation units of SushiRuntime, one folder per layer. The public headers are in
`include/SushiRuntime/`, under the same folder names.

Each folder's README records the reasoning behind its sources: why an order matters, a
measured cost, a hardware or toolchain quirk, a failure that happened. A source comment
states the invariant in one line and cites the README by path.

| Folder | Holds | README |
| --- | --- | --- |
| `core/` | The compiled-in library version | [core/README.md](core/README.md) |
| `distributed/` | Master coordinator, worker agent, transports, registries | [distributed/README.md](distributed/README.md) |
| `execution/` | Runtime context, execution engine, in-order chain | [execution/README.md](execution/README.md) |
| `graph/` | Task graph and dependency tracker | [graph/README.md](graph/README.md) |
| `interop/` | Entry points behind the `Interop` facade | [interop/README.md](interop/README.md) |
| `scheduler/` | Worker pool, work stealing, offload branch | [scheduler/README.md](scheduler/README.md) |
| `topology/` | Device discovery and NUMA mapping | [topology/README.md](topology/README.md) |

`memory/` holds no source file.
