# Glossary

The words this repository's documents use. A term defined in code names the header that holds it.

## The runtime

| Word | Meaning |
| --- | --- |
| Fluent API | The public surface under `modules/surface/api/include/SushiRuntime/api/`: `Runtime`, `Buffer`, `State`, `Graph`, `DynamicGraph`. A consumer includes it through `<SushiRuntime/sushiruntime.hpp>` |
| Public module | A module whose headers are installed and whose names are in `SushiRuntime`: `core`, `report`, `region`, `log`, `machine`, `interop`, `api`, and `distributed` when it is built |
| Internal module | A module marked `INTERNAL`: its headers are not installed and its names are in `SushiRuntime::Internal::<Module>` |
| Rule | What one recording verb adds to a graph: one node, or the nodes an extension records together |
| Refusal | The `Error` a recorder keeps when it refuses a recording; `refusal()` reads it and the next run returns it |
| Task | One unit of work in a graph. It declares the memory it reads and writes; the runtime derives the order |
| Node | A task as the engine's graph stores it, with its successors and in-degree (`task/node.hpp`, internal) |
| Hazard | A pair of accesses to the same memory that fixes an order between two tasks: read-after-write (RAW), write-after-read (WAR) or write-after-write (WAW) |
| Plan | A compiled graph. It is built once and replayed every step; its topology is fixed |
| Sentinel | The completion node the engine links every leaf of a plan to. A scheduled run ends when it fires |
| Late-bound value | A size, base or enable flag a plan leaves open and resolves at each run, on the host or from a value a kernel wrote on the device |
| Residency | Where a `Buffer`'s memory lives: `Shared` (host-addressable USM, the default), `Device` (device USM, host access through `read_range` and `write_range`) or `Host` (pinned host USM, readable while kernels run) |
| USM | SYCL unified shared memory: allocations addressed by plain pointers |
| USM registry | `USMRegistry` of the internal `memory` module, the record of which device owns each USM allocation and how long it is |
| EBR | Epoch-based reclamation: freeing memory other threads may still read, without locks (`memory/epoch_reclamation.hpp`, internal) |
| Region | One partition of a `DynamicGraph`, keyed by a `RegionKey`. It compiles on its own and is added or dropped between steps |
| Chain | The in-order path that runs a chainable plan from the calling thread, without the scheduler |
| Layer | Which path ran a step, reported in `RunReport::layer` (`ExecutionLayer`) |
| Registration index | The number a task takes when it is added; the nodes of one `Recorder::rule()` scope share it. `RunReport::node_events` is numbered by it |
| Node event record | `NodeEventRecord`: the submit, start and end times of one dispatch, collected when a graph requests profiling |
| Clock pairing | `ClockPairing`: an event-clock reading and a steady-clock reading taken together at a run's first device submission; it places host and device records on one axis |
| Profiling queue | A second queue per device, built on first use with profiling enabled, that a graph requesting profiling dispatches through |
| Offload | Sending one task to a remote worker through the optional distributed layer |
| `OperationID` | A 32-bit FNV-1a hash of an operation's name, written with the `_op` literal. It is what crosses the wire in place of code (`core/operation_id.hpp`) |
| Master, worker | The two roles of the distributed layer. The master owns the graph and the buffers; a worker runs the ops it registered |
| Cluster | One master and the workers registered with it. `Distributed::Cluster` is the handle `Cluster::attach` returns (`distributed/cluster.hpp`) |
| Kernel (distributed) | The function a worker runs for an `OperationID`. It receives the task's buffers as `BufferView`s and returns a SYCL event (`distributed/kernel_registry.hpp`) |
| Kernel registry | `Distributed::KernelRegistry`, the map from `OperationID` to kernel that is compiled into a worker's program |
| Local fallback | The kernel given to the verb that recorded a node; it runs on the master when the node is not offloaded or is taken back |
| Shared buffer | A USM allocation the master has made known to its workers, through `Cluster::share()`. Only a task whose buffers are all shared can be offloaded |
| Buffer handle | `Distributed::BufferHandle`: the id, version and size that name a shared buffer on the wire (`distributed/buffer_handle.hpp`) |
| Buffer version | A number the master raises on every write to a shared buffer. A worker's cached copy at an older version is pulled again |
| Pull | A worker asking the master for the bytes of one buffer version it does not hold |
| Chunk | One frame's worth of a buffer's bytes, 256 KiB by default. A buffer crosses as a run of chunks |
| Frame | One protocol message on the wire: a 12-byte header and a payload of at most 1 MiB |
| Transport | What carries frames between a master and its workers: `Distributed::Transport`, implemented over TCP and over an in-process fabric (`distributed/transport.hpp`) |
| Fabric | `Distributed::InProcessFabric`, which joins a master and workers inside one process for tests |
| Peer | One connection of a transport, named by a `PeerID` that is never reused. A worker that reconnects is a new peer |
| Shared secret | The key file a master and its workers hold in common, at least 32 bytes (`distributed/shared_secret.hpp`) |
| Handshake | The four frames (`HELLO`, `CHALLENGE`, `PROOF`, `WELCOME`) by which a master and a worker prove the shared secret to each other and the worker registers |
| Session | One master process as its workers know it. A worker that registers with a new session empties its buffer cache |
| Offload policy | `Distributed::OffloadPolicy`, which picks the worker of each offloadable task, or none (`distributed/offload_policy.hpp`) |
| Liveness timeout | The silence after which the master closes a worker, and a worker its master; 5 s by default |
| Task deadline | The longest a kernel may run on a worker before the master takes the task back; 30 s by default, 0 for no bound |
| Heartbeat | The frame a worker sends every second with its queue depth, its cache size and the oldest task it still holds |
| Reap | The master taking back one task whose worker is still registered: past its deadline, never confirmed, or absent from the worker's heartbeats |
| Redispatch | A task taken back from a worker and resubmitted to the scheduler, counted in `ClusterStatistics::redispatched` |
| Stale writeback | An output chunk the master refuses because its task is not out on a worker or its version is old |
| Co-tenancy | The runtime sharing a machine with an engine's own threads: `RuntimeConfig` fixes its worker count, first core and pinning |
| Interop | `Interop`, which `Runtime::interop()` returns: exporting host memory and importing device memory another API allocated |

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
