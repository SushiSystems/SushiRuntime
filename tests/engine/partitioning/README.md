# partitioning tests

The tests of the `partitioning` module, mirroring `modules/engine/partitioning/`.
`tests/engine/partitioning/CMakeLists.txt` builds them; `sr test` runs them with the rest.

`random_partition_case.hpp` holds `SeededDraw`, a portable generator, and `random_partition_case()`,
which builds from a seed a graph of three to eight nodes with forward edges and a speed per device.
`best_feasible_step_time()` tries every placement of a model, keeps the ones that fit and honour
fixed nodes, and returns the least step time; `placement_of()` turns an assignment into device
indices of the model.

## test_partition_report_builder.cpp

The rows match the plan: nodes, bytes and modelled time per device, the measured time and copies of a
`PartitionMeasurement` per device with the measured step, the floor's step and devices, a device with no node as zeros, a
buffer with a stated device counted at it, a refusal with the shortfall of each pool, the answer of
`ListSchedulingPartitioner` against its own assignment, and the same report for the same inputs.

## test_problem_hash.cpp

Equal problems hash equal; a node's cost, placement, split or stated read ranges, a buffer's bytes,
an edge's bytes and a device's copy path each change the hash. Two splits whose `regions` functions
read from different offsets hash apart, and the same function hashes equal.

## test_floor_partitioner.cpp

Two independent nodes on two equal devices go to one device at the floor's step of one second, slower
than the scheduler's plan, with `floor_taken` set; a job no fill plan fits is refused with
`AllocationFailed`.

## test_planned_homes.cpp

Each buffer of a chain is homed on the device of the node that writes it, and a buffer with a stated
device has no home.

## test_step_simulation.cpp

Two readers of one 100-byte range in one pool cost one copy of 100 bytes, the second waiting for the
first's copy. A node with no reads fills the idle gap before a node waiting on a copy, finishing at
0.5 s instead of after it, and `step_time()` in that order gives the same makespan. A planned buffer
read before its first write starts at the home `planned_homes()` names, the writer's device, so the
reader on the other device copies its 100 bytes. A split part on the device that holds its input
half copies nothing, and the other part copies only its 50 bytes.

## test_list_schedule.cpp

For each of 32 seeds, the step `place_by_rank()` and `list_schedule()` report equals
`step_time(placement, order)` of their plan, refining never raises it, and two runs of the
partitioner hash equal. `Medium_ListSchedule` partitions 64 nodes over sixteen devices in under 5
wall seconds, hashes equal over two runs, and is never modelled slower than its floor.

## test_range_max_tree.cpp

Against a plain row of slots, random range additions and range maxima agree, and `peak_with()`
answers the peak under four further additions without changing the row.

## test_fnv_hash.cpp

Folding two values hashes their sixteen little-endian bytes, against a byte-wise FNV-1a reference with
the published constants.

## test_partition_refusal.cpp

A pool's shortfall is the bytes required above the usable ones, and zero when the pool holds them.
`to_error()` keeps the refusal's class and names each pool's required, usable and missing bytes.

## test_partition_assignment.cpp

`home_of()` returns the home of a homed buffer and nothing for another.

## test_assignment_hash.cpp

Equal assignments hash equal; another device or the floor flag changes the hash.

## test_split_sizing.cpp

`size_split()` sizes parts by speed and they sum to the extent; the rounding remainder goes to the
fastest device, the first of equals; no device gets two parts and one with an infinite time gets
none; an axis shorter than the device count gives fewer parts; equal inputs give equal parts; and a
zero extent or no usable device gives none.

## test_split_expansion.cpp

On a three-node chain whose middle node is cut 7 to 3, `expand_splits()` gives one placed node per
part with its share of the operations and of the written bytes, edges into the cut node reach each
part whole and edges out of it carry each part's share, and every edge still runs forward. The reader
of the cut node's output reads the parts' slices.
`planned_splits()` gives disjoint written ranges that cover the buffer and whole read ranges, as
`sliced_split()` declares them. When the split's regions name a column panel of a 2 x 10 row-major
matrix, one range per row, the planned parts read and write exactly those ranges, and a part is
priced by their sum: 14 bytes written and 50 read for the 7-column part, its slice and its outgoing
edge 14 bytes. A node with one part stays whole.

## test_pool_occupancy.cpp

A buffer two nodes touch is counted once per pool, and a buffer with a stated home is held in its
pool for the whole step. A chain holds only the two buffers live at each link, whatever order its nodes
are added in, and a buffer read before any write is live for the whole step.

## test_partition_model.cpp

Devices that share a pool count it once and copy nothing between them, an edge across two pools adds
its copy to the step, a backward edge is refused, a node that writes a buffer with a stated device is
fixed to it, and a node placed on a device with no profile is refused.

## test_fill_plan.cpp

A seven-node `state_chain()` over three devices whose pools hold three nodes' buffers fills the fastest device,
then the next, then the slowest, and crosses each device boundary once. When nothing is left, the
last device takes the rest and the walk reports that it does not fit. The floor is a fitting plan,
and there is none when no subset fits. `floor_candidate_count()` is 1, 6 and 136 for one, three and
sixteen devices. With a third device added, the floor is no slower than the fill plan of either
prefix of the old two devices.

## test_list_scheduling_partitioner/

`ListSchedulingPartitioner` has three test files, so its tests sit in a folder of that name.

### test_placement_and_refusal.cpp

A node larger than every pool, and a job larger than the pools together, are refused as
`AllocationFailed` with the bytes and the shortfall of each pool. A chain whose buffers exceed the
pool together fits by live intervals. Two partitioners built from one
profile set give assignments that hash equal, and so does a second run of one partitioner. A planned
buffer is homed on its writer's device, the plan is never modelled slower than its floor, and a
device with no profile receives no node.

### test_partition_properties.cpp

For each of 64 fixed seeds, `Small_PartitionProperties` builds a graph of three to eight nodes over
two or three devices whose buffers fit one pool, and checks three properties; a failure names its
seed. The partitioner's modelled step time, in the order its schedule runs the nodes, is within
10% of the best that an exhaustive search over every fitting placement, in recording order, finds. No plan is modelled slower than its floor. With a device a thousand
times slower added, that device gets no node. The search counts `devices ^ nodes` placements, at most
6561 here, and prices each with `PartitionModel`.

### test_splitting.cpp

A node that writes 1200 bytes, more than a pool's 900 usable, and declares a split of extent 12
runs as two parts on two devices of speed 2 and 1: 8 and 4 indices, writing bytes [0, 800) and
[800, 1200), the ranges `sliced_split()` of `tests/support/partition_problem_builder.hpp` declares. With `Splitting::Off` the same node is refused, and a node that fits whole has no
parts. A node with no declared split is not cut, nor is one whose split has no `regions` function, and a split plan hashes equal across runs. The
parts, run through `dispatch_split()` and `join_split()` on a CPU queue, give the bytes of one slice over the whole
extent; the test skips without a CPU device.

`Large_ListSchedulingPartitioner.ASplitProductStepIsShorterThanWithSplittingOff` runs on the first
machine and skips with fewer than two GPUs. It times a product kernel whole on each GPU, builds the
profiles from those times, and runs the parts the partitioner gives with splitting allowed and
with it turned off; the split run, best of three after a warm-up, must take less wall time.

## compile_buffer_range.cpp, compile_partition_measurement.cpp, compile_partition_problem.cpp, compile_partitioner.cpp and compile_partitioning_log_category.cpp

Each file includes one header alone.
