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
| `late_bindings.hpp` | `Internal::Graph::LateBindings`, with the `replay_words` a recording hands a late-bound node |
| `submit_form.hpp` | `Internal::Graph::SubmitForm`, what a recorded command graph may keep of a node's submission (`docs/design/GRAPH_REPLAY/README.md`) |
| `small_event_vector.hpp` | `Internal::Graph::SmallEventVector` |
| `region_link.hpp` | `Internal::Graph::RegionLink` |
| `node_profiling.hpp` | `Internal::Graph::NodeProfiling` |
| `node.hpp` | `Internal::Graph::SuccessorNode`, `Internal::Graph::Node` |
| `offload_state.hpp` | `Internal::Graph::OffloadPayload`, `Internal::Graph::OffloadState`, the per-node record `Node::offload_state_` points to |
| `offload_sink.hpp` | `Internal::Graph::OffloadSink`, through which a coordinator returns nodes to the scheduler |
| `offload_coordinator.hpp` | `Internal::Graph::OffloadCoordinator`, the seam an offload layer implements |
| `in_memory_offload_coordinator.hpp` | `Internal::Graph::InMemoryOffloadCoordinator`, a test double of the seam that records its calls |
| `in_memory_offload_sink.hpp` | `Internal::Graph::InMemoryOffloadSink`, a test double of the sink that records the nodes it takes back |
| `refusing_offload_sink.hpp` | `Internal::Graph::RefusingOffloadSink`, a test double of the sink that refuses every resubmitted node |
| `recording_task.hpp` | `Internal::Scheduler::RecordingTask`, a host task that counts its runs and completions and submits nothing |
| `node_widths.hpp` | `Internal::Graph::NodeWidths`, `Internal::Graph::snapshot_widths` (defined in `modules/engine/task/source/node_widths.cpp`) |
| `task_submission.hpp` | `Internal::Graph::submit_task`, `Internal::Graph::submit_task_discarding` (defined in `modules/engine/task/source/task_submission.cpp`): submit one task behind its event dependencies, the one path `ChainRun` and `TaskScheduler` share |
| `dispatch_events.hpp` | `Internal::DispatchEvents::DispatchEventScope`, the thread-local scope that collects the events a task body hands over |
| `node_placement.hpp` | `Internal::Graph::K_DEFAULT_PLACEMENT`, `Internal::Graph::placement_of`, `Internal::Graph::anchor_of` and `Internal::Graph::anchor_device` (defined in `modules/engine/task/source/node_placement.cpp`): the one anchor rule, the handle a node is anchored to and its owning device, and the device a node runs on |
| `handle_location.hpp` | The memory space of each device (host 0, device i at i + 1), `locate()`, the lowest device instance of a handle, and `resolve_on_device()`, the pointer a node dispatched on a device works on; both read a `HandleResolver` only |
| `kernel_pointers.hpp` | `Internal::Graph::KernelPointerSlot`, `Internal::Graph::KernelPointers` and `Internal::Graph::KernelPointerScope` (defined in `modules/engine/task/source/kernel_pointers.cpp`): the pointers a node's kernel receives, bound on the node's device at each dispatch |
| `placement_memory.hpp` | `Internal::Graph::PlacementMemory`, the memory facts `placement_of()` and the chain classifier read: the device of a handle's buffer, the device count and the shared contexts |
| `usm_placement_memory.hpp` | `Internal::Graph::USMPlacementMemory` (defined in `modules/engine/task/source/usm_placement_memory.cpp`), the port answered from a `HandleResolver` and a `USMAllocator` |
| `in_memory_placement_memory.hpp` | `Internal::Graph::InMemoryPlacementMemory`, a test double of the port that answers from handles a test records |

## Metadata of a node

`TaskMetadata` carries the label (`name`), the operation, the placement class and key, the
determinism contract and an optional `NodeCost`. The cost is absent unless the recorder stated one;
the graph module copies it into the plan's node table.

## The host form of a launch

