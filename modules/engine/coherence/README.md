# coherence

The module plans the allocate, copy and free nodes that bring every byte a node reads to the
device it runs on, and adds them to a built graph. It is an internal module: its headers reach the
modules that name it and the tests, and are never installed. Its names are in
`SushiRuntime::Internal::Coherence`. The design is in `docs/design/GLOBAL_HANDLE/README.md`,
section "Coherence and copy planning".

It depends on `core`, `handle`, `region`, `report`, `memory` and `task`.

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
| `transfer_path.hpp` | `TransferPath`, the interface of one way of copying, and `TransferRequest` |
| `transfer_cost.hpp` | `TransferCost` and `uniform_transfer_cost()` |
| `same_context_path.hpp` | `SameContextPath`, one memcpy between two devices of one SYCL context |
| `peer_path.hpp` | `PeerPath`, one peer copy between two contexts of one backend |
| `host_staged_path.hpp` | `HostStagedPath`, the copy through pinned host staging buffers |
| `file_path.hpp` | `FilePath`, the read of a file space into a host or device space through the staging ring |
| `staging_ring.hpp` | `StagingRing`, `StagingRingShape` and `StagingSlot`, the fixed ring of pinned slots |
| `pinned_host_memory.hpp` | `PinnedHostMemory`, the port that makes and frees pinned host memory |
| `space_queues.hpp` | `SpaceQueues`, the port that names the queue a space copies on |
| `peer_access.hpp` | `PeerAccess`, the port that grants one device the memory of another |
| `transfer_observer.hpp` | `TransferObserver` and `TransferRecord`, the report of which path each copy took |
| `transfer_observer_slot.hpp` | `TransferObserverSlot`, the observer the runtime's paths hold, which forwards to one a caller installs |
| `instance_allocator.hpp` | `InstanceAllocator`, the port that makes and frees an instance and names its kind of USM |
| `usm_instance_allocator.hpp` | `USMInstanceAllocator`, the port over a `USMAllocator` |
| `planned_node_factory.hpp` | `PlannedNodeFactory`, the port that turns a step into a node |
| `directory_planned_node_factory.hpp` | `DirectoryPlannedNodeFactory`, nodes that resolve through the directory |
| `plan_splice.hpp` | `splice_plan()` and `SpaceOfNode` |
| `planned_host_access.hpp` | `PlannedHostAccess`, which mints a planned buffer and moves its bytes to and from the host |
| `device_profile.hpp` | `DeviceProfile` and `PathProfile`, what one device measured |
| `device_profile_set.hpp` | `DeviceProfileSet`, the profiles by `DeviceKey`, and `has_profile()` |
| `device_profile_file.hpp` | `DeviceProfileFile`, the reader and writer of the profile file |
| `cost_model.hpp` | `CostModel`, which prices a `NodeCost` and a copy in seconds |
| `profile_transfer_cost.hpp` | `select_transfer_cost()` and `SpaceDeviceResolver` |
| `profile_probe.hpp` | `ProfileProbe`, the port that times copies and kernels on a device |
| `profile_measurement.hpp` | `ProfileMeasurement` and `MeasurementPlan`, which build a `DeviceProfile` through the probe |
| `partition_problem.hpp` | `PartitionProblem`, `PartitionNode`, `PartitionBuffer`, `PartitionEdge` and `PartitionDevice` |
| `partition_assignment.hpp` | `PartitionAssignment`, the device of each node, the parts of each split node and the home of each planned buffer, with `PlannedSplit`, `PlannedSplitPart` and `BufferRange` |
| `split_sizing.hpp` | `size_split()` and `SizedPart`: an axis cut into parts sized by device speed, at most one per device |
| `split_expansion.hpp` | `expand_splits()`, `planned_splits()` and `SplitExpansion`: a split node as one placed node per part, and the byte ranges each part reads and writes |
| `partition_refusal.hpp` | `PartitionRefusal` and `PoolShortfall`, a refusal with the shortfall per pool |
| `partitioner.hpp` | `Partitioner`, the port that shares a graph across devices |
| `list_scheduling_partitioner.hpp` | `ListSchedulingPartitioner`, rank, place, refine and the floor check |
| `partition_model.hpp` | `PartitionModel`, which prices a placement and counts its bytes per pool |
| `pool_occupancy.hpp` | `PoolOccupancy`, the bytes a placement holds in each pool |
| `fill_plan.hpp` | `fill_plan()`, `floor_plan()`, `FillWalk` and `FloorPlan` |
| `assignment_hash.hpp` | `assignment_hash()`, the FNV-1a hash of an assignment |
| `probe_devices.hpp` | `ProbeDevices` and `ProbeDevice`, the port that finds a device's queue and space by `DeviceKey` |
| `device_profile_probe.hpp` | `DeviceProfileProbe` and `ProbeKernel`, the probe that times real SYCL devices |
| `partition_report_builder.hpp` | `make_partition_report()`, the `PartitionReport` of a partitioner's answer |
| `dispatch_observation.hpp` | `DispatchObservation`, `DeviceKeyTable` and `observe_dispatches()`, the ledger's records as measured work |
| `profile_fold.hpp` | `fold_observations()`, which folds measured throughput into the device profiles |
| `profile_update.hpp` | `update_profile_file()`, which folds a run into the profile file and writes it back |
| `fnv_hash.hpp` | `FNVHash`, the 64-bit FNV-1a accumulator `plan_hash()` and `assignment_hash()` share |
| `graph_partition_problem.hpp` | `make_graph_partition_problem()` and `GraphPartitionProblem`, the problem of a recorded graph |
| `partition_apply.hpp` | `apply_partition()`, which puts the movable nodes where an assignment says |
| `partition_measurement.hpp` | `PartitionMeasurement`, what the runs of a plan measured per step |
| `partition_ledger.hpp` | `PartitionLedger`, the dispatches, steps and seconds of a plan's runs |
| `partition_session.hpp` | `PartitionSession` and `PartitionSessionParts`, a runtime's partitioner, last plan and ledger |
| `transfer_path_tally.hpp` | `TransferPathTally`, `PathTally` and `PathTallies`, the copies counted per destination and path |

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
also calls `ResidencyDirectory::record_host_write()`, so a stored copy plan over the buffer is made
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

