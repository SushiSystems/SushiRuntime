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
| `host_staged_path.hpp` | `HostStagedPath`, the copy through pinned host staging buffers |
| `staging_ring.hpp` | `StagingRing`, `StagingRingShape` and `StagingSlot`, the fixed ring of pinned slots |
| `pinned_host_memory.hpp` | `PinnedHostMemory`, the port that makes and frees pinned host memory |
| `space_queues.hpp` | `SpaceQueues`, the port that names the queue a space copies on |
| `peer_access.hpp` | `PeerAccess`, the port that grants one device the memory of another |
| `transfer_observer.hpp` | `TransferObserver` and `TransferRecord`, the report of which path each copy took |
| `instance_allocator.hpp` | `InstanceAllocator`, the port that makes and frees an instance |
| `usm_instance_allocator.hpp` | `USMInstanceAllocator`, the port over a `USMAllocator` |
| `planned_node_factory.hpp` | `PlannedNodeFactory`, the port that turns a step into a node |
| `directory_planned_node_factory.hpp` | `DirectoryPlannedNodeFactory`, nodes that resolve through the directory |
| `plan_splice.hpp` | `splice_plan()` and `SpaceOfNode` |
| `device_profile.hpp` | `DeviceProfile` and `PathProfile`, what one device measured |
| `device_profile_set.hpp` | `DeviceProfileSet`, the profiles by `DeviceKey`, and `has_profile()` |
| `device_profile_file.hpp` | `DeviceProfileFile`, the reader and writer of the profile file |
| `cost_model.hpp` | `CostModel`, which prices a `NodeCost` and a copy in seconds |
| `profile_transfer_cost.hpp` | `select_transfer_cost()` and `SpaceDeviceResolver` |
| `profile_probe.hpp` | `ProfileProbe`, the port that times copies and kernels on a device |
| `profile_measurement.hpp` | `ProfileMeasurement` and `MeasurementPlan`, which build a `DeviceProfile` through the probe |

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

The runtime builds the paths once, in `engine/execution`'s `build_transfer_paths()`, and the
context hands them out as `RuntimeContext::transfer_paths()`: `SameContextPath`, `PeerPath`, then
`HostStagedPath`. The planner takes the first path that connects, so each later path serves only
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
bandwidth; the path is looked up on the source device first, then on the destination. A device or a
path with no profile gives no time. A device with a rate of zero that has work to do gives infinity.

`select_transfer_cost()` gives the planner its `TransferCost`: the model's seconds when a model is
loaded, `uniform_transfer_cost()` when none is. A copy the model cannot price costs infinity.

## ProfileMeasurement

`ProfileMeasurement` fills a `DeviceProfile` through the `ProfileProbe` port. The probe copies bytes no
consumer reads and times a kernel the caller registered for each kind in the `MeasurementPlan`; the
runtime holds no kernel and no list of kinds. The plan must name `OperationKinds::K_GENERIC`, whose
kernel gives the base throughput. Memory bandwidth counts the bytes read and written. A path's latency
is the time of the small copy, and its bandwidth is the large copy's bytes over its time less the
latency. A probe that reports a time of zero or less is refused as `InvalidState`.

## Device profile file

`DeviceProfileFile` owns the file. It is read once at the start of a run and is frozen for that run.
The writer writes only the current version, devices in `DeviceKey` order, kinds by value and paths by
name, with doubles in 17 significant digits, so one set always gives the same bytes. The file is
written to a temporary file and renamed over the old one.

```
format_version 1

device cuda:uuid:00112233445566778899aabbccddeeff:0
capacity_bytes 12884901888
reserve_bytes 536870912
memory_bandwidth 9.1e+11
base_throughput 1.2e+13
throughput 3195455669 3.1e+13
path pcie 1.2e+10 1e-05
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
| `end` | Closes the block. |

| Version | The reader |
| --- | --- |
| 1 | Reads it. It is the current version. |
| 2 or later | Rejects it with `Unsupported`, naming the version found and the version known. |

A fault in a line is `InvalidArgument` and names the line. A file that is missing is refused by
`read()`; a run that has no file runs with no `CostModel`.
