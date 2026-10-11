# task tests

The tests of the `task` module, built into one program, `sr_task_tests`, which
`modules/engine/task/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_task_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_node_placement.cpp

The file covers the placement a node states and the rule `Internal::Graph::placement_of()` applies to it:
the owner of the first write region, else of the first read region, else the stated placement, else
device 0. `modules/engine/scheduler/README.md` states the rule under "Queue selection", and the
scheduler's own `test_node_placement.cpp` checks the queue a dispatch receives.

### `Unit_NodePlacementState`

The two tests need no allocator. `place_on()` stores an index and `stated_placement()` returns it;
an index the 16-bit field cannot hold throws `ErrorCode::InvalidGraph` with the index in the message
and leaves the earlier placement in force.

### `Unit_NodePlacementRule`

The fixture holds a `USMAllocator` over two queues on the device SYCL selects by default, so device
indices 0 and 1 both exist. No test allocates device memory: `adopt()` records a host address as
owned by one of the two, and an address with no record stands for a region no device owns.

## test_small_event_vector.cpp

The file covers `Internal::Graph::SmallEventVector`, the event dependencies a node holds: copy, move,
assignment from another vector or from a `std::vector`, and `clear()`. The events are
default-constructed. Each one has its own state, so `==` tells two of them apart and a copy of
one compares equal to it; the tests rely on that.

### `Unit_SmallEventVector`

Each test compares `to_vector()` with the list it built. The assignment tests cover a list that
fits the inline buffer, one that spills past it, one that shrinks the vector, and the vector's own
overflow storage as the source. A moved-from vector has size zero, and self-assignment leaves the
content unchanged.

## test_task_types.cpp

The file covers the operation hash of `OperationID` and the parameter slots of
`Internal::Graph::TaskMetadata`. The hash checks are `static_assert`s, so a wrong value stops the test
program from compiling; they cover two names the repository uses, one name with a byte above 127,
and the unsigned-byte rule the hash applies.

### `Unit_TaskTypes`

One test stores a value in the first and the last parameter slot and reads each back as its type.
