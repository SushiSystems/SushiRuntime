# coherence tests

The tests of the `coherence` module, mirroring `modules/engine/coherence/`.
`tests/engine/coherence/CMakeLists.txt` builds them; `sr test` runs them with the rest.

`fake_transfer_path.hpp` holds the doubles: `FakeTransferPath` connects every pair of spaces and
copies with `memcpy`, `ClosedTransferPath` connects none, and `FixedMemoryProbe` reports one
gigabyte free.

## compile_instance_allocator.cpp, compile_planned_node_factory.cpp and compile_transfer_path.cpp

Each file includes one interface header alone.

## test_coherence_planner.cpp

A node on device 1 reading a buffer written on device 0 gets an allocation and a copy, and its
write makes a later reader on device 0 copy back. A node on the device that holds the bytes gets no
step. Of two valid sources at one cost, the first space in the given order is the source, and two
plans of one input are equal. The cheapest path chooses the source, and a copy no path makes is
refused as `Unsupported`.

## test_copy_plan.cpp

Two steps compare equal only when every field is equal.

## test_directory_planned_node_factory.cpp

A node runs on the device of its space and declares its byte range. A copy naming an unknown path
is refused. Allocate, copy and free run through `host_work` and move the bytes; the test skips when
SYCL offers no queue.

## test_plan_splice.cpp

The copy runs after the writer and before the reader on the other device, and the copy back and
the free follow the reader. Nodes on one device are left as they are, and a refused plan leaves the
nodes unchanged.

## test_transfer_cost.cpp

The uniform cost prices every path by its bytes.

## test_usm_instance_allocator.cpp

The instance of a space is made on its device, and a space past the devices is refused.
