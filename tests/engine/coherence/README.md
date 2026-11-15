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

## compile_instance_allocator.cpp, compile_peer_access.cpp, compile_planned_node_factory.cpp, compile_profile_probe.cpp, compile_space_queues.cpp, compile_transfer_observer.cpp and compile_transfer_path.cpp

Each file includes one interface header alone.

## test_same_context_path.cpp

The path connects spaces of one context and no others, copies the bytes on the CPU queue, and tells
its observer the name `same_context`. The tests skip when SYCL offers no CPU device.

## test_peer_path.cpp

The path connects two contexts only when access is granted and leaves one context to
`SameContextPath`. It copies the bytes and names itself `peer`. A plan over both paths takes
`same_context` for the copy inside a context and `peer` for the copy across contexts; the planned
copy nodes move the bytes, and the observer records which path each copy took.

## test_coherence_planner.cpp

A node on device 1 reading a buffer written on device 0 gets an allocation and a copy, and its
write makes a later reader on device 0 copy back. A node on the device that holds the bytes gets no
step. Of two valid sources at one cost, the first space in the given order is the source, and two
plans of one input are equal. The cheapest path chooses the source, and a copy no path makes is
refused as `Unsupported`.

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
nodes unchanged.

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
