# coherence

The module plans the allocate, copy and free nodes that bring every byte a node reads to the
device it runs on, and adds them to a built graph. It is an internal module: its headers reach the
modules that name it and the tests, and are never installed. Its names are in
`SushiRuntime::Internal::Coherence`. The design is in `docs/design/GLOBAL_HANDLE/README.md`,
section "Coherence and copy planning".

It depends on `core`, `handle`, `region`, `memory` and `task`.

## Public entry points

| Header | Holds |
| --- | --- |
| `coherence_planner.hpp` | `CoherencePlanner`, `NodeAccess` and `byte_range()` |
| `copy_plan.hpp` | `CopyPlan`, `PlannedStep` and `PlannedStepKind` |
| `transfer_path.hpp` | `TransferPath`, the interface of one way of copying, and `TransferRequest` |
| `transfer_cost.hpp` | `TransferCost` and `uniform_transfer_cost()` |
| `same_context_path.hpp` | `SameContextPath`, one memcpy between two devices of one SYCL context |
| `peer_path.hpp` | `PeerPath`, one peer copy between two contexts of one backend |
| `space_queues.hpp` | `SpaceQueues`, the port that names the queue a space copies on |
| `peer_access.hpp` | `PeerAccess`, the port that grants one device the memory of another |
| `transfer_observer.hpp` | `TransferObserver` and `TransferRecord`, the report of which path each copy took |
| `instance_allocator.hpp` | `InstanceAllocator`, the port that makes and frees an instance |
| `usm_instance_allocator.hpp` | `USMInstanceAllocator`, the port over a `USMAllocator` |
| `planned_node_factory.hpp` | `PlannedNodeFactory`, the port that turns a step into a node |
| `directory_planned_node_factory.hpp` | `DirectoryPlannedNodeFactory`, nodes that resolve through the directory |
| `plan_splice.hpp` | `splice_plan()` and `SpaceOfNode` |

## CoherencePlanner

`CoherencePlanner::plan()` walks the nodes in order. For each read range that the node's space does
not hold, it adds an allocation when the space has no instance, then one copy per missing interval.
The source is the cheapest valid space by `TransferCost`; of equal costs, the first space in the
order the planner was given wins, and the caller gives spaces in `DeviceKey` order. Of equal paths,
the first path wins. Each write leaves only the node's space valid. At the graph end the plan copies
every byte back to each space it was valid on before the graph, and frees each instance the plan
made, so a graph run again meets the state it was planned for.

A plan reads only its inputs: the nodes, the directory's state, the spaces, the paths and the cost.
A buffer with the `Borrowed` or `HostOnly` flag, or with no instance, is left alone. A copy no path
makes is refused as `Unsupported`.

## SameContextPath and PeerPath

Both copy with one `memcpy` on the destination's queue, which `SpaceQueues` names. `SameContextPath`
connects two spaces when the source lists the destination among its direct targets, which is how a
device space records a shared SYCL context. `PeerPath` connects two spaces the source does not list,
when `PeerAccess` grants the destination's device the source's memory. The runtime's `PeerAccess`
asks the SYCL peer-access extension on DPC++ and refuses everywhere else, so under AdaptiveCpp the
path connects nothing. Each path tells its optional `TransferObserver` the path name, the two spaces
and the byte count of every copy it starts; the runtime registers no observer.

The runtime builds both paths once, in `engine/execution`'s `build_transfer_paths()`, and the
context hands them out as `RuntimeContext::transfer_paths()`, `SameContextPath` first.

## splice_plan

`splice_plan()` builds every planned node through the factory before it changes the graph, so a
refused step leaves the nodes as they were. A step before a node takes the node's predecessors and
precedes it. A step precedes every later step and node that uses one of its instances. A step at
the graph end follows every leaf. Each edge raises the target's in-degree, and the planned nodes are
appended in plan order.

## DirectoryPlannedNodeFactory

Each node is a host node pinned to the device of the space it makes, writes or frees. An allocate
node makes the instance only when the directory has none; a copy node resolves both instances when
it runs and hands them to its `TransferPath`; a free node drops the instance and releases it. The
nodes share the allocator; the directory and the paths must outlive them.