## SameContextPath and PeerPath

Both copy with one `memcpy` on the destination's queue, which `SpaceQueues` names. `SameContextPath`
connects two spaces when the source lists the destination among its direct targets, which is how a
device space records a shared SYCL context. `PeerPath` connects two spaces the source does not list,
when `PeerAccess` grants the destination's device the source's memory. The runtime's `PeerAccess`
asks the SYCL peer-access extension on DPC++ and refuses everywhere else, so under AdaptiveCpp the
path connects nothing. Each path tells its optional `TransferObserver` the path name, the two spaces
and the byte count of every copy it starts. The runtime gives every path its `TransferObserverSlot`,
which drops each copy until a caller installs an observer through
`AdvancedControls::install_transfer_observer()`, between runs.

The runtime builds the paths once, in `engine/execution`'s `build_transfer_paths()`, and the
context hands them out as `RuntimeContext::transfer_paths()`: `SameContextPath`, `PeerPath`,
`HostStagedPath`, then `FilePath`. The planner takes the first path that connects, so each later path serves only
what the earlier ones refuse.

## HostStagedPath and StagingRing

`HostStagedPath` connects any two spaces that have a queue, and the host space with any space that
has one. A copy between two devices is cut into chunks of one ring slot. A chunk goes from the
source into a slot on the source's queue, and from the slot to the destination on the destination's
queue; the copy out of the slot for chunk k runs while chunk k + 1 is copied into the next slot, so
the two legs overlap. The path waits on the host for each source-to-slot leg, because the two
queues may belong to two contexts and an event does not cross a context. It returns the event of
the last slot-to-destination leg; each such leg waits for the one before it.

The host space is pinned host memory, so a copy to or from it is one `memcpy` on the device's
queue, with no slot. A space `SpaceQueues` gives no queue is read as host memory in `copy()`.

`StagingRing` holds `slot_count` pinned buffers of `slot_bytes` each. The defaults,
`K_DEFAULT_STAGING_SLOT_BYTES` (4 MiB) and `K_DEFAULT_STAGING_SLOT_COUNT` (4), are in
`staging_ring.hpp` and nowhere else; the runtime builds the ring with them. The slots are made
through `PinnedHostMemory` on the first `acquire()`, never again, and freed with the ring. A slot
has one holder; the holder retires it with the event of its last read, and the next holder waits
for that event. The ring locks only to hand a slot out or take it back.

