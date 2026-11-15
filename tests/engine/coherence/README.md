# coherence tests

The tests of the `coherence` module, mirroring `modules/engine/coherence/`.
`tests/engine/coherence/CMakeLists.txt` builds them; `sr test` runs them with the rest.

`fake_profile_probe.hpp` holds `FakeProfileProbe`, which answers every probe from fixed rates and a
speed per device, with the builders `host_key()` and `round_profile()`.
`fake_transfer_path.hpp` holds the doubles: `FakeTransferPath` connects every pair of spaces and
copies with `memcpy`, `ClosedTransferPath` connects none, and `FixedMemoryProbe` reports one
gigabyte free.

`transfer_path_doubles.hpp` holds the doubles of the path tests: `CPUSpaceQueues` answers one SYCL CPU
queue for every space, `FixedPeerAccess` grants or refuses every request and counts them, and
`RecordingTransferObserver` keeps each copy it hears.

`partition_problem_builder.hpp` holds the partitioner tests' builders: `speed_model()` makes a
`CostModel` of `round_profile()` devices, `add_devices()` gives each device a pool of its own and
the `"pcie"` path, `add_buffer()` adds a planned buffer, and `chain()` builds a chain whose node `i`
writes buffer `i` and reads buffer `i - 1`.

`random_partition_case.hpp` holds `SeededDraw`, a portable generator, and `random_partition_case()`,
which builds from a seed a graph of three to eight nodes with forward edges and a speed per device.
`best_feasible_step_time()` tries every placement of a model, keeps the ones that fit and honour
fixed nodes, and returns the least step time; `placement_of()` turns an assignment into device
indices of the model.


## test_partition_report_builder.cpp

The rows match the plan: nodes, bytes and modelled time per device, a device with no node as zeros, a
buffer with a stated device counted at it, a refusal with the shortfall of each pool, the answer of
`ListSchedulingPartitioner` against its own assignment, and the same report for the same inputs.

## test_dispatch_observation.cpp

A record becomes the device, the declared cost and the seconds; a host record takes the host key or is
dropped; a record with an unknown node, no cost, no operations, an unknown device or no positive duration is
dropped; the order of the records is kept.

## test_profile_fold.cpp

A folded profile prices the observed node at the measured time; the entry is the median, the lower one of
an even count; the generic kind sets the base throughput; the rest of every profile is kept; an observation
that says nothing changes nothing; the order of the observations does not matter.

## test_profile_update.cpp

Through a profile file in the temporary directory: a second run reads the throughput the first one
measured, a run that measured nothing writes the same profiles, a later run folds over an earlier one, and a
missing file is an error that creates nothing.

## compile_coherence_log_category.cpp, compile_instance_allocator.cpp, compile_peer_access.cpp, compile_planned_node_factory.cpp, compile_probe_devices.cpp, compile_profile_probe.cpp, compile_space_queues.cpp, compile_transfer_observer.cpp and compile_transfer_path.cpp

Each file includes one interface header alone.

## test_same_context_path.cpp

The path connects spaces of one context and no others, copies the bytes on the CPU queue, and tells
its observer the name `same_context`. The tests skip when SYCL offers no CPU device.

## test_peer_path.cpp

The path connects two contexts only when access is granted and leaves one context to
`SameContextPath`. It copies the bytes and names itself `peer`. A plan over both paths takes
`same_context` for the copy inside a context and `peer` for the copy across contexts; the planned
copy nodes move the bytes, and the observer records which path each copy took.

## test_host_staged_path.cpp

The path connects any two devices and the host with a device, never two host spaces. A copy
between two spaces with no direct path matches bytes for sizes below, at and several times one ring
slot, makes the ring once and reuses it, and the report names `host_staged` once per copy. A copy to
or from the host is one memcpy with no slot. A copy with no queue on either side is refused.

## test_staging_ring.cpp

The ring makes no slot before the first acquire, makes all slots once, hands them out in ring
order, skips a held slot, frees every slot with the ring and refuses when the memory has no room.

## test_coherence_planner/

`CoherencePlanner` has two test files, so its tests sit in a folder of that name. The helpers of the
randomized test sit beside it: `graph_case.hpp` holds a generated graph and the `Outcome` of a run,
`node_operation.hpp` holds `run_node()`, the integer operation of every node (it folds what the node
reads into a digest and writes bytes from the digest, so a stale read changes later bytes),
`graph_generator.hpp` builds a graph from a seed with a portable generator,
`reference_interpreter.hpp` runs it over one host copy of each buffer, and `planned_interpreter.hpp`
runs it through the planner, `DirectoryPlannedNodeFactory` nodes and `FakeTransferPath`, with each
device a space of host memory. The operation is test code; the runtime holds no math.

### test_planned_steps.cpp

