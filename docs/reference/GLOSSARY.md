# Glossary

The words this repository's documents use. A term defined in code names the header that holds it.

## The runtime

| Word | Meaning |
| --- | --- |
| Fluent API | The public surface under `modules/surface/api/include/SushiRuntime/api/`: `Runtime`, `Buffer`, `State`, `Graph`, `DynamicGraph`. A user who stays on it never names a lower layer |
| Task | One unit of work in a graph. It declares the memory it reads and writes; the runtime derives the order |
| Node | A task as the graph stores it, with its successors, in-degree and metadata (`graph/node.hpp`) |
| Hazard | A pair of accesses to the same memory that fixes an order between two tasks: read-after-write (RAW), write-after-read (WAR) or write-after-write (WAW) |
| Plan | A compiled graph. It is built once and replayed every step; its topology is fixed |
| Sentinel | The completion node `TaskGraph::compile()` links every leaf to. A run ends when it fires |
| Late-bound value | A size, base or enable flag a plan leaves open and resolves at each run, on the host or from a value a kernel wrote on the device |
| Residency | Where a `Buffer`'s memory lives: `Shared` (host-addressable USM, the default), `Device` (device USM, host access through `read`/`write_range`) or `Host` (pinned host USM, readable while kernels run) |
| USM | SYCL unified shared memory: allocations addressed by plain pointers |
| USM registry | `Memory::UsmRegistry`, the record of which device owns each USM allocation and how long it is |
| EBR | Epoch-based reclamation: freeing memory other threads may still read, without locks (`memory/epoch_reclamation.hpp`) |
| Region | One partition of a `DynamicGraph`, keyed by a `RegionKey`. It compiles on its own and is added or dropped between steps |
| Chain | The in-order path that runs a chainable plan from the calling thread, without the scheduler. Layer 1 of the graph replay design |
| Layer | Which path ran a step, reported in `RunReport::layer` (`Core::ExecutionLayer`) |
| Registration index | The number a task takes when it is added; the nodes of one `Recorder::rule()` scope share it. `RunReport::node_events` is numbered by it |
| Node event record | `Core::NodeEventRecord`: the submit, start and end times of one dispatch, collected when a graph requests profiling |
| Clock pairing | `Core::ClockPairing`: an event-clock reading and a steady-clock reading taken together at a run's first device submission, used to place host and device records on one axis |
| Profiling queue | A second queue per device, built on first use with profiling enabled, that a graph requesting profiling dispatches through |
| Offload | Sending one task to a remote worker through the optional distributed layer |
| `OpID` | A 32-bit FNV-1a hash of an operation's name. It is what crosses the wire in place of code (`graph/task_types.hpp`) |
| Master, worker | The two roles of the distributed layer. The master owns the graph and the buffers; a worker runs the ops it registered |
| Co-tenancy | The runtime sharing a machine with an engine's own threads: `RuntimeConfig` fixes its worker count, first core and pinning |
| Interop | `API::Interop`: exporting host memory and importing device memory another API allocated |

## Build and tools

| Word | Meaning |
| --- | --- |
| `sr` | This repository's command line, the package under `cli/`. It configures, builds, tests, runs and packages |
| `hub` | SushiStack's command line, which provisions several checkouts at once |
| sushicore | The Python package the `sr` CLI takes its shared commands and help page from |
| Toolchain | One of the three SYCL compilers `SR_SYCL_TOOLCHAIN` selects: intel-llvm, AdaptiveCpp or Intel oneAPI |
| Suite | A set of tests `sr test --suite` runs: `functional` (unit, regression, integration), `benchmark` (stress, performance), `package`, or `all` |
| Package suite | The suite that installs the runtime to a prefix, then builds and runs `tests/package/` with only that prefix on the path |
| Lane | One build configuration that is tested as a unit, such as the ASan build or the CPU-only build |
| Checker | A script under `tools/` that reads the repository and reports where it breaks a rule; it never rewrites a file |

## Planning codes

| Word | Meaning |
| --- | --- |
| BB-0 to BB-8 | The work packages of `docs/design/ENGINE_BACKBONE_REFACTOR.md` |
| WP-0 to WP-7 | The work packages of `docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md` |
| R1 to R9 | The requests of `docs/design/PHYSICS_SUBSTRATE_REQUIREMENTS.md` |
| P0 to P3 | The phases of `docs/design/GRAPH_REPLAY.md` |
| G1 | The gate that added node event records and the per-graph profiling switch; its design is in `docs/archive/superpowers/specs/` |
| L1 to L17 | The owner decisions locked in §0.1 of the simulation substrate plan |
| Work folder | `docs/agent/<YYYY_MM_DD>_<WORK_NAME>/`, holding the `SPEC.md`, `PLAN.md` and `REPORT.md` of one piece of agent work |
