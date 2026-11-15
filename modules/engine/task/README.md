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
| `memory` | `memory/usm_allocator.hpp`, `memory/residency_view.hpp` (private) | Which device owns a region; the entry kind that makes a kernel pointer's home host visible |
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
| `placed_split.hpp` | `Internal::Graph::PlacedSplit` and `Internal::Graph::PlacedSplitAccess`: the parts a partitioner cut a node into, each part's device index, axis range and bytes |
| `split_dispatch.hpp` | `Internal::Graph::SplitDispatchPart` and `Internal::Graph::dispatch_split` (defined in `modules/engine/task/source/split_dispatch.cpp`): run a split node's parts, each with its device's queue and the node's pointers bound there |
| `kernel_pointers.hpp` | `Internal::Graph::KernelAddressing`, `Internal::Graph::KernelPointerSlot`, `Internal::Graph::home_is_host_visible`, `Internal::Graph::make_kernel_pointer_slot`, `Internal::Graph::KernelPointers` and `Internal::Graph::KernelPointerScope` (defined in `modules/engine/task/source/kernel_pointers.cpp`): whether a node may leave its home device, and the pointers a node's kernel receives, bound on the node's device at each dispatch |
| `placement_memory.hpp` | `Internal::Graph::PlacementMemory`, the memory facts `placement_of()` and the chain classifier read: the device of a handle's buffer, the device count, the shared contexts and whether a handle's home is host visible |
| `usm_placement_memory.hpp` | `Internal::Graph::USMPlacementMemory` (defined in `modules/engine/task/source/usm_placement_memory.cpp`), the port answered from the residency directory (`ResidencyView`) and a `USMAllocator` |
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
directory. Any other slot is resolved with `resolve_on_device()`, which is lock-free. A buffer with
no instance there keeps its home address only when its slot is host visible; otherwise `bind()`
fails its `SR_VERIFY` with a message naming the handle's slot and generation and the device, since
the coherence plan should have copied the buffer there. `home_binds()` and `resolved_binds()` count
the two cases.

`make_kernel_pointer_slot()` builds a slot from the residency directory once, when the node is
built, so `bind()` reads no entry kind. The home device is the lowest device whose instance is the
recorded address. The home is host visible when the directory holds the home instance (its bit is
set in `valid_spaces`) and one of these is true:

- the home is the host space;
- the home instance's kind is `USMKind::Host` or `USMKind::Shared`;
- the kind is unknown and the handle carries `HandleFlags::Borrowed`. The runtime is not told the
  kind of borrowed caller memory, and whoever borrowed it vouches that the devices reach it.

A planned buffer with no home instance yet, and device USM, are not host visible.

A `Dynamic`'s device count and device base slots go through the same table: the recorder appends
the count slot's buffer, then the base slot's, to the node's pointers, and `read_late_window()`
reads both addresses from the table the dispatching node bound. Those slots are `Residency::Host`,
so on another device they keep their home address legitimately.

### The parts of a split node

`dispatch_split()` runs a `NodeSplit` over its parts in slice order. For each part it calls
`KernelPointers::bind()` with the part's device and hands the slice the part, that device's queue
and the bound table as a `span<void* const>`. The next bind overwrites the table, so the span is
valid during the call only and a kernel copies the pointers it reads. A node with no table gives
an empty span. The combine, when the split has one, then receives the parts in slice order. The
queue of a device comes from the caller, so the function reads no device set.

`Node::placed_split` holds the parts `apply_partition()` gave a node, null for a node that runs
whole. `PlacedSplit::parts` is contiguous so the step driver hands it to `dispatch_split()` as is, with
`Node::kernel_pointers()`, the narrow accessor of the node's table. `PlacedSplit::access` holds the
bytes each part reads and writes, which the coherence planner plans part by part.

### Which nodes may leave their home device

`Node::kernel_addressing` says whether a node's kernel may run away from the home of its data.
It defaults to `KernelAddressing::FixedToHome`. The node builder in the API sets it to
`Rebindable` only for a capture launch, whose description sets `NodeDescription::rebindable`,
because every address that kernel reads comes from its table. A launch, a raw command group, an
untracked node, a task body, a host body, a `State` rule and a dynamic node capture raw addresses
in their own closures and stay fixed. The flag is never inferred from the table: a dynamic node
has a table for its device slots and is still fixed. A fixed node may still run on another
device where its raw addresses hold; "Placement rule order" says where.

`home_is_host_visible()` holds the host-visibility rule above. `make_kernel_pointer_slot()`,
`USMPlacementMemory::host_visible()` and the API's coherence planning
(`modules/surface/api/source/region_coherence.cpp`) all call it, so the rule is written once. One thread dispatches a node at a time, so
the table takes no lock. The scope's pointer is a `thread_local` defined once in
`source/kernel_pointers.cpp` and exported, for the reason given for `DispatchEventScope`.

## Placement rule order

`placement_of()` returns the device a node runs on, or a refusal, in this order:

1. A stated device (`Node::place_on()`) on a node whose first write region, else first read
   region, a device owns: the stated device when the node is `KernelAddressing::Rebindable`, whose
   table rebinds each captured buffer to the instance on the stated device while the coherence
   planner copies the regions there. A node `FixedToHome` runs where its raw addresses are valid:
   on the stated device when it shares the owner's SYCL context and no region's home is device
   USM of another device. Otherwise it gets an `ErrorCode::InvalidGraph` error naming the device
   that holds the memory, "in another SYCL context" or "as device USM", and the raw addresses.
   A region's home counts as device USM when `PlacementMemory::host_visible()` says no; that port
   answers through `home_is_host_visible()`, the one rule of host visibility. The node is never
   moved silently. A stated index past the devices is returned as stated, for the dispatch to
   refuse.
2. No stated device: the owner.
3. No owner: the stated device, else `K_DEFAULT_PLACEMENT`. A node whose buffers are all
   `Residency::Planned` and hold no instance on any device has no owner, and so has one whose only
   instance is the host's after a `write_range()`, so it runs on the default device until a
   stated device or the partitioner places it; the coherence planner then allocates its buffers
   there.

The function needs no queue. It reads both answers through `PlacementMemory`, which is asked about
a region's `GlobalHandle`; the form that takes a `USMAllocator` and a `ResidencyView` wraps them
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