`ThePlanOfAMultiDeviceGraphHashesEqualAcrossRuns` plans a four-node graph over three devices and two
buffers three times, each time with a planner of its own. The steps compare equal, and a canonical FNV-1a
hash of the steps (count, then each field in declaration order) is equal across the runs. The planner
takes no worker count, so this check does not vary the pool; its determinism is a property of the plan
alone.

A node on device 1 reading a buffer written on device 0 gets an allocation and a copy, and its
write makes a later reader on device 0 copy back. A node on the device that holds the bytes gets no
step. Of two valid sources at one cost, the first space in the given order is the source, and two
plans of one input are equal. The cheapest path chooses the source, and a copy no path makes is
refused as `Unsupported`.

### test_randomized_equivalence.cpp

For each of 32 fixed seeds, `Medium_CoherenceEquivalence` builds a graph of two to four devices, two
to six buffers and six to twenty-four nodes with partial read and write ranges, and runs it on the
reference and through the planner. The bytes at the end and the digest of each node's reads are equal,
and the plan leaves no instance outside device 0. A second case runs each seed twice and compares the
plans and the bytes. A failure names its seed, which reproduces the graph. The tests skip when SYCL
offers no queue.

## test_residency_options.cpp

On a device that holds two of three buffers, the instance read furthest ahead is evicted in graph
order, and the eviction plan has equal steps and an equal `plan_hash()` across planners. An instance
holding the only valid copy is copied to the host before it is freed, a pinned instance is never
chosen, a space with only pinned or in-use instances refuses the allocation as `AllocationFailed`,
and a plan within capacity frees nothing and is steady. A write the host does not read leaves no copy
back and a plan that is not steady; a buffer named in `host_read` is copied back to its home. A
second plan starts from the state the first left, and a planner with no options frees every
instance as before.

## compile_eviction_policy.cpp and compile_residency_options.cpp

Each file includes one header alone.

## test_logical_time_eviction.cpp

The furthest next read is chosen, an instance never read again goes first, equal reads prefer the
cheaper write-back, so an instance that needs none goes first, then the first candidate, and no
candidate gives no victim.

## test_pin_set.cpp

A pin holds for its buffer and space only, pinning twice counts once, and unpinning removes it.

## test_plan_hash.cpp

Equal plans hash equal; each field of a step, the order of steps and the steady flag change the hash.

## test_copy_plan.cpp

Two steps compare equal only when every field is equal.

## test_cost_model.cpp

A node's time is its operations over throughput plus its bytes over bandwidth, a kind with no entry
takes the base throughput, a device with no profile has no time, and a device that cannot do the
work takes infinite time. A copy's time is the latency plus the bytes over the bandwidth, and an
unknown path has no time.

## test_device_profile.cpp

A kind with no entry takes the base throughput, and a path is found by name.

## test_device_profile_file.cpp

A profile written once and read back gives equal profiles and the same `CostModel` times, and a
changed file changes them. The write order does not depend on the insertion order, a device missing
from the file has no profile, a newer version is refused naming both versions, malformed text is
refused with its line, a path name with white space is refused on write, and a file on disk reads
back equal.

## test_device_profile_set.cpp

A device has a profile only after it is set, setting again replaces it, and profiles come in
`DeviceKey` order.

## test_directory_planned_node_factory.cpp

A node runs on the device of its space and declares its byte range. A copy naming an unknown path
is refused. Allocate, copy and free run through `host_work` and move the bytes; the test skips when
SYCL offers no queue.

## test_plan_splice.cpp

The copy runs after the writer and before the reader on the other device, and the copy back and
the free follow the reader. Nodes on one device are left as they are, and a refused plan leaves the
nodes unchanged. An eviction runs after the node that wrote the instance and before the
allocation it makes room for.

## test_planned_host_access.cpp

A host space, two device spaces, the fake memcpy path and a heap allocator that counts what it
holds and names a kind for each allocation, so the directory records its instances as the runtime's. `run()` carries out a planned step on them, so a plan runs without SYCL.

| Test | What it proves |
| --- | --- |
| `APlannedBufferIsMintedWithItsSizeAndNoInstance` | `mint()` records the size, makes no instance, and leaves a map that covers the bytes with no space set. |
| `APlannedBufferWithNoInstanceIsAllocatedOnTheFirstNodeNotRefused` | A node writing a planned buffer gets an allocation step on its space and no copy. |
| `APlannedBufferWrittenBeforeAPlanReadsBackEqualAfterANodeOnAnotherDeviceWroteIt` | Bytes written before a plan reach the node's device; what the node writes there reads back equal on the host through one path copy. |
| `AHostWriteInvalidatesEveryOtherSpace` | A host write leaves only the host valid for its range and the device valid for the rest. |
| `BytesNoSpaceHoldsReadAsZero` | Bytes never written read as zero and make no host instance. |
| `AGraphWithNoPlannedBufferGetsThePlanItGotBefore` | A buffer with an instance gets the allocate, copy and free of before, and a buffer with no instance and no map is still left alone. |
| `ReleaseFreesEveryInstanceAndRetiresTheHandle` | `release()` frees the host instance and a device instance and retires the handle. |
| `ReleaseFreesThePlannedInstancesOfAHeldBufferAndLeavesItsHeldInstance` | For a buffer `mint_held()` made, `held_spaces()` names the held space alone, and `release()` frees the device instance a plan made and leaves the held bytes as they were. |
| `AHostWriteIsRecordedAsAHostWriteOfTheHandle` | `write()` raises the handle's host-write mark in the directory. |

