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

## compile_coherence_log_category.cpp, compile_instance_allocator.cpp, compile_peer_access.cpp, compile_planned_node_factory.cpp, compile_profile_probe.cpp, compile_space_queues.cpp, compile_transfer_observer.cpp and compile_transfer_path.cpp

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
instance that needs no write-back and then the first candidate, and no candidate gives no victim.

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

## test_profile_measurement.cpp

The profile holds the rates the fake probe reported, a faster device measures faster, only the kinds
of the plan are measured, and a plan with no generic kind, a large copy no bigger than the small one,
or a probe that reports no time is refused.

## test_profile_transfer_cost.cpp

With no model the cost is the uniform one, with a model it is the modelled seconds, and a copy the
model cannot price costs infinity.

## test_transfer_cost.cpp

The uniform cost prices every path by its bytes.

## test_usm_instance_allocator.cpp

The instance of a space is made on its device, and a space past the devices is refused.