The path does not keep the host bytes as a valid instance: the slot is reused, and the directory
never hears of it. Keeping a host instance when a later reader needs it is a planner refinement,
not part of this path.

## FilePath

`FilePath` reads a file space into any space that is not a file. It connects a source only when the
source is a `FileMemorySpace` added to it with `add()`; the runtime adds each file space when a
buffer is made from a file range, between runs, and `remove()` lets go of it when the buffer is
dropped, so its id can name the next file space. A copy to a space with a queue cuts the range into
chunks of one ring slot, in file order: the path reads a chunk from the file into a slot and copies
the slot to the destination on the destination's queue, and the read of the next chunk runs while
that copy does. The host memory a copy holds is therefore the ring, `slot_bytes * slot_count`, and
the file stream's own buffer, far under the 512 MB the design allows for read buffering. A copy to
a space with no queue, the host, reads the file straight into the destination. Reads go through
`std::ifstream`, so the path is the same on Windows and Linux. The file must not change while a
buffer made from it lives.

`PlannedHostAccess::mint_held()` mints the buffer: its one valid instance is the file space's token,
and the coherence map names the file space as the holder of every byte. A read from the host, or a
node the planner places on a device, then copies from the file through this path.

## splice_plan

`splice_plan()` plans the nodes, builds every planned node through the factory before it changes the
graph, and returns the `CopyPlan` it spliced, so the caller can keep it beside the graph it built. A
refused step leaves the nodes as they were. A step before a node takes the node's predecessors and
precedes it. A step precedes every later step and node that uses one of its instances. A step at
the graph end follows every leaf. Each edge raises the target's in-degree, and the planned nodes are
appended in plan order.

## DirectoryPlannedNodeFactory

The planner and the factory take a `Memory::ResidencyView`, the port the residency directory implements,
and name no concrete directory. The class keeps its name because its callers in `surface/api` still
use it; it works over any `ResidencyView`.

Each node is a host node pinned to the device of the space it makes, writes or frees. An allocate
node makes the instance only when the directory has none; a copy node resolves both instances when
it runs and hands them to its `TransferPath`; a free node drops the instance and releases it. The
nodes share the allocator; the directory and the paths must outlive them.

## Device profile

`DeviceProfile` holds, for one `DeviceKey`, the capacity and reserve of the device's pool, its memory
bandwidth, a base throughput, a throughput per `OperationKind` and the bandwidth and latency of each
transfer path that leaves it, by path name. Throughputs are operations per second, bandwidths are bytes
per second and latencies are seconds. A kind with no entry takes the base throughput.

`DeviceProfileSet` holds the profiles in `DeviceKey` order. `has_profile()` is how the partitioner
learns that a device was never measured; such a device receives no node.

## CostModel

`CostModel` is built once from a `DeviceProfileSet` and never changes. `node_time()` is a node's
operations over the device's throughput for its kind, plus its read and written bytes over the
device's memory bandwidth. `transfer_time()` is the latency of a path plus the bytes over its
bandwidth; the path is looked up on the source device first, then on the destination.
`host_copy_time()` prices a copy over a device's host link in one direction, and `host_staged_time()`
prices a copy between two devices through host memory as the source's device-to-host link plus the
destination's host-to-device link. A device, a path or a link with no profile gives no time. A device
with a rate of zero that has work to do gives infinity.

`select_transfer_cost()` gives the planner its `TransferCost`: the model's seconds when a model is
loaded, `uniform_transfer_cost()` when none is. A copy the model cannot price costs infinity. The
runtime builds it once, at create, in `engine/execution`'s `load_transfer_cost()`, and hands it out as
`RuntimeContext::transfer_cost()`; the composer's and `PlanCache`'s hook prices every copy with it,
so the cheapest path and source win and eviction weighs the price of a write-back. The resolver maps
a device space to its device's `DeviceKey` and the host space, or a file space, to none. A copy between
two devices costs the path's own measurement, or the copy through host memory when the profile names no
such path, which prices `HostStagedPath`. A copy between a device and a space with no device costs
that device's host link, which prices host copies and `FilePath`. A copy no measurement covers costs
infinity, and the first such copy is logged as a warning.

