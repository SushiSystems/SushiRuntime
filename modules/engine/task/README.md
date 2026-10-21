# task

The unit of work the scheduler dispatches and the graph node built on it.
The module is internal: its headers are for the other modules of the library and are not installed.

`task` exists so that `scheduler` and `graph` both include it and neither includes the other:
`Internal::Graph::Node` derives from `Internal::Scheduler::Task`, and the scheduler's poller reads `Node` fields.

## Depends on

| Module | Today's path | Used for |
| --- | --- | --- |
| `core` | `core/export.hpp`, `core/sushi_ptr.hpp`, `core/ref_counted.hpp`, `core/assert.hpp` | Export macro, intrusive pointer, reference count, the check `Node::place_on()` makes |
| `region` | `region/buffer_region.hpp` | The regions a node reads and writes |
| `memory` | `memory/usm_allocator.hpp` | Which device owns a region |
| `report` | `report/dispatch_width.hpp` | `K_WIDTH_UNKNOWN` |
| `sycl` | `sycl/sycl.hpp` | `sycl::event`, `sycl::handler`, `sycl::queue` |

No header here changes shape with a build flag. A node carries one `Internal::Graph::OffloadState`
pointer, null until `Node::set_live_io()` or a coordinator's `prepare()` fills it,
and what a coordinator needs beyond the seam's fields it stores behind
`OffloadState::payload`.

## Public headers

| Header | Declares |
| --- | --- |
| `task.hpp` | `Internal::Scheduler::Task` |
| `ready_sink.hpp` | `Internal::Scheduler::ReadySink` |
| `task_types.hpp` | `Internal::Graph::TaskType`, `Internal::Graph::TaskMetadata` |
| `task_work.hpp` | `Internal::Graph::TaskWork`, `Internal::Graph::HostWork` |
| `late_bindings.hpp` | `Internal::Graph::LateBindings` |
| `small_event_vector.hpp` | `Internal::Graph::SmallEventVector` |
| `region_link.hpp` | `Internal::Graph::RegionLink` |
| `node_profiling.hpp` | `Internal::Graph::NodeProfiling` |
| `node.hpp` | `Internal::Graph::SuccessorNode`, `Internal::Graph::Node` |
| `offload_state.hpp` | `Internal::Graph::OffloadPayload`, `Internal::Graph::OffloadState`, the per-node record `Node::offload_state_` points to |
| `offload_sink.hpp` | `Internal::Graph::OffloadSink`, through which a coordinator returns nodes to the scheduler |
| `offload_coordinator.hpp` | `Internal::Graph::OffloadCoordinator`, the seam an offload layer implements |
| `node_widths.hpp` | `Internal::Graph::NodeWidths`, `Internal::Graph::snapshot_widths` (defined in `modules/engine/task/source/node_widths.cpp`) |
| `node_placement.hpp` | `Internal::Graph::K_DEFAULT_PLACEMENT`, `Internal::Graph::placement_of` (defined in `modules/engine/task/source/node_placement.cpp`), the device a node runs on |

The notes on why each symbol is the way it is are in `modules/engine/graph/README.md` and
`modules/engine/scheduler/README.md`; each header's file block cites the page that holds its
notes.

## Designs

<!-- designs:begin -->
- [LAYERED_EXECUTION](../../../docs/design/LAYERED_EXECUTION/README.md): What SushiRuntime adds so that SushiBLAS, SushiAI and SushiEngine can say where their work runs without writing it into their code: a placement vocabulary on every node, a description of...
<!-- designs:end -->
