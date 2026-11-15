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
| `planned_node_factory.hpp` | `PlannedNodeFactory`, the port that turns a step, an effect or a copy's record into a node |
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
other buffer a node wrote stays valid only where it was written. The plan sets `CopyPlan::steady`
to whether every buffer is still valid on the spaces it was valid on before. It writes nothing into
the directory's maps; the run records the end state as its steps complete (see "Planned state and
current state").
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

Each call works on the map under its coherence lock. A call made while a run of a graph that holds
the buffer is in flight, from the run's Hold effect to its Leave effect, is refused as
`InvalidState`. Refusing was chosen over waiting: a host node of the same graph that read the buffer
would wait for its own run's Leave and never return, and a refusal leaves the caller free to retry
after the run. `read()` and `write()` make the host instance before they take the lock, since making
it takes the directory's writer mutex.

## Planned state and current state

The directory's map of a buffer is its current state: what the steps that already ran left. The
planner never writes it, apart from giving a buffer it meets with no map the state the buffer already
has (its bytes on its home space). It copies each map once, under the coherence lock, and plans on
that copy, so two commits at once on graphs that share a buffer each plan from a whole snapshot.

The current map changes only as steps complete, so mid-run it is never ahead of the bytes. A copy
node starts its transfer and returns the transfer's event without waiting for it. The scheduler polls
that event like any device event and releases the copy's successors once it completes; one of them
is the copy's record node, from `PlannedNodeFactory::make_copy_record()`, which then records
`record_copy()`. Under a lazy event model such as AdaptiveCpp's, the poller's stall drive (a
`wait()` on the oldest pending event, `modules/engine/scheduler/README.md`) is what moves the copy
forward; no planned node waits on it. A free node records `drop_space()` once the instance is
dropped. `CopyPlan::effects`
carries what no step carries, each built into a host node by `PlannedNodeFactory::make_effect()`:

| Effect | Placed | Records |
| --- | --- | --- |
| `Hold` | one per buffer, before node 0 | the run holds the buffer: host access is refused |
| `Write` | one per written region, after its node has completed | `record_write()` on the node's space |
| `Leave` | one per buffer, at the graph end after every node and step | the run's hold on the buffer ends |

A host read between a commit and its run therefore sees the state before the run. Effects are
recorded in plan order on each buffer, so the map holds the plan's end state when the run leaves the
buffer.

Hold and Leave nodes carry `Graph::Node::runs_on_abort`, so the scheduler runs them after a task
failure or a cancellation aborts the run, when it skips every other node. A failed run therefore
releases its holds, and host access works after it. A Write node does not carry the flag: the write
of a node that never ran is not recorded.

Each effect node takes the buffer's coherence lock once. These are host nodes, not kernel dispatches,
and the lock is uncontended unless host access or a horizon hits the same buffer at that moment. A
copy's record node does the same. The cost will be measured in the closing build and test run of the
GLOBAL_HANDLE plan.

## splice_plan

`splice_plan()` plans the nodes, builds every planned node through the factory before it changes the
graph, and returns the `CopyPlan` it spliced, so the caller can keep it beside the graph it built. A
refused step leaves the nodes as they were. A step before a node precedes it; an allocation or a
free before a node also takes the node's predecessors, a copy does not. The splice keeps, for each
instance, the nodes that wrote it (allocations, copies into it, frees, Writes), the copies that read
it and the record nodes of the copies into it. A copy follows only the writers of its source, so it
waits for the producer of the version it moves and two copies of one source run together; it
follows every writer, reader, record and node user of its destination, so it never overwrites bytes
still read. A node reads after the writers of an instance, and writes after its writers and the
copies that read it. A step follows the writers, readers and records of the instance it makes or
frees. A copy precedes its record node, and nothing waits for a record but later steps and effects
of the buffer. A step at the graph end follows every leaf. A Hold precedes every step, effect and
node of its buffer. A Write follows its node and every earlier step and record of its buffer on any
space, and precedes every later step, effect and node of the buffer. A Leave follows every leaf and
every step and record of its buffer. Each edge raises the target's in-degree; the step nodes are
appended in plan order, then the effect nodes in plan order, then the record nodes in plan order.

`splice_plan()` given a `SpaceOfDevice` plans a cut node as its parts: `NodeAccess::parts` holds each
part's space, reads and writes, the regions the consumer's `NodeSplit::regions` gave that part,
which may be several ranges of one buffer for a strided part. The planner makes every part's reads valid on its space before it
records any part's writes, since the parts run at once. A later reader on another space then copies
back only the range each part wrote, and the end of the graph restores the start state as for any node.

## DirectoryPlannedNodeFactory

The planner and the factory take a `Memory::ResidencyReader` and a `Memory::ResidencyWriter`, the two
ports the residency directory implements, and name no concrete directory. The planner reads sizes and
instances through the reader and locks each buffer's coherence map through the writer; the factory's
nodes also record and drop instances through the writer. The class keeps its name because its callers
in `surface/api` still use it; it works over any pair of them. Taking a reader and a writer of one
directory is how it uses both roles, not a split to undo: the planner reads sizes and locks maps, and a
step reads instances and records them.

Each node is a host node pinned to the device of the space it makes, writes or frees. An allocate
node makes the instance only when the directory has none; a copy node resolves both instances when
it runs, hands them to its `TransferPath` and returns the path's event; a copy's record node, on the
same device, records the copy on the map under the coherence lock; a free node drops the
instance, releases it and drops the space from the map. An effect node is a host node on device 0
that applies its effect to the map under the coherence lock. The
nodes share the allocator; the directory and the paths must outlive them.

A node runs `Node::checked_host_work`, which returns a `Result<sycl::event>`, so no planned node
throws on a worker. An allocator that returns null gives `AllocationFailed`, a directory that refuses
the instance or a copy between an instance that is missing gives `InvalidState`, and a path's error is
passed on as it is. The scheduler records the error as the run's failure.