`Node::host_form` holds the launch as a loop on the calling thread: the recorder fills it for a
launch over a count or an `Extent`, whose kernel takes an index the host can build. The host
runtime's engine runs it in place of `work`, which needs a `sycl::handler`. The engine calls
`Node::run_on_host()`, which runs a native host node's work or the host form, returns what either
threw as an `Error`, and refuses a node with an `NdRange`, a raw command group or a queue body with
`Unsupported`; the refusal lives with the node kinds. The device engines never read it.

## The events a body hands over

A host-work body that submits several kernels returns one event, which the successors wait on, and
hands each kernel's event to the dispatcher with `note_dispatch_event()` (declared in
`modules/surface/api/include/SushiRuntime/api/graph/dispatch_events.hpp`). The dispatcher opens a
`DispatchEventScope` around the body only while the plan records device timing; the scope collects
into a vector on the calling thread, and `note()` does nothing when no scope is open. Scopes nest and
restore the outer one. The pointer is a `thread_local` defined once in `source/dispatch_events.cpp` and
exported, so the library and every program linked to it see one instance; an inline definition
would give each DLL its own. The returned event must complete after every handed-over event, since
the dispatch is timed across the handed-over ones and completes with the returned one. The body
signature and `execute_host()` are unchanged.

## The pointers a kernel receives

A capture launch records each buffer's home address at `add()`. When the coherence planner runs the
node on another device, the kernel must read the instance there. `Node::kernel_pointers_` holds one
`KernelPointerSlot` per pointer: the buffer's handle, the home address and the home device.
`Node::execute()` calls `KernelPointers::bind()` with the node's device, `cached_queue_index_`, and
opens a `KernelPointerScope` around the work; the capture launch's command group reads the table
from the scope while it is built. A node with no table, or not yet placed, runs its work as before.

`bind()` keeps the home address of a slot whose home device is the node's device and reads no
directory. Any other slot is resolved with `resolve_on_device()`, which is lock-free; a buffer with
no instance there keeps its home address, which is how host, borrowed and shared memory is reached.
`home_binds()` and `resolved_binds()` count the two cases. One thread dispatches a node at a time, so
the table takes no lock. The scope's pointer is a `thread_local` defined once in
`source/kernel_pointers.cpp` and exported, for the reason given for `DispatchEventScope`.

## Placement rule order

`placement_of()` returns the device a node runs on, or a refusal, in this order:

1. A stated device (`Node::place_on()`) on a node whose first write region, else first read
   region, a device owns: the stated device when its queue shares a SYCL context with the
   owner's; otherwise an `ErrorCode::InvalidGraph` error. The node is never moved silently.
2. No stated device: the owner.
3. No owner: the stated device, else `K_DEFAULT_PLACEMENT`.

The function needs no queue. It reads both answers through `PlacementMemory`, which is asked about
a region's `GlobalHandle`; the form that takes a `USMAllocator` and a `HandleResolver` wraps them
in `USMPlacementMemory`. Its `owning_device()` is the lowest device the handle resolves to an
instance on, read from the resolver alone; the allocator keeps no address table and answers only
`shares_context()`. A test passes
`InMemoryPlacementMemory` and needs no device. The scheduler's `place_for_dispatch()` passes the refusal on and
refuses an index past the device count; `modules/engine/scheduler/README.md` holds why.

The notes on why each symbol is the way it is are in `modules/engine/graph/README.md` and
`modules/engine/scheduler/README.md`; each header's file block cites the page that holds its
notes.

## refusing_offload_sink.hpp

`RefusingOffloadSink` is the in-memory adapter of `OffloadSink` for the path where the sink refuses a
node: `resubmit()` counts the call, in `refusals()`, and throws `std::runtime_error`; the other two calls
take their node and do nothing. It is a test double beside the port it implements.

## Designs

<!-- designs:begin -->
- [LAYERED_EXECUTION](../../../docs/design/LAYERED_EXECUTION/README.md): What SushiRuntime adds so that SushiBLAS, SushiAI and SushiEngine can say where their work runs without writing it into their code: a placement vocabulary on every node, a description of...
<!-- designs:end -->
