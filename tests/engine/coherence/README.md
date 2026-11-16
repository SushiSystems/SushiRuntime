# coherence tests

The tests of the `coherence` module, mirroring `modules/engine/coherence/`.
`tests/engine/coherence/CMakeLists.txt` builds them; `sr test` runs them with the rest.

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
alone. In `EachPartOfASplitNodeHoldsItsReadsAndItsWrittenRangeComesBackToALaterReader` the part on
device 1 gets the whole buffer before the node, and only the half it wrote is copied back to device 0
for the reader after it.

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
chosen (the unpinned one goes instead and, read again, comes back by evicting another), a space with only pinned or
in-use instances refuses the allocation as `AllocationFailed`, and a plan within capacity frees
nothing and, since it leaves new instances resident, is not steady. A write the host does not read leaves no copy
back and a plan that is not steady; a buffer named in `host_read` is copied back to its home. A
second plan starts from the state the first left, and a planner with no options frees every
instance as before.

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

## test_directory_planned_node_factory.cpp

A node runs on the device of its space and declares its byte range. A copy naming an unknown path
is refused. Allocate, copy and free run through `checked_host_work` and move the bytes; the copy
leaves the map alone and its record node records it; the test skips when SYCL offers no queue. Only
a copy has a record node. An allocator that returns null gives `AllocationFailed`, a copy between a
missing instance gives `InvalidState`, and a failing path's error is passed on.

## test_plan_splice.cpp

The copy runs after the writer's Write and before the reader on the other device, and the copy back
and the free follow the reader. A copy's record node follows it, the reader does not wait for it,
and the free and the leave do. A copy does not wait for an unrelated predecessor of its reader, and
two copies of one source to two devices do not wait for each other. Nodes on the device of their
buffer's only instance get no step and no effect, so the graph is left as it is, and a refused plan
leaves the nodes unchanged. An eviction runs after the node that wrote the instance and before the
allocation it makes room for. Every node the splice adds is marked a planned step and the recorded
nodes are not, so the timing record leaves out only the added ones.

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

## test_usm_instance_allocator.cpp

The instance of a space is made on its device and released there with no ERROR, host memory is
released on the device it was made on, a device space names device USM and the host space host
USM, and a space past the devices is refused.

## compile_coherence_log_category.cpp, compile_eviction_policy.cpp, compile_instance_allocator.cpp, compile_planned_node_factory.cpp and compile_residency_options.cpp

Each file includes one header alone.
