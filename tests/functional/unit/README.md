# Unit tests

The tests in `tests/functional/unit` each exercise one component. This document records what each
test proves and why it is built the way it is. A section is named after a test file, and a
subsection after the test, helper or topic it describes. The source comments state one invariant per
line and cite this file.

## test_affinity.cpp

Thread-to-core pinning is best-effort: on failure it logs a warning and does not throw. The tests
assert that no call throws. They pin from a worker thread, so the affinity of the test runner is
left untouched.

## test_delegate.cpp

The file covers `Core::Delegate`: the empty state, invocation and return values, argument
forwarding, move semantics (a move leaves the source empty), destruction of the erased target, and
that a move-only target is held without a copy.

### `DestroysTarget`

The lambda owns a copy of the `shared_ptr`. Destroying the `Delegate` must run the destructor of the
lambda and release that copy.

## test_dependency_tracker.cpp

The file covers hazard resolution in the lock-free `DependencyTracker`. It pins the edge semantics
on raw nodes: read-after-write, write-after-read and write-after-write each add an edge, two readers
stay parallel, a shared buffer produces one edge and not several, and a node never depends on
itself. The observable is `in_degree`, the count of incoming edges that the scheduler decrements.

## test_distributed.cpp

The tests of the distributed layer in this file need no hardware. They cover wire-protocol
serialization, the in-process transport switchboard that simulates a cluster on one machine, and the
registries and policy on the master. An integration test,
`tests/functional/integration/test_distributed_e2e.cpp`, covers end-to-end kernel offload.

### `BufferRegistryVersioning`

Registering the same pointer again raises the version and keeps the existing id. The version is how
a stale handle is detected when a USM address is reused.

### Offload policy tests

The offload policy tests drive `pick_worker()` of `LeastLoadedOffloadPolicy` with synthetic
`WorkerStats`. They validate the cost model (round-trip time, transfer and queue backlog) and its
gates without a live cluster.

### `live_worker`

`live_worker` creates synthetic `WorkerStats` for the policy tests.

### Malformed wire input

The distributed layer sits on an unauthenticated trust boundary. Every deserialize path must survive
garbage without a crash, without reading past the end, and without allocating an amount an attacker
chose. The tests of the `Unit_DistributedProtocolFuzz` suite exercise the surfaces where findings
C1, C3 and H7 of `docs/archive/slop/CODE_AND_ARCHITECTURE_REVIEW.md` were: a blob length prefix that
is oversized or overflows, a truncated stream, and an empty payload.

### `GetBlobRejectsOverflowingLength`

A blob length prefix so large that `pos_ + bytes` wraps around must be rejected by the bounds check,
which compares `bytes > size - pos` and performs no addition. Otherwise the call would yield a span
about 2^64 bytes long. This is the regression behind finding C3.

### `GetBlobRejectsOversizedLength`

A length prefix that is larger than what remains, without overflowing, is still out of bounds and
must fail cleanly.

### `BufferPushSurvivesOverflowingBlobLength`

`BufferPushMsg::deserialize` on a hostile oversized length must not crash and must not attempt an
allocation of 2^64 bytes. It should return an empty payload.

### `BufferPushSurvivesTruncatedStream`

A truncated stream, with a handle and no blob, deserializes to an empty payload and does not read
past the end.

### `TaskResultSurvivesTruncatedOutputs`

A `TaskResultMsg` that claims more outputs than the buffer can hold must stop at the first failed
extraction and must not loop past the end.

### `DeserializeEmptyPayloadIsSafe`

Deserializing an empty payload does nothing and yields empty fields. It never reads out of bounds.

## test_epoch_reclamation.cpp

The file covers epoch-based reclamation. `EpochManager` is a process-wide singleton, so the tests
assert observable contracts: retired objects are eventually deleted once, `EpochGuard` enter and
exit are balanced, and a thread that retires and then exits hands its nodes to the global orphan
list.

### `Tracked`

`Tracked` is a heap payload that increments a shared counter when it is destroyed, so a test can
observe reclamation without reaching into `EpochManager`.

The counter is a `shared_ptr` and not a raw pointer to an atomic on the stack. Reclamation is
deferred, so a tail of retired objects can be freed during a later test. A raw stack pointer would
then be written after the frame of the test is gone, which is a stack-use-after-return. The
`shared_ptr` keeps the counter alive as long as any `Tracked` refers to it.

### `retire_and_drain`

