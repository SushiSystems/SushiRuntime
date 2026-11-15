# coherence

The module plans the allocate, copy and free nodes that bring every byte a node reads to the
device it runs on, and adds them to a built graph. The design is in
`docs/design/GLOBAL_HANDLE/README.md`, section "Coherence and copy planning". It is an internal module: its headers reach the modules
that name it and the tests, and are never installed. Its names are in
`SushiRuntime::Internal::Coherence`.

It depends on `core`, `handle`, `log`, `memory`, `region`, `task` and `transfer`, and its sources on
`partitioning` for `FNVHash`.

## Public entry points

| Header | Holds |
| --- | --- |
| `coherence_planner.hpp` | `CoherencePlanner`, `NodeAccess` and `byte_range()` |
| `copy_plan.hpp` | `CopyPlan`, `PlannedStep` and `PlannedStepKind` |
| `eviction_policy.hpp` | `EvictionPolicy`, the interface that chooses a victim, and `EvictionCandidate` |
| `logical_time_eviction.hpp` | `LogicalTimeEviction`, the default policy |
| `pin_set.hpp` | `PinSet`, the instances eviction never touches |
| `residency_options.hpp` | `ResidencyOptions`, the capacity rules of one planner |
| `plan_hash.hpp` | `plan_hash()`, the FNV-1a hash of a plan |
| `instance_allocator.hpp` | `InstanceAllocator`, the port that makes and frees an instance and names its kind of USM |
| `usm_instance_allocator.hpp` | `USMInstanceAllocator`, the port over a `USMAllocator` |
| `planned_node_factory.hpp` | `PlannedNodeFactory`, the port that turns a step into a node |
| `directory_planned_node_factory.hpp` | `DirectoryPlannedNodeFactory`, nodes that resolve through the directory |
| `plan_splice.hpp` | `splice_plan()` and `SpaceOfNode` |
| `planned_host_access.hpp` | `PlannedHostAccess`, which mints a planned buffer and moves its bytes to and from the host |
| `coherence_log_category.hpp` | `K_COHERENCE_LOG_CATEGORY`, the log category of the module |

## CoherencePlanner

`CoherencePlanner::plan()` walks the nodes in order. For each read range that the node's space does
not hold, it adds an allocation when the space has no instance, then one copy per missing interval.
The source is the cheapest valid space by `TransferCost`; of equal costs, the first space in the
order the planner was given wins, and the caller gives spaces in `DeviceKey` order. Of equal paths,
the first path wins. Each write leaves only the node's space valid. Without residency options, at the graph end the plan copies
every byte back to each space it was valid on before the graph, and frees each instance the plan
made, so a graph run again meets the state it was planned for.

A plan reads only its inputs: the nodes, the directory's state, the spaces, the paths and the cost.
A buffer with the `Borrowed` or `HostOnly` flag is left alone. A buffer with no instance is left
alone too, unless its coherence map already covers its bytes: a `Residency::Planned` buffer is made
that way, and the first node that uses it gets an allocation step. Its bytes that no space holds were
never written, so nothing is copied for them. A copy no path makes is refused as `Unsupported`.

## Capacity and eviction

`CoherencePlanner::set_residency()` turns the capacity rules on; without it the planner behaves as
described above. With it the planner counts the bytes it plans on each space, starting from the
instances the buffers of the nodes already hold, and keeps the count below
`MemorySpace::capacity_bytes()` less `reserve_bytes()`. Memory held by anything the nodes do not
name is left to the reserve.

When a space cannot hold a new instance, the planner lists the instances on that space in handle
order and asks the `EvictionPolicy` for one. It never lists an instance of `PinSet`, an instance the
current node uses, or the only copy on the host, which has nowhere to be written back to. The
victim's bytes that no other space holds are first copied to `ResidencyOptions::host`; then the
instance is freed. Both steps carry `evicts`, so the splice orders them after every earlier node
that used the instance, and orders an allocation on the space after the evictions that made room. A
space with nothing to evict refuses the allocation as `AllocationFailed`, naming the planned bytes
and the usable bytes.

Each candidate carries `write_back_cost`: for a sole copy, the least price of copying its bytes to
the host over a path that connects them, by the planner's `TransferCost`; zero otherwise.

`LogicalTimeEviction` evicts the instance whose next read is furthest in the recorded graph. A read
in the next step counts as a position past the graph end, in node order. Of equal positions the
instance with the lower `write_back_cost` goes first, so one that needs no write-back goes before one
that does, then the first candidate. No clock and no address takes part, so the same graph gives the same plan, and `plan_hash()` over its steps and `steady`
flag gives the same value in every run.

A resident plan does not free what it allocated, so the next step meets the instances it needs. It
copies back only the buffers named in `ResidencyOptions::host_read`, each to its home space; any
other buffer a node wrote stays valid only where it was written. The plan then writes the end state
into each buffer's map in the directory, so the next `plan()` starts from it, and sets
`CopyPlan::steady` to whether every buffer is still valid on the spaces it was valid on before.
A plan that is not steady must be planned again before the next run and is never replayed.

## PlannedHostAccess

`PlannedHostAccess` serves `Residency::Planned` buffers. `mint()` describes the entry with its size,
its element layout and the host space as the recorded home, and leaves the coherence map covering
the bytes with no space set; no instance is made. The planner reads such a map as a buffer held
nowhere, so the first node that uses it gets an allocation step and no copy.

`write()` copies the bytes into the buffer's host instance, made through the `InstanceAllocator` on
first use, and calls `record_write()` on the host space, so every other space loses the range. It
also calls `HostWriteLog::record_host_write()`, so a stored copy plan over the buffer is made
again before the next run.
`read()` zeroes the destination, copies to the host instance each range the host space lacks from
the first space in the planner's order that holds it, over the first path that connects the two,
and then reads the host instance. A range no path reaches is refused as `Unsupported`. `release()`
frees every instance the directory records with a kind of USM, the host instance and each instance
a planned allocation made on a device, and retires the handle. An instance with no kind is the one
`mint_held()` gave, which the runtime did not allocate; `held_spaces()` names its spaces before the
release, so the caller can free what holds it. For a file-backed buffer that is its file space.

The calls write the coherence map, which takes no lock, so they run between runs and never beside
the planner.

## splice_plan

`splice_plan()` plans the nodes, builds every planned node through the factory before it changes the
graph, and returns the `CopyPlan` it spliced, so the caller can keep it beside the graph it built. A
refused step leaves the nodes as they were. A step before a node takes the node's predecessors and
precedes it. A step precedes every later step and node that uses one of its instances. A step at
the graph end follows every leaf. Each edge raises the target's in-degree, and the planned nodes are
appended in plan order.

`splice_plan()` given a `SpaceOfDevice` plans a cut node as its parts: `NodeAccess::parts` holds each
part's space, reads and writes. The planner makes every part's reads valid on its space before it
records any part's writes, since the parts run at once. A later reader on another space then copies
back only the range each part wrote, and the end of the graph restores the start state as for any node.

## DirectoryPlannedNodeFactory

The planner and the factory take a `Memory::ResidencyView`, the port the residency directory implements,
and name no concrete directory. The class keeps its name because its callers in `surface/api` still
use it; it works over any `ResidencyView`.

Each node is a host node pinned to the device of the space it makes, writes or frees. An allocate
node makes the instance only when the directory has none; a copy node resolves both instances when
it runs and hands them to its `TransferPath`; a free node drops the instance and releases it. The
nodes share the allocator; the directory and the paths must outlive them.
