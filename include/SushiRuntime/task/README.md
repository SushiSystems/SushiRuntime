# task

The unit of work the scheduler dispatches and the graph node built on it.

`task` exists so that `scheduler` and `graph` both include it and neither includes the other:
`Graph::Node` derives from `Scheduler::Task`, and the scheduler's poller reads `Node` fields.
The declarations keep the namespaces they had before the module was split out.

## Depends on

| Module | Today's path | Used for |
| --- | --- | --- |
| `core` | `core/export.hpp`, `core/sushi_ptr.hpp`, `core/ref_counted.hpp` | Export macro, intrusive pointer, reference count |
| `memory` | `core/resource_region.hpp` | The USM regions a node reads and writes |
| `observability` | `core/dispatch_width.hpp` | `Core::WIDTH_UNKNOWN` |
| `sycl` | `sycl/sycl.hpp` | `sycl::event`, `sycl::handler`, `sycl::queue` |

`node.hpp` also includes `distributed/protocol.hpp` when `SR_DISTRIBUTED` is defined, for
`OffloadState`. That include points up the tier order; section 4 of
`docs/agent/2026_10_09_USABILITY_AUDIT/SPEC.md` replaces it.

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
| `node.hpp` | `Graph::SuccessorNode`, `Graph::Node`, and `Graph::OffloadState` and `Graph::NodeExtensions` |
| `node_widths.hpp` | `Graph::NodeWidths`, `Graph::snapshot_widths` (defined in `src/task/node_widths.cpp`) |

The notes on why each symbol is the way it is are in `include/SushiRuntime/graph/README.md` and
`include/SushiRuntime/scheduler/README.md`; each header's file block cites the page that holds its
notes.