## test_profile_measurement.cpp

The profile holds the rates the fake probe reported, a faster device measures faster, only the kinds
of the plan are measured, and a plan with no generic kind, a large copy no bigger than the small one,
or a probe that reports no time is refused.

## test_profile_transfer_cost.cpp

With no model the cost is the uniform one, with a model it is the modelled seconds, and a copy the
model cannot price costs infinity.

## test_transfer_observer_slot.cpp

An empty slot drops every copy, an installed observer hears each copy in order, and installing null
stops the forwarding.

## test_device_profile_probe.cpp

Medium, on a SYCL CPU device; it skips when SYCL offers none. `ProfileMeasurement` measures a device
through `DeviceProfileProbe`: capacity and reserve are the space's, and memory bandwidth, base
throughput and the same-context path's bandwidth are above zero. A path name no path has is
`Unsupported`, a kind with no kernel and a key with no device are refused.

## test_transfer_cost.cpp

The uniform cost prices every path by its bytes.

## test_usm_instance_allocator.cpp

The instance of a space is made on its device and released there with no ERROR, host memory is
released on the device it was made on, a device space names device USM and the host space host
USM, and a space past the devices is refused.

## test_file_path.cpp

`Small_FilePath` writes a temporary file and checks that `FilePath` connects an added file space to
any space that is not a file, streams ranges onto a SYCL CPU device through a two-slot ring with
equal bytes and a peak of pinned memory equal to the ring, reads straight into the host with no
slot, and refuses a space it does not hold or a range past the end of the file. After `remove()` the
path does not connect the removed space, and a new space added under the same id is connected.
`Large_FilePath` writes a file 1 GiB larger than the first CUDA GPU's memory into `temp` under the
working directory, streams it window by window onto the GPU, checks the first and last 4 KiB of each
window, and checks that pinned memory stays within the ring, under the ring plus 512 MB. It skips
with no CUDA GPU or too little disk, and deletes the file.

## compile_partition_problem.cpp and compile_partitioner.cpp

Each file includes one partitioner header alone.

## test_partition_refusal.cpp

A pool's shortfall is the bytes required above the usable ones, and zero when the pool holds them.

## test_partition_assignment.cpp

`home_of()` returns the home of a homed buffer and nothing for another.

## test_assignment_hash.cpp

Equal assignments hash equal; another device or the floor flag changes the hash.

## test_pool_occupancy.cpp

A buffer two nodes touch is counted once per pool, and a buffer with a stated home is held in its
pool from the start.

## test_partition_model.cpp

Devices that share a pool count it once and copy nothing between them, an edge across two pools adds
its copy to the step, a backward edge is refused, a node that writes a buffer with a stated device is
fixed to it, and a node placed on a device with no profile is refused.

## test_fill_plan.cpp

A seven-node chain over three devices whose pools hold three nodes' buffers fills the fastest device,
then the next, then the slowest, and crosses each device boundary once. When nothing is left, the
last device takes the rest and the walk reports that it does not fit. The floor is a fitting plan,
and there is none when no subset fits.

## test_list_scheduling_partitioner/

`ListSchedulingPartitioner` has two test files, so its tests sit in a folder of that name.

### test_placement_and_refusal.cpp

A node larger than every pool, and a job larger than the pools together, are refused as
`AllocationFailed` with the bytes and the shortfall of each pool. Two partitioners built from one
profile set give assignments that hash equal, and so does a second run of one partitioner. A planned
buffer is homed on its writer's device, the plan is never modelled slower than its floor, and a
device with no profile receives no node.

### test_partition_properties.cpp

For each of 64 fixed seeds, `Small_PartitionProperties` builds a graph of three to eight nodes over
two or three devices whose buffers fit one pool, and checks three properties; a failure names its
seed. The partitioner's modelled step time is within 10% of the best that an exhaustive search over
every fitting placement finds. No plan is modelled slower than its floor. With a device a thousand
times slower added, that device gets no node. The search counts `devices ^ nodes` placements, at most
6561 here, and prices each with `PartitionModel`.
