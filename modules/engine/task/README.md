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
pointer, null until `Node::set_live_io()` or a coordinator's `prepare()` fills it.
`OffloadCoordinator::prepare()` takes a compiled graph's nodes and the rules of the placement
snapshot the graph compiled against, and stores those rules in `OffloadState::rules`, the set the
node's `TaskMetadata::placement_rule` points into. What a coordinator needs beyond the seam's
fields it stores behind `OffloadState::payload`.

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
| `task_submission.hpp` | `Internal::Graph::submit_task`, `Internal::Graph::submit_task_discarding` (defined in `modules/engine/task/source/task_submission.cpp`): submit one task behind its event dependencies, the one path `ChainRun` and `TaskScheduler` share |
| `dispatch_events.hpp` | `Internal::DispatchEvents::DispatchEventScope`, the thread-local scope that collects the events a task body hands over |
| `node_placement.hpp` | `Internal::Graph::K_DEFAULT_PLACEMENT`, `Internal::Graph::placement_of` (defined in `modules/engine/task/source/node_placement.cpp`), the device a node runs on |

## Metadata of a node

`TaskMetadata` carries the label (`name`), the operation, the placement class and key, the
determinism contract and an optional `NodeCost`. The cost is absent unless the recorder stated one;
the graph module copies it into the plan's node table.

## The events a body hands over

A host-work body that submits several kernels returns one event, which the successors wait on, and
hands each kernel's event to the dispatcher with `note_dispatch_event()` (declared in
`modules/surface/api/include/SushiRuntime/api/graph/dispatch_events.hpp`). The dispatcher opens a
`DispatchEventScope` around the body only while the plan records device timing; the scope collects
into a vector on the calling thread, and `note()` does nothing when no scope is open. Scopes nest and
restore the outer one. The pointer is a `thread_local` inside an inline function, so every module of
the library sees one instance. The returned event must complete after every handed-over event, since
the dispatch is timed across the handed-over ones and completes with the returned one. The body
signature and `execute_host()` are unchanged.

## Placement rule order

`placement_of()` returns the device a node runs on, or a refusal, in this order:

1. A stated device (`Node::place_on()`) on a node whose first write region, else first read
   region, a device owns: the stated device when its queue shares a SYCL context with the
   owner's; otherwise an `ErrorCode::InvalidGraph` error. The node is never moved silently.
2. No stated device: the owner.
3. No owner: the stated device, else `K_DEFAULT_PLACEMENT`.

The allocator answers both questions, `owning_device()` and `shares_context()`, so the
function needs no queue. The scheduler's `place_for_dispatch()` passes the refusal on and
refuses an index past the device count; `modules/engine/scheduler/README.md` holds why.

The notes on why each symbol is the way it is are in `modules/engine/graph/README.md` and
`modules/engine/scheduler/README.md`; each header's file block cites the page that holds its
notes.

## Designs

<!-- designs:begin -->
- [LAYERED_EXECUTION](../../../docs/design/LAYERED_EXECUTION/README.md): What SushiRuntime adds so that SushiBLAS, SushiAI and SushiEngine can say where their work runs without writing it into their code: a placement vocabulary on every node, a description of...
<!-- designs:end -->