`retire_and_drain` retires a number of objects and then runs extra guarded passes to advance the
epoch. The tests use it to drain pending nodes.

### `RetiredObjectsEventuallyReclaimed`

Most objects should be gone. The survivors are those retired in the final epoch, which has not
advanced yet. The bound is generous so that the test does not flake.

### `ThreadExitHandsOffOrphans`

When the thread exits, its destructor donates the remaining retire list to the global orphan list.

The main thread then drains. Each guarded retire triggers `check_and_reclaim`, which pulls in the
orphaned nodes and frees those past the safe epoch.

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

## test_fixed_order_reduce.cpp

The file covers `Graph::add_reduce`, `Graph::add_segmented_reduce` and the fixed-tile plan under
them. A parallel reduce also gets the sum right, so a correct sum is not the claim under test. The
claim is that the combination order is a function of the element layout alone, which makes the bit
pattern reproducible.

The strongest available check is a host reference that performs the identical fixed-tile fold,
compared bit for bit. If the device result matched some other order, the two would differ in the low
bits of a float sum. The inputs are built so that the naive left-to-right sum differs from the tiled
one, and that difference is what lets the comparison fail.

### `fold_level`

`fold_level` is the host mirror of `detail::fold_tile` over a whole level. It folds each tile of at
most `TILE` values left to right, seeded with the first element of the tile.

### `fixed_order_sum`

`fixed_order_sum` returns the value the runtime is required to produce for a sum: repeated levels of
`fold_level` until one value remains. It mirrors `detail::plan_reduction`.

### `order_sensitive_input`

`order_sensitive_input` builds a sum whose result depends on the order in which the values are
combined: one large magnitude, followed by many values small enough to vanish when added to the
running total but not when added to each other first.

### `bit_equal`

The `double` overload of `bit_equal` does for `double` what `bit_equal(float, float)` does for
`float`. The byte-identity checks need it, because `EXPECT_DOUBLE_EQ` tolerates a difference of 4
ULP and the late-bound contract is byte identity.

### `PlanLayoutIsPureAndDisjoint`

The plan is a pure function of the element count. Level counts divide by the tile size until one
value remains, and consecutive levels never share scratch.

### `SumMatchesTheFixedOrderReferenceExactly`

The sum matches, bit for bit, a host fold with the identical tile structure. It also differs from
the naive left-to-right sum, which proves that the comparison discriminates between orders.

### `ResultIsStableAcrossSchedules`

Repeated runs of the same graph, and a fresh graph on a runtime that runs the background rebalancer,
must all agree bit for bit. Work stealing and the rebalancer change which worker folds which tile,
and neither may change the result. The test varies the schedule at a fixed pool size. Varying the
pool size is the job of the conformance suite `Integration_Determinism`.

The second runtime takes the same input with the rebalancer on. That is a different scheduling
regime, because task migration reassigns tiles to workers in the middle of a run.

### `EdgeCounts`

The test uses element counts around the tile boundaries and the degenerate ones: a single value, an
empty reduction (which yields the identity), and counts that land exactly on a level boundary.

### `MinimumAndMaximum`

`Minimum` and `Maximum` reduce with the same machinery. Their identities must let any element win.

### `ConsumerSeesTheFinishedResult`

A reduction is an ordinary tracked task. A consumer that reads the output is ordered after the whole
reduction chain, not only after its first level.

### `SegmentedReduceOverRaggedSegments`

The segmented reduce runs over ragged segments, one of them empty. The empty segment must yield the
identity and must not read past its zero-length window.

### `RejectsMisSizedArguments`

Arguments of the wrong size are rejected when the task is recorded, at the call site. They do not
fault on the device in the middle of a run.

### `LateBoundMatchesFixedCountExactly`

A `LateCount` reduce planned for a capacity must match the fixed-count overload bit for bit at every
live count tested. The design promises the same fold order for a live n as the fixed-count path
would produce for that n.

### `LateBoundChangingCountKeepsOneCompile`

A late-bound reduce whose live count changes between runs must not recompile. The topology (levels,
scratch and node count) is fixed at the capacity, and only the live count varies per step.

### `LateBoundAboveCapacityFailsTheRun`

A `LateCount` above its capacity fails the run. The alternative is a read past the buffers the
reduce was planned for.

## test_late_count.cpp

