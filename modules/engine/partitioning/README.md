# partitioning

The module shares a graph's nodes across devices: the partition problem, the partitioner port
and its two implementations, the model that prices a placement, the split of a node into parts,
the hashes of a problem and an assignment, and the report of an answer. Its headers and sources
include no SYCL header, and the module is compiled without SYCL. It is an internal module: its
headers reach the modules that name it and the tests, and are never installed. Its names are in
`SushiRuntime::Internal::Coherence`.

It depends on `core`, `handle`, `machine` (for `split_part.hpp` only), `report` and
`device_profile`.

## Public entry points

| Header | Holds |
| --- | --- |
| `problem_hash.hpp` | `problem_hash()`, the FNV-1a hash of a partition problem |
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
| `partition_report_builder.hpp` | `make_partition_report()`, the `PartitionReport` of a partitioner's answer |
| `fnv_hash.hpp` | `FNVHash`, the 64-bit FNV-1a accumulator `plan_hash()` and `assignment_hash()` share |
| `partition_measurement.hpp` | `PartitionMeasurement`, what the runs of a plan measured per step |
| `floor_partitioner.hpp` | `FloorPartitioner`, which answers every problem with its floor plan |
| `planned_homes.hpp` | `planned_homes()`, the home device of each planned buffer under a placement |

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
  its share of the operations. Its read and written bytes are the sums of the ranges the split's
  `regions` function returns for that part, and it writes a buffer of its own per written buffer
  that holds the bytes its regions name there, so capacity counts only those bytes. An edge into
  the node reaches every part whole; an edge out of it carries the bytes the part's writes leave in
  the buffers the reader reads, at most the edge's bytes. A later reader of a buffer the parts wrote
  reads their slices in its place, so each slice is live until its last reader. The expanded problem
  is scheduled like any other.
- `planned_splits()` gives the assignment one `PlannedSplit` per cut node: per part the device, the
  `SplitPart`, and the ranges the split's `regions` returns for it, read and written, verbatim and
  in their order; a `BufferRegion::K_WHOLE` length ends at the buffer's declared size. The runtime
  holds no layout rule: a strided part, such as a column panel of a row-major matrix, arrives as
  several ranges from the consumer. The written ranges of one node are disjoint, as the consumer
  promises; reads may overlap. A node whose split has no `regions` function is never cut.
  `node_devices` names the first part's device for a cut node.

The step drivers, the composer and the copy planner do not read `splits` yet; a cut node runs as
parts through `Internal::Graph::dispatch_split()` (`engine/task`) once they do.

## Hashes

`problem_hash()` folds every field of a `PartitionProblem` into one FNV-1a value, through the same
`FNVHash` and `fold_device_key()` that `plan_hash()` and `assignment_hash()` use. The API's
`GraphPartition` runs the partitioner again only when that hash changed, and takes the session's
answer again when `PartitionSession::generation()` moved for the same hash.

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

## FloorPartitioner

`FloorPartitioner` answers with `floor_plan()` alone, its homes from `planned_homes()`, the same function
`ListSchedulingPartitioner` uses, and `floor_taken` set.
