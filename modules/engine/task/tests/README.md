# task tests

The tests of the `task` module, built into one program, `sr_task_tests`, which
`modules/engine/task/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_task_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_node_placement.cpp

The file covers the placement a node states and the rule `Graph::placement_of()` applies to it:
the owner of the first write region, else of the first read region, else the stated placement, else
device 0. `modules/engine/scheduler/README.md` states the rule under "Queue selection", and the
scheduler's own `test_node_placement.cpp` checks the queue a dispatch receives.

### `Unit_NodePlacementState`

The two tests need no allocator. `place_on()` stores an index and `stated_placement()` returns it;
an index the 16-bit field cannot hold throws `Errc::invalid_graph` with the index in the message
and leaves the earlier placement in force.

### `Unit_NodePlacementRule`

The fixture holds a `UsmAllocator` over two queues on the device SYCL selects by default, so device
indices 0 and 1 both exist. No test allocates device memory: `adopt()` records a host address as
owned by one of the two, and an address with no record stands for a region no device owns.