## Partitioner

`Partitioner` takes a `PartitionProblem` (nodes in recording order with their `NodeCost`, buffers
with their declared size, edges, devices with their pool and copy path) and returns a
`PartitionAssignment`, or a `PartitionRefusal` before anything is loaded.
`ListSchedulingPartitioner` is the one implementation; the shape it follows is in
[PARTITIONING.md](../../../docs/design/GLOBAL_HANDLE/PARTITIONING.md).

- `PartitionModel` keeps the devices that have a profile, in `DeviceKey` order, and numbers the
  pools in the order their first device appears. A pool's usable bytes are its first device's
  capacity less its reserve, so a pool that several devices share is counted once. A step runs the
  nodes in recording order, each device one after another; an edge between two pools waits for the
  copy over the source device's path, an edge inside one pool does not.
- `PoolOccupancy` counts the peak of the live instances of each pool. An instance is live from the
  first to the last node of its pool that touches the buffer, in recording order; a buffer read
  before any node writes it, or written by the last node that touches it, is carried across steps
  and live for the whole step, in every pool it has an instance in. A buffer with a stated device
  holds its home instance for the whole step. Nodes may be added in any order.
- `fill_plan()` orders a device subset by the time each takes alone, walks the nodes in recording
  order and moves to the next device when a pool is full, never back. `floor_plan()` is the fitting
  fill plan of least step time over every subset, so the model takes at most sixteen devices.
- `ListSchedulingPartitioner` ranks the nodes upward, places each on the device that finishes it
  earliest within capacity, moves a node to a neighbour's device while the step falls, and takes the
  floor when the floor is faster. Ties go to recording order and to the lower `DeviceKey`.
- A job that fits in no plan is refused as `AllocationFailed` with one `PoolShortfall` row per
  pool. When one node needs more than any pool holds, each row holds that node's bytes; otherwise
  each row holds what the fill plan over every device puts in the pool.
- `PartitionAssignment::home_of()` gives each `Residency::Planned` buffer the device of its first
  writer, or of its first reader when nothing writes it. `assignment_hash()` hashes the devices, the
  homes, the step time, the floor flag and the parts of each split node, so equal inputs and one
  profile file give equal hashes. It and `plan_hash()` fold their fields through one `FNVHash`.

### Splitting

A `PartitionNode` whose `split` holds an extent of two or more may run as parts.
`ListSchedulingPartitioner` takes a declared split when the node fits no pool whole, and otherwise
when the cut shortens the modelled step; `Splitting::Off` keeps every node whole. Nodes are tried
in recording order and a cut is kept only when it lowers the step time of the best plan so far,
which starts as the plan of the graph with its nodes whole, so a plan with splits is never
modelled slower than that plan or its floor. A consumer-placed node is never cut.

- `size_split()` gives each device a share of the extent in proportion to its speed, one over the
  time `CostModel` gives the whole node there. Shares are whole numbers rounded down, the remainder
  goes to the fastest device (the first of equals), a device with no share gets no part, and parts
  follow `DeviceKey` order.
- `expand_splits()` replaces the node with one node per part, placed on the part's device, costing
  its share of the operations and bytes. A part reads every buffer the node reads whole and writes
  a buffer of its own that holds its share of each written buffer, so capacity counts only that
  share. An edge into the node reaches every part whole; an edge out of it carries each part's
  share. A later reader of a buffer the parts wrote reads their slices in its place, so each slice is
  live until its last reader. The expanded problem is scheduled like any other.
- `planned_splits()` gives the assignment one `PlannedSplit` per cut node: per part the device, the
  `SplitPart`, the whole range of each buffer read and the bytes `[bytes * begin / extent, bytes *
  end / extent)` of each buffer written. The written ranges of one node are disjoint and cover the
  buffer; reads may overlap. `node_devices` names the first part's device for a cut node.

The step drivers, the composer and the copy planner do not read `splits` yet; a cut node runs as
parts through `Internal::Graph::dispatch_split()` (`engine/task`) once they do.

## Partitioning a recorded graph