The file covers the tiled level plan (`detail::plan_tiled_levels` and `live_tile_levels` in
`api/detail/fixed_order_scan.hpp`) and the one resolution point of `LateCount`
(`detail::resolve_late_count` in `api/graph/graph.hpp`). Both are pure host functions, so the suite
needs no device and no `Runtime`.

### `LevelCountsAtTileBoundaries`

The level count for a capacity is the tree that `add_reduce` already plans. The test exercises it at
the boundaries the tile size creates.

### `LiveCountBelowCapacityMatchesThatCountAtCapacity`

A live count below the capacity must fold into the tile boundaries that the same count would get as
a capacity. The levels below the point where the live chain reaches one value line up, index for
index, with `plan_reduction(n)`.

Every level past what `plan_reduction(n)` needs is a pass-through of a single element, and folding
it changes nothing.

### `DefaultResolvesToCapacity`

`resolve_late_count` is the one resolution point. With neither a host provider nor a device slot,
the live count is the capacity, as in a fixed call.

### `AboveCapacityIsRejected`

A live count above the capacity fails the run, as an out-of-window `based_at()` does. It is neither
clamped silently nor allowed to read past the end.

### `DeviceSlotResolvesFromHostResidentBuffer`

A `Residency::Host` device slot resolves to the value a predecessor wrote into it, exactly as
`sized_from_device()` reads its own `device_count` slot.

### `NonHostDeviceSlotIsRejectedAtRegistration`

A device slot that is not `Residency::Host` is rejected at registration, the way
`require_host_device_slots()` rejects a non-host slot of `sized_from_device()`. It does not wait to
fail on the first run of the graph.

### `RadixDigitCountAtByteBoundaries`

The test checks digit counts at the 8-bit boundaries. A key range of 256 still fits one digit and
257 needs two. Zero and one take one digit.

### `RadixPlanSkipsDigitsAboveTheLiveKeyRange`

A plan for three digits whose live key range needs fewer runs only the low digits, and the last
active digit always places into the indices.

## test_node_dispatch_width.cpp

The width of a node is stamped after the node is added, as its name is. `add_node` returns `void`,
and the emitters build the node before they know which of several internal widths it took.

`Node` is a non-literal type, so `Node{}` cannot be an operand of a `static_assert`; the compiler
reports "non-literal type 'Node' cannot be used in a constant expression".

### `BakedNodeReportsOneWidthTwice`

A node with no late bindings reports its baked width as both launched and live, because nothing
narrows it at run time.

### `DynamicNodeSeparatesCapacityFromLiveCount`

A dynamic node launches over its capacity and does work over its live count. The instrument has to
show both, or the narrow kernels look wide.

### `UseBakedFallsBackToTheBakedWidth`

`USE_BAKED` means "the rest of the capacity" and is not a count, so it must not be reported as a
measured live width.

### `OversizedWidthReportsUnknownRatherThanTruncating`

A width that does not fit in `uint32_t` is reported as unknown, not truncated. `launched_width` is
32 bits wide, and a wrapped value would read as a real, wrong measurement.

## test_node_event_record.cpp

The file covers the layout of `Core::NodeEventRecord` and checks that `RunReport::reset()` keeps the
capacity of `node_events` while it zeroes the clock pairing. The engine copies `NodeEventRecord`
field for field with no translation, so the tests pin its size and triviality.

### `LayoutIsFortyBytesAndTriviallyCopyable`

The record is 40 bytes and trivially copyable, which is the shape the engine backend copies with no
translation. A change of padding or an added field breaks that contract.

### `ResetKeepsNodeEventsCapacity`

`reset()` must not free the storage of `node_events`. Every other vector of `RunReport` already
keeps its capacity across a reset.

### `ResetZeroesClockPairing`

`reset()` must zero the clock pairing, so that a stale pairing from a previous run never survives
into a report that reads as freshly reset.

### `WidthsDefaultToUnknown`

The widths default to `WIDTH_UNKNOWN` and not to zero. Zero is a real dispatch width on an empty
pass and would read as a measured value.

## test_numa_steal.cpp

The file validates NUMA-aware hierarchical work stealing on single-node hardware by injecting a
synthetic topology. A `RemappingTopologyProvider` spreads the cores across N nodes (core i goes to
node i % N) with a matching `MatrixDistanceModel`. The tests then assert the locality invariant:
victims on the local node come before remote ones.

## test_nvidia_topology.cpp

The file validates that an NVIDIA GPU (a GTX 1060, sm_61) is discovered through the CUDA SYCL
backend and mapped to a NUMA node by hwloc. The tests skip when there is no CUDA device.

