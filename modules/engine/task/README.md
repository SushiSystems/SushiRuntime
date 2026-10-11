# task

The unit of work the scheduler dispatches and the graph node built on it.

`task` exists so that `scheduler` and `graph` both include it and neither includes the other:
`Graph::Node` derives from `Scheduler::Task`, and the scheduler's poller reads `Node` fields.

## Depends on

| Module | Today's path | Used for |
| --- | --- | --- |
| `core` | `core/export.hpp`, `core/sushi_ptr.hpp`, `core/ref_counted.hpp`, `core/error.hpp` | Export macro, intrusive pointer, reference count, the error `Node::place_on()` throws |
| `memory` | `memory/resource_region.hpp`, `memory/usm_allocator.hpp` | The USM regions a node reads and writes; which device owns a region |
| `observability` | `observability/dispatch_width.hpp` | `Observability::WIDTH_UNKNOWN` |
| `sycl` | `sycl/sycl.hpp` | `sycl::event`, `sycl::handler`, `sycl::queue` |

No header here changes shape with a build flag. A node carries one `Graph::OffloadState`
pointer, null until `TaskGraph::add_offload_dynamic()` or a coordinator's `prepare()` fills it,
and what a coordinator needs beyond the seam's fields it stores behind
`OffloadState::payload`.

## Public headers

| Header | Declares |
| --- | --- |
| `task.hpp` | `Scheduler::Task` |
| `ready_sink.hpp` | `Scheduler::ReadySink` |
| `task_types.hpp` | `Graph::TaskType`, `Graph::OpID`, `Graph::Literals::operator""_op`, `Graph::TaskMetadata` |
| `task_work.hpp` | `Graph::TaskWork`, `Graph::HostWork` |
| `late_bindings.hpp` | `Graph::LateBindings` |
| `small_event_vector.hpp` | `Graph::SmallEventVector` |
| `region_link.hpp` | `Graph::RegionLink` |
| `node_profiling.hpp` | `Graph::NodeProfiling` |
| `node.hpp` | `Graph::SuccessorNode`, `Graph::Node` |
| `offload_state.hpp` | `Graph::OffloadPayload`, `Graph::OffloadState`, the per-node record `Node::offload_state_` points to |
| `offload_sink.hpp` | `Graph::OffloadSink`, through which a coordinator returns nodes to the scheduler |
| `offload_coordinator.hpp` | `Graph::OffloadStatistics`, `Graph::OffloadCoordinator`, the seam an offload layer implements |
| `node_widths.hpp` | `Graph::NodeWidths`, `Graph::snapshot_widths` (defined in `modules/engine/task/source/node_widths.cpp`) |
| `node_placement.hpp` | `Graph::DEFAULT_PLACEMENT`, `Graph::placement_of` (defined in `modules/engine/task/source/node_placement.cpp`), the device a node runs on |

The notes on why each symbol is the way it is are in `modules/engine/graph/README.md` and
`modules/engine/scheduler/README.md`; each header's file block cites the page that holds its
notes.