`make_graph_partition_problem()` turns a graph's nodes into a `PartitionProblem` over the runtime's
devices, device i standing for device index i. A node's cost is its `NodeCost`, none counting as
zero, and its split is the axis and extent of its `declared_split`. A `Borrowed` or `HostOnly` buffer is left out; a buffer a device owns keeps that device, and
any other is planned. An edge joins the last earlier writer of a buffer to each reader and carries
the bytes the reader's region names; the edges are ordered by writer, then reader. A node with a
stated device, or a `FixedToHome` node whose anchor a device owns, is placed where `placement_of()`
puts it (`modules/engine/task/README.md`, "Placement rule order"); the partitioner may move every
other node.

`apply_partition()` states the assigned device on each movable node, or changes nothing and returns
`InvalidArgument` when the assignment does not fit the problem. The coherence planner then
allocates each planned buffer's first instance on the device of its first writer, which is the home
the assignment gives it. Rebalancing calls the same function with a new assignment.

`PartitionSession` holds what the runtime keeps: the frozen `CostModel`, the `Partitioner`, the
devices with their pools and copy paths, the problem and answer of the last plan, its
`PartitionLedger` and the `TransferPathTally` every transfer path reports to. `partition()` keeps a
new plan and starts an empty ledger and tally; `report()` gives `make_partition_report()` of the last
plan with what its runs measured. A session with no cost model is inactive and reports nothing.
`PartitionLedger::measurement()` divides by the steps added: the measured step, the summed dispatch
seconds of each device and the copies into each device by path. `TransferPathTally` takes a lock per
copy, since paths report from several workers, and forwards each copy to the observer a caller
installed.

## PartitionReportBuilder

`make_partition_report()` turns a problem, a `CostModel` and a partitioner's answer into a `PartitionReport`
(`modules/foundation/report/README.md`). It makes one row per device of the problem, in `DeviceKey` order, so
a device that received no node has a row of zeros. For an assignment a row counts the nodes put on the device,
the bytes of the buffers homed on it (a buffer with a stated device at that device, a planned buffer at the home
the assignment gave it) and the sum of the model's node times of those nodes; a node the model cannot price adds
nothing. The report carries the assignment's modelled step time and floor flag. For a refusal the rows hold no
nodes and no bytes, each row holds the bytes its pool lacked (`PoolShortfall::missing()`), and the report holds
the refusal's sentence. For an assignment the report also carries the floor's modelled step and devices, and,
from a `PartitionMeasurement`, the measured step, each device's measured time and its copies by path. The
function is pure.

## Folding the ledger into the profile

The profile entries a run measured go back into the profile file in three steps, one unit each, so a second
run reads them.

- `observe_dispatches()` turns the records `ProfilingLedger::collect()` appended to `RunObservation::events`
  into `DispatchObservation` values: the device that ran the dispatch, the cost its node declared in the
  `NodeTable`, and the seconds from the record's start to its end. A `DeviceKeyTable` maps a record's device
  index to a `DeviceKey` and may name the host's key. A record is dropped when its node has no cost or counts
  no operations, when its device has no key, or when its end does not follow its start. The unit reads the
  records and the table and names neither the ledger nor the scheduler.
- `fold_observations()` computes one throughput per observation, the node's operations over its seconds less
  the time its bytes take at the device's memory bandwidth, the inverse of how `CostModel::node_time()` prices
  a node, so the folded profile prices the same node at the measured time. The entry of a device and a kind
  becomes the median of its samples, the lower one of an even count, and the generic kind sets
  `base_throughput`. The median is robust against one slow dispatch and gives the same entry for any order of
  records. A device with no profile receives nothing, since it is measured first; an observation whose time is
  at most what its bytes need, or whose time or operations are not positive and finite, is dropped.
- `update_profile_file()` reads the file the run was planned from, folds the observations into it and writes it
  through `DeviceProfileFile::write()`, which uses a temporary file. The file a run reads stays frozen for that
  run; the update is called when the run ends, and the next run reads the result. A missing file is an error
  and nothing is created.