## test_range_task.cpp

The file covers `RangeTask::split`, the primitive for divisible workloads: the grain threshold, that
`split()` yields a distinct sub-task while work remains, that repeated splitting shrinks the owner
and terminates, and that `split()` returns `nullopt` below the grain. All tests are deterministic
and run on the host.

### `SplitHandsOffDistinctChunksUntilExhausted`

The test keeps every chunk alive. An address that was freed and reused could otherwise look like a
duplicate, and the test could not assert that the chunks are distinct.

## test_ref_counted.cpp

The file covers the intrusive reference count `RefCounted` and its smart pointer `sushi_ptr`: single
ownership, copy and move semantics, `leak()`, polymorphic deletion, and concurrent counting without
a race.

## test_registration_index.cpp

The file covers `Node::registration_index` and the grouping of the tile levels of
`Graph::add_reduce()` under one index. The design is sections D and F5 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`.

### `AddReduceGroupSurvivesRebuild`

The group of `add_reduce()` must survive a rebuild onto a fresh `BackingGraph`, whose registration
counter restarts at 0.

The test adds a plain node after `add_reduce()`, which marks the graph dirty, and then runs twice,
so that `build_plan()` replays every spec into a second `BackingGraph`. The levels of the reduce
must still share one index, distinct from the index of every other node, and the indices of all
nodes must be dense from 0 to n-1 in registration order.

### `LateBoundAddReduceGroupsTileLevelsUnderOneIndex`

The `LateCount` overload of `add_reduce()` groups its tile levels, and the trailing identity write
for a zero count, under one index.

The test mirrors `AddReduceGroupsTileLevelsUnderOneIndex`, but the count is bound through a
`LateCount` host provider planned for a capacity of at least three tile levels. That exercises the
registration token of the dynamic emitter and not the fixed-count path.

## test_thermal_rebalance.cpp

The file drives the real `ThermalProxyStrategy` with a fake telemetry and a fake migrator. The rule
of the strategy is to migrate work away from a throttled worker, whose queue backs up while its
completion crawls, toward the least-loaded one, and to leave alone a long queue that still drains
fast. `get_completion_rate()` is cumulative, so on the first `evaluate()` the delta equals the
reported total.

## test_topology.cpp

The file covers hardware discovery and NUMA mapping, edge cases included. The invalid-NUMA fallbacks
check that the runtime degrades without failing on bad input.

## test_topology_abstraction.cpp

The file validates the injectable topology abstraction, `ITopologyProvider` and
`INumaDistanceModel`. It exercises multi-NUMA logic deterministically on single-node hardware, which
the real hwloc path cannot do on a machine with one socket.

## test_usm_registry.cpp

### `fake`

Registry keys are opaque and nothing is dereferenced, so distinct fake addresses are enough, and the
test needs no `Runtime`.

### `EXTENT`

The fake addresses sit 64 bytes apart, so a 64-byte extent covers exactly one of them and never
reaches its neighbour.

### `TakeReportsTheOwnerAndForgetsThePointer`

The free path erases the entry and reads the owner in one step. A second free of the same pointer
must therefore be distinguishable from the first and must not report a stale device.

### `RecordingAPointerTwiceKeepsTheLatestOwner`

Recording a pointer twice is reachable only if an allocator handed back an address the registry had
not been told was freed. The last writer wins, which matches the map assignment the runtime has
always done. The test asserts it so that a future change of container does not quietly make the
first writer win.

### `ConcurrentRecordAndTakeLoseNothing`

Buffers are constructed and destroyed from whichever thread owns them, so several threads write the
registry at once. Every pointer the test records is taken exactly once, by exactly one thread.

### `ContainingAllocationResolvesAnInteriorPointer`

The graph builder needs this case: a caller offsets into a plan array and hands over an address that
the table, which is keyed by base address, cannot find.

### `AnEntryWithoutALengthResolvesOnlyAtItsBase`

An adopted address whose importer knew no size must not read as zero-length and must not swallow
offsets it cannot vouch for.

## test_work_stealing_deque.cpp

The Chase-Lev deque must never drop a task on overflow. A dropped task leaves the `in_degree` of its
successors above zero for good and deadlocks the run. The checks drive the ring past its fixed
capacity, single-threaded for exactness and with concurrent stealers for the lock-free path, and
assert that every pushed task comes back out exactly once.
