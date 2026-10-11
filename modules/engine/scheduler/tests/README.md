# scheduler tests

The tests of the `scheduler` module, built into one program, `sr_scheduler_tests`, which
`modules/engine/scheduler/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_scheduler_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_event_polling.cpp

`ActiveEventList` must never drop an in-flight event. A dropped event leaves the successors of a
task untriggered and deadlocks the run. The checks pin the inline-then-spill contract on the data
structure, without a live scheduler: they push far past the inline capacity, remove in every order,
and assert that nothing is lost and that swap-with-last identity holds across the boundary between
inline and spilled storage.

### `RemoveAtDrainsEveryEventExactlyOnce`

Always removing index 0 relocates the tail to the front on every step, which crosses the boundary
between inline and spilled storage repeatedly.

### `RemoveAcrossInlineSpillBoundary`

The only spilled entry is the last one, with tag N. Removing an inline slot must move that tail into
the freed slot and shrink the spill to empty.

## test_offload_seam.cpp

The file pins what the scheduler tells an offload coordinator. A coordinator bumps the version of
every buffer a reported node writes and may send an offered node to another machine, so a node of
an aborted run must reach neither call. Each test first runs one node on a healthy run and asserts
the count it expects to stay fixed, so a coordinator that was never called cannot pass.

### `Unit_OffloadSeam`

The fixture builds the scheduler from a topology view, a device set and an allocator, the parts a
`RuntimeContext` would supply, because a module test names no execution header. The pool has two
floating workers. The nodes are native host nodes, so no test submits to a device queue.

### `SingleNodeTopology`

`SingleNodeTopology` and `SingleNodeDistance` describe one NUMA node with no core map, which is all
an unpinned pool reads.

### `CountingCoordinator`

`CountingCoordinator` declines every offer, so an offered node still runs locally and
`wait_quiescent()` covers it.

### `AbortedRunOffersNoNode`

The run is aborted twice: once by `request_cancellation()`, once by a node whose work throws. The
offer count must stay at the one offer of the healthy run both times.

### `SkippedNodeNotReportedAsCompleted`

Three kinds of node did not run to their end: a node of a cancelled run, a late-bound node
disabled for the step and a node whose work threw. None may reach `on_local_completion()`. The poller makes that call before
it drops the in-flight count, so the counter is final when `wait_quiescent()` returns.

## test_node_placement.cpp

The file pins which device queue the scheduler hands a graph node. A kernel that captured USM the
runtime cannot see names no region, so nothing but the placement rule keeps it on the device that
owns its memory. Each node is a host node that submits device work, so the scheduler picks a queue
for it, and its work records the queue it receives and submits nothing.

### `Unit_NodePlacement`

The fixture builds the scheduler from a topology view, a device set and an allocator, as
`Unit_OffloadSeam` does. The device set holds two entries over the one device SYCL selects by
default, both on NUMA node 0. Each entry has its own `sycl::queue`, and two queues compare equal
only when they are copies of one another, so the queue a node receives names its entry.

### `dispatched_device`

`dispatched_device` runs one node and compares the queue its work received with the plain queue of
each entry. `wait_quiescent()` returns after the work ran, so the read needs no lock. The tests also
read `Node::cached_queue_index_`, the index the scheduler records on the node at its first dispatch,
which would still tell the entries apart if their queues could not.

### `RegionlessNodesRunOnDeviceZeroAndAPlacedNodeOnItsDevice`

Two nodes with no region and no placement both receive entry 0, and a third placed with
`place_on(1)` receives entry 1. A rotation over the NUMA node's devices cannot hand entry 0 to two
dispatches in a row. The test first asserts that the two entries' queues differ.

### `ANodeKeepsItsDeviceOnEveryStep`

Each node is dispatched four times, so every dispatch after the first takes the cached index.

### `ARegionOwnerOutranksTheStatedPlacement`

`UsmAllocator::adopt()` records a host address as owned by entry 1 without allocating device
memory. A node that writes it and states entry 0 still runs on entry 1.

### `AnUnownedRegionLeavesTheStatedPlacementInForce`

A host address with no record has no owning device, so the node runs on the placement it states,
and on entry 0 when it states none.

### `APlacementPastTheDeviceSetFailsTheRun`

`place_on(2)` on a two-entry set fails the run with `Errc::invalid_graph`. The message names the
node, the index and the device count, and the node's work does not run.

## test_range_task.cpp

The file covers `RangeTask`, the primitive for divisible workloads: the grain threshold, that
`split()` yields a distinct sub-task while work remains, that repeated splitting shrinks the owner
and terminates, that `split()` returns `nullopt` below the grain, and that the constructor refuses a
reversed range. All tests are deterministic. `SplitPartsCoverTheRangeExactlyOnce` submits to the
default SYCL device; the others construct no queue.

### `IsSplittableAgreesWithSplitAroundTheThreshold`

A range splits when the upper half, which the owner keeps, holds at least 1024 elements, so the
first size that splits is 2047. The test asks `is_splittable()` and `split()` about sizes 1024,
1025, 2046, 2047 and 2048, once from index 0 and once from index 4096, and the two must agree on
each.

### `SplitPartsCoverTheRangeExactlyOnce`

The range [37, 10044) is split as far as `split()` allows, chunks included. `execute()` takes a
`sycl::handler`, so each part runs inside a `queue.submit()`; the kernel records the range it is
given and submits an empty `single_task`. The recorded ranges, sorted, must run from 37 to 10044
with each one starting where the last ended.

### `ConstructorRejectsAReversedRange`

`RangeTask(8, 4, ...)` throws `Core::SushiException`. An empty range, `start == end`, is accepted
and does not split.

### `SplitHandsOffDistinctChunksUntilExhausted`

The test keeps every chunk alive. An address that was freed and reused could otherwise look like a
duplicate, and the test could not assert that the chunks are distinct.

## test_thermal_rebalance.cpp

The file drives the real `ThermalProxyStrategy` with a fake telemetry and a fake migrator. The rule
of the strategy is to migrate work away from a throttled worker, whose queue backs up while its
completion crawls, toward the least-loaded one, and to leave alone a long queue that still drains
fast. `completed_task_count()` is cumulative, so on the first `evaluate()` the delta equals the
reported total.

## test_work_stealing_deque.cpp

The Chase-Lev deque must never drop a task on overflow. A dropped task leaves the `in_degree` of its
successors above zero for good and deadlocks the run. The checks drive the ring past its fixed
capacity, single-threaded for exactness and with concurrent stealers for the lock-free path, and
assert that every pushed task comes back out exactly once.

## test_workload_coordinator.cpp

The file covers the heartbeat loop of `WorkloadCoordinator`. Fakes for all three collaborators
(telemetry, migrator and strategy) keep the test deterministic and on the host. The loop must drive
the strategy, start and stop cleanly, and swap strategies at runtime.

### `wait_until_at_least`

`wait_until_at_least` spins until the value reaches the target or the budget elapses. The tests use
it to synchronize their assertions with the background threads of the coordinator.