The step driver of `surface/api` calls them through the session's `PartitionLedger`: after each blocking run
of a partitioned plan it adds the run's steps, its seconds and its dispatches, and at each horizon of
`coherence_horizon()` it folds the dispatches into the file `RuntimeConfig::device_profile_file` names. A
runtime with no profile file folds nothing. A failed write is logged and the run goes on.

## ProfileMeasurement

`ProfileMeasurement` fills a `DeviceProfile` through the `ProfileProbe` port. The probe copies bytes no
consumer reads and times a kernel the caller registered for each kind in the `MeasurementPlan`; the
measurement holds no kernel and no list of kinds. With `host_links` set, it also times each device's
copy from host memory and back, and stores them as the host links. The plan must name `OperationKinds::K_GENERIC`, whose
kernel gives the base throughput. Memory bandwidth counts the bytes read and written. A path's latency
is the time of the small copy, and its bandwidth is the large copy's bytes over its time less the
latency. A probe that reports a time of zero or less is refused as `InvalidState`.

## DeviceProfileProbe

`DeviceProfileProbe` is the probe on real devices. It uses standard SYCL only, so it builds under
AdaptiveCpp. `ProbeDevices` gives it each device's queue and memory space by `DeviceKey`. Capacity
and reserve are the space's. A device copy is one `memcpy` between two device allocations; a path
copy goes through the runtime's own `TransferPath` of that name between an allocation on each
device, so it times what the planner will use; a kind runs the `ProbeKernel` the caller registered
for it, which returns the operations it did; a host copy is one `memcpy` between a device
allocation and a host allocation. Each runs once to warm up and once timed, on the host's
steady clock, from the submit to the end of the wait. The time is a measurement and decides no plan
until it is written to the profile file. A path no path of that name connects is `Unsupported`; a
kind with no kernel, or a key with no device, is `InvalidArgument`.

Integrated detection is a hook, not a rule. When a backend reports `host_unified_memory` false for an
APU that carves its memory from RAM (HIP on gfx1036 is the open case of T42), the probe is where
the fact may be taken from measured behaviour instead, such as a device copy no faster than a host
copy. It is not done: no threshold is guessed until the closing run has measured one.

## Device profile file

`DeviceProfileFile` owns the file. It is read once at the start of a run and is frozen for that run.
The writer writes only the current version, devices in `DeviceKey` order, kinds by value and paths by
name, with doubles in 17 significant digits, so one set always gives the same bytes. The file is
written to a temporary file and renamed over the old one.

```
format_version 2

device cuda:uuid:00112233445566778899aabbccddeeff:0
capacity_bytes 12884901888
reserve_bytes 536870912
memory_bandwidth 9.1e+11
base_throughput 1.2e+13
throughput 3195455669 3.1e+13
path pcie 1.2e+10 1e-05
host_to_device 1.1e+10 8e-06
device_to_host 1.0e+10 9e-06
end
```

| Line | Holds |
| --- | --- |
| `format_version <integer>` | The first line. |
| `device <key>` | Opens a block; the key is the text of `to_text(DeviceKey)`. A key appears once. |
| `capacity_bytes`, `reserve_bytes` | Unsigned integers; each appears once per block. |
| `memory_bandwidth`, `base_throughput` | Finite numbers of zero or more; each appears once per block. |
| `throughput <kind> <value>` | `<kind>` is the `OperationKind` value as an unsigned integer. One per kind. |
| `path <name> <bandwidth> <latency>` | `<name>` is one word. One per name. |
| `host_to_device <bandwidth> <latency>`, `device_to_host <bandwidth> <latency>` | The host links; each appears at most once per block. A block without one leaves that link unmeasured. Version 2 only. |
| `end` | Closes the block. |

| Version | The reader |
| --- | --- |
| 1 | Reads it and upgrades it: it has no host links, so every link stays unmeasured. A host link line in it is a fault. |
| 2 | Reads it. It is the current version. |
| 3 or later | Rejects it with `Unsupported`, naming the version found and the versions known. |

A fault in a line is `InvalidArgument` and names the line. A file that is missing is refused by
`read()`. The runtime reads the file `RuntimeConfig::device_profile_file` names, once, at create. An
empty path runs with no `CostModel` and logs it. A path with no file is a first start: the runtime
measures every device and writes the file, which the next start reads. A file that is there and cannot
be read refuses the create with the reader's error.
