# Integration tests

The tests in `tests/functional/integration` run a graph through a `Runtime` or the execution engine.
This document records what each test proves and why it is built the way it is. A section is named
after a test file, and a subsection after the test, helper or topic it describes. The source
comments state one invariant per line and cite this file.

Three design documents are cited by path throughout:

- `docs/design/GRAPH_REPLAY.md`, the replay layers and their phases.
- `docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md`, the work packages WP-0 to WP-7 and their
  numbered sections.
- `docs/design/ENGINE_BACKBONE_REFACTOR.md`, the backbone items BB-0 to BB-8.

## test_async_execution.cpp

CPU work and GPU work dispatched through independent per-device queues must run concurrently and
without a race. These tests assert correctness only. The benchmark suite measures the speedup from
the overlap (`AsyncOverlapBench`).

### `CpuGpuConcurrentVectorAdd`

The test submits to the GPU queue and then to the CPU queue without waiting on the GPU. Both submits
return immediately, and the two devices proceed in parallel.

## test_async_step.cpp

The file covers `Graph::run_async()`, `DynamicGraph::run_async()` and the `RunHandle` they return.

The claim under test is that a step can start without blocking the caller, so that a simulation tick
overlaps with work that needs no workers, such as a renderer or an audio extract of the previous
tick. A return value cannot show that a call did not block, so the overlap tests use a rendezvous:
the step's own task waits for a flag that the caller sets after `run_async()` has returned. If
`run_async()` blocked, nobody could set the flag in time and the task would time out. Every wait is
bounded, so a regression reports a failure and does not hang.

### `wait_for_gate`

`wait_for_gate` blocks until the flag is released or the deadline passes, and reports which
happened. It is the body of a host task, so a step can be held open while the test does something on
the calling thread.

### `SubmitReturnsBeforeTheStepFinishes`

`run_async()` must return before the step finishes. The step's host task waits for a flag the caller
sets only afterwards, so the task can see the flag only if the caller kept running.

### `MatchesTheSynchronousResult`

An asynchronous step must be observationally identical to a synchronous one: the same stepped
`State`, driven the same number of times, ends in the same place.

### `StateAdvancesOnWaitNotOnSubmit`

The `State` advance belongs to `wait()`, not to the submit. Until the step has been settled, the
field it just wrote is not yet current. A runtime that advanced on submit would let a reader see the
next field one step early.

### `DestroyingAnUnwaitedHandleStillFinishesTheStep`

A handle that goes out of scope unwaited must still finish the step, because the compiled plan
cannot be destroyed while workers hold pointers into it. The graph must also be runnable again
afterwards.

### `WaitingTwiceThrows`

A second wait on the same handle has no step to wait for. It must throw, because the alternative is
to block on a latch nobody will signal again.

### `ASecondRunIsRejectedWhileAStepIsInFlight`

A second step started while one is in flight, synchronously or asynchronously, must be rejected
before anything touches the plan. A synchronous run re-arms the in-degree of every node, so letting
it proceed would rewrite the scheduling state of the DAG the workers are still walking.

### `TaskExceptionSurfacesOnWaitAndLeavesTheGraphUsable`

A task exception surfaces from `wait()`, and the graph stays usable. One failing step must not turn
into a graph that fails forever.

### `ReadyTracksTheStep`

`ready()` reports whether a wait would return immediately, and it does so without blocking. It must
be false while the step is held open and true once the step has finished.

### `DynamicGraphRunsAsynchronously`

The region-partitioned graph behaves the same way: a step overlaps the caller, and a pending
structural change is committed before the step is submitted.

## test_cancellation.cpp

A run must be cancellable from another thread. An otherwise unbounded `run_until` has to return
promptly when a second thread calls `cancel()`, and the pool must stay usable so that a later run
completes normally. Cancellation is reset per run; it does not latch.

## test_chain_layer.cpp

The file covers layer 1 of `docs/design/GRAPH_REPLAY.md` (sections C and G, phase P1).

A chainable plan runs as an in-order chain, matches the scheduler bit for bit, and names the path it
took in `RunReport::layer`.

## test_co_tenancy.cpp

The file covers the co-tenancy contract: the runtime is a tenant of the machine, not its owner.

Inside a game the runtime shares the machine with a main thread, a render thread, an audio callback
and asset I/O. A batch runtime takes every core and pins worker 0 onto the core the host's main
thread runs on. A host has to be able to keep some cores for itself. These tests assert the
mechanism that allows it (a worker count, a core range, and the option not to pin at all) and check
that a narrower pool still executes correctly.

### `expect_runs_correctly`

`expect_runs_correctly` runs a small graph and checks every element, which proves that a
configuration executes and does more than construct.

### `DefaultsToOneWorkerPerCoreFromCoreZero`

The default is the sole-tenant behaviour: one worker per logical core, starting at core 0. A silent
change to the default would move the performance profile of every existing consumer.

### `ReservesTheCoresTheHostAskedFor`

A host that wants cores 0 to `reserved - 1` for its own threads asks for a pool that starts past
them. Both the count and the placement must be exactly what the host requested, because a runtime
that quietly took more cores would defeat the reservation.

### `ASingleWorkerStillExecutesTheWholeGraph`

A pool much smaller than the machine still runs everything correctly. The graph does not depend on
there being a worker per core.

### `FloatPolicyRunsUnpinned`

The `Float` policy leaves placement to the operating system. The pool is still the requested size,
but no worker is bound. That is the right choice inside a container whose CPU allowance is enforced
by quota and not by affinity.

### `AnOutOfRangeCoreRangeIsClampedNotIgnored`

A core range that runs off the end of the machine is clamped, not obeyed. The operating system's
affinity calls accept an out-of-range mask and then do not bind, so without the clamp the runtime
would report a reservation it never made.

## test_completion_latch.cpp

The file covers the reusable completion latch, section 5.1 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md`. `execute()` allocates no promise and no future
per step. One latch on the engine is reset, armed and waited on each step. These tests pin the
behaviour the latch must preserve: a long replayed run drives every step and reaches the right
state, and the same plan re-runs cleanly afterwards, which requires the latch to reset and the
sentinel to re-wire.

### `ReRunsCleanlyOnTheSamePlan`

The test makes three separate `run()` calls on one compiled plan. The latch must reset between them,
and the sentinel, which is wired once, must keep signalling.

### `SingleStepRunsRepeatedlyWithoutLeak`

Many independent single-step runs stress the reset, arm and wait cycle hardest. Each run allocates
nothing on the completion path.

## test_counting_sort.cpp

The file covers `Graph::add_counting_sort()`. Its design is sections A3 and B of
`docs/archive/superpowers/specs/2026-09-16-scan-and-counting-sort-design.md`, and it was built as
task 4 of `docs/archive/superpowers/specs/2026-09-16-scan-and-counting-sort-plan.md`. The tests
check stability against `std::stable_sort`, the digit-count boundaries a live key range crosses,
late-bound replay with one compile, and that every internal pass of one call shares one `node_index`
across steps.

### `HostReference`

`HostReference` is the host reference for every case in the file: a stable sort of (key, index)
pairs by key alone, plus the bucket offsets and counts that follow from it. It is the ground truth
`add_counting_sort()` is checked against, and it does not copy the algorithm under test.

### `StableAgainstHostReferenceOnDuplicateKeys`

The test checks stability against `std::stable_sort` on seeded duplicate keys. A comparison against
`std::sort` proves the case is not vacuous: a plain unstable sort must be able to disagree with the
stable reference on the same input, or the test would pass whether or not `add_counting_sort()` were
stable.

### `StableAcrossMultipleDigitsWhenTheLowDigitTies`

The test checks stability across digits. Every key has the same low byte, so the first, least
significant digit pass ties every element, and only the second digit pass tells them apart. The same
`std::sort` comparison as in `StableAgainstHostReferenceOnDuplicateKeys` proves that the tie
matters.

### `LargeRandomCaseAgainstHostReference`

The large case is seeded and random. n = 10^6 with a 2^20 key range needs more than 256 tiles (10^6
/ 256 is about 3907) and three digit passes. It is the first size and range in the suite that
crosses both boundaries at once.

### `ZeroElementsWritesZeroEverywhere`

With zero elements, every bucket starts and stays at the live count, which is zero, and nothing
beyond the declared window is touched.

### `MaximumKeyOfA20BitRange`

The maximum key of a 2^20 range catches a digit-count or mask error at the top bit, because
`key_range.capacity - 1` sets exactly bit 19.

### `DigitCountFollowsKeyRangeCapacity`

A live key range at or below 2^16 must take two digit passes, and one above it three. Each digit
block is a histogram, a column scan, a bin base and a placement, so the node count the call emits
grows by exactly four per extra digit.

### `LateBoundChangingCountAndKeyRangeKeepsOneCompile`

A late-bound element count and a late-bound key range that both change between runs must not force a
recompile. The capacity fixes the topology; only the live values and the buffers they describe vary.

### `LiveKeyRangeBelowCapacityLandsInIndices`

A key range capacity of 2^24 plans three digits. Live key ranges that need one, two and three digits
must each leave the stable order in the indices, whichever digit turns out to be the last one that
runs.

### `DigitsAboveTheLiveKeyRangeDoNotRun`

The upper digits are skipped. A key that breaks the precondition of the live key range but fits the
capacity is sorted by its low byte alone, which could not happen if the digit above it still ran.
The test is white-box by design; a caller must never pass such a key.

The key 0x0101 has low byte 1, so one digit puts it between 0 and 2, and two digits would put it
last.

### `RegionsRecordScanAndCountingSort`

A `DynamicGraph` region records the prefix scan and the counting sort exactly as a `Graph` does,
each in its own region, with live counts that change between runs.

### `EachRunSortsItsOwnKeys`

The test covers substeps: `run(4)` with keys that change every step, so the sort of each step must
reflect the buffer as it stood when that step ran.

### `SharesOneNodeIndexAcrossStepsAndPasses`

Every internal pass that one `add_counting_sort()` call emits shares one `node_index`, and
`run(steps)` records one invocation per step for each pass. The node event records are specified in
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`.

`key_range.capacity = 50` needs one digit, because 50 - 1 fits in 6 bits. The call therefore emits 1
node for the resolver, total and digit plan, 4 for the one digit block, 1 for the offsets and 1 for
the counts: 7 nodes.

### `InternalNodeWidthsMatchTheDispatchTable`

The `launched_width` of each internal pass must be its own row of the width table; a test that only
checked that some width was stamped would prove less. Every pass reads a region the pass before it
wrote: `plan_region`, then `hist_region`, then `col_total_region`, then `bin_base_region`, then the
shared indices buffer, then the offsets. `report.node_events` therefore lands in that dependency
order whichever execution path ran the passes.

The shape is that of `SharesOneNodeIndexAcrossStepsAndPasses`: `key_range.capacity = 50` needs one
digit, `T_cap = next_live_tile_count(300) = 2` and `DIGIT_BINS = 256`. The node order is resolver,
histogram, column scan, bin base, placement, offsets, counts.

### `LateBoundCountAboveCapacityFailsTheRun`

A live element count above its capacity fails the run. The alternative is a read or a write past the
buffers the sort was planned for.

### `LateBoundKeyRangeAboveCapacityFailsTheRun`

A live bucket count above its capacity fails the run too. The offsets pass never reads the live key
range (section A3 of the design document), but the `key_range` resolver still validates it on every
run.

## test_cross_context_guard.cpp

A single kernel cannot dereference USM from two device contexts that are not peers. The scheduler
detects such a node on its first dispatch and fails the run with a precise error, before the node
can make an illegal cross-context access. The guard is section 5.4 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md`. Splitting data together with the work across
devices belongs to work package WP-5 of the same document, and until that is built the guard
enforces the co-location contract. The check needs at least two device contexts, so the test skips
on a single-device machine.

### `NodeSpanningTwoContextsFailsCleanly`

The node reads data in context 0 and writes data in context 1, and no single queue can run it
correctly.

The run fails with a clear error and not with an illegal memory access, and the runtime stays usable
afterwards, which is the error-isolation contract.

## test_determinism.cpp

The file is the determinism conformance suite: the same graph on the same machine must produce
byte-identical output however many workers ran it.

Rollback netcode and replay files are built on this guarantee, and two independent things can break
it. The scheduler can: if the combination order of a reduction depended on which worker picked up
which tile, a two-worker run and a sixteen-worker run would disagree in the low bits and nothing
would report an error. The compiler can: fast-math licenses reassociation, so the same source may be
folded in a different order in two builds, and FMA contraction rounds once where the source says
twice. `Unit_FixedOrderReduce` pins the order of the reduction at a fixed pool size. This suite
varies the pool size, which the `worker_count` setting from item BB-2 of
`docs/design/ENGINE_BACKBONE_REFACTOR.md` makes possible, and it separately asserts the compiler
contract, which no scheduling discipline can replace.

Every comparison is on the raw bytes. A check with a tolerance would pass on exactly the divergence
that ruins a replay a thousand steps later.

### `order_sensitive_value`

The input is one large value followed by many small ones. Each small value vanishes when added to
the running total but survives when the small values are summed together first. A sum over this
input gives a different number for a different combination order, so the bit comparison can fail.
Without such an input the test would pass on a runtime that reassociated freely.

### `alternating_value`

`alternating_value` is a second pattern that alternates in sign, so that a divergence which happened
to cancel in the first reduction still shows in the output.

### `run_workload`

`run_workload` runs one fixed workload on a runtime built for `worker_count` workers, where 0 means
the whole machine, and copies the results out. Two independent reduction chains compete for the
pool, so the worker that folds a given tile varies with the pool size. A map then mixes both totals
into every output element, so a divergence in either chain reaches the bytes being compared.

### `bytes_equal`

`bytes_equal` compares bytes, not numbers. A difference of one ulp must fail, and NaN payloads must
compare as the bits they are.

### `first_difference`

`first_difference` returns the index of the first differing element, so that a failure message can
name the element.

### `TheFloatingPointContractReachesConsumerCode`

The test asserts the compiler half of the contract where a consumer would feel it. This translation
unit is not part of the library and compiles against the installed-style interface, so a definition
that arrives in it is evidence that the floating-point contract travels with the target and does not
stop at the library edge. Restoring `/fp:fast` or `-ffast-math` fails the test at compile time,
which is the property item BB-3 of `docs/design/ENGINE_BACKBONE_REFACTOR.md` asks the suite to have.

### `HostArithmeticIsNotReassociated`

The test catches reassociation at run time, without a macro, so it still works on a compiler whose
fast-math mode does not announce itself. The two expressions hold the same three values in the same
order with different grouping, and they are required to disagree. A compiler allowed to reassociate
would rewrite one into the other and they would match.

### `OutputIsByteEqualAcrossWorkerCounts`

This test is the core claim of the suite. One worker, two workers and the whole machine must agree
on every byte of the output.

The test guards against a vacuous pass: if every configuration collapsed to the same pool, the
schedule would not have varied at all.

### `RepeatedConfigurationsAgreeByteForByte`

The same configuration, run again from scratch, must land on the same bytes.
`OutputIsByteEqualAcrossWorkerCounts` varies the pool size. This test varies nothing and relies on
the run-to-run scheduling jitter of an unchanged pool (which steal wins a race, which worker parks)
to expose a dependence on order.

### `ReplayDoesNotDriftAcrossSteps`

A replayed graph must not drift. Every step of a multi-step run recomputes the same reduction from
the same input, so step 1 and step `STEPS` have to produce the same bytes. A drift would mean that
per-step state of the plan had leaked into the arithmetic.

## test_device_count.cpp

The file covers `API::sized_from_device`: a task whose iteration size is a count that a predecessor
kernel produced within the same step.

A collision pipeline has this shape. The broadphase writes a pair count, the narrowphase iterates
exactly that many pairs and writes a manifold count, and the solver iterates exactly that many
manifolds. None of those numbers exists when the step boundary polls the host-side `sized()`
providers. Without `sized_from_device` the choices are a readback in the middle of the step or a
dispatch at the worst case on every tick.

### `ConsumerIteratesTheCountTheProducerJustWrote`

A producer kernel writes a count, and a consumer sized from that count iterates exactly that many
elements, in one `run()`. The consumer must see the count this step wrote, not the count of the
previous step and not the capacity.

The producer computes the count on the device. The test reads it through a pointer, so the host
cannot have supplied it.

### `CounterIsRegisteredAsAReadAutomatically`

The caller does not have to remember the edge between producer and consumer. The counter is
registered as a read of the consuming task, so the consumer is ordered after the producer even
though the caller named neither in `Reads()`. Without that edge the consumer could read the count
before it was written and would iterate a stale value, 0 on the first step.

### `ChainedProducerCountsInOneComposition`

A three-stage chain is the real shape: the output count of each stage is the iteration size of the
next, all in one `run()` with one compilation.

### `WhenComposesWithDeviceSize`

`when()` still composes with a size driven by the device. A disabled step runs nothing, whatever the
counter says.

### `CountOverCapacityFailsTheRun`

A count larger than the compiled capacity fails the run with a message that names both numbers.
Truncating would silently drop work, which is the failure `sized_from_device` exists to remove.

### `RejectsACounterThatIsNotHostResident`

A counter the host cannot read at every moment of a run is rejected where it is written, not in the
middle of a run. `Device` memory is never host-addressable, and `Shared` memory is managed memory
the host may not touch while a kernel runs on some backends.

### `AcceptsAnInteriorPointerIntoAHostSlot`

A caller with one plan array per device offsets into the array and hands over the interior address.
Nothing else resolves an interior pointer to its allocation kind, so `add()` must resolve the
pointer to the allocation it lies in before judging it.

### `AcceptsAnInteriorPointerIntoAHostBaseSlot`

`based_at_device` reads its slot on the host exactly as `sized_from_device` does, through the same
check, so an offset base slot resolves the same way.

### `RefusesARawSlotThatIsNotHostResident`

The raw-pointer setters take a bare address, so the residency check at `add()` is all that stands
between a device slot and a host read of it in the middle of a run. The refusal has to say which of
the two is wrong.

### `RefusesASlotThisRuntimeNeverAllocated`

A pointer from another runtime or from the stack has no residency this context can read. That is a
different fault from a device slot, and it gets a different message.

## test_device_pipeline.cpp

The file exercises the device-kernel path. `add_node(TaskWork, reads, writes)` hands the engine a
raw `void(sycl::handler&)` body, so the engine owns the submission, the queue choice and the event
wiring. The graph is a diamond:

```
       init (write A)
        /          \
  doubleA          incA      (both read A -> parallel readers)
  (A->B)            (A->C)
        \          /
       combine (read B,C -> write D)
```

With `A[i] = i` the invariant is `D[i] = 2*A[i] + (A[i] + 1) = 3i + 1`.

## test_device_resident.cpp

The file covers device-resident state, work package WP-6 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md`. A buffer or a state allocated with
`Residency::Device` stays on its device and is not host-addressable. The host seeds it and reads it
back through `write_range()` and `read_range()`, while kernels run against it exactly as they do
against shared USM.

### `BufferRoundTrip`

A device-resident buffer reports its residency and is not host-accessible. A host seed, a kernel and
a host readback round-trip correctly without the host ever indexing the allocation.

### `StateStepsResident`

A device-resident `State` steps with the fluent `add(state, fn)` rule and is seeded and read back
through its range API. The field stays resident across all steps, and only the windowed readback
touches the host.

### `SharedDefaultUnchanged`

Shared residency stays the default and is host-addressable. Its range API works alongside direct
indexing, so existing code is unaffected.

### `HostSpanIndexesSharedBuffer`

`host()` is the branch-free bulk access to shared USM: one residency check, then unchecked indexing
over the whole buffer.

### `HostAccessOnDeviceThrows`

A device-resident buffer is not host-addressable. Host indexing throws a deterministic error in
every build; it never dereferences into undefined behaviour.

### `HostBufferIsWrittenByAKernelAndReadOnTheHost`

A host-resident buffer is host-addressable, a kernel on its device writes it, and the host reads the
result by direct indexing.

## test_device_selection.cpp

The file covers what the hardware filter selects and how it fails.

Both properties move a failure earlier. A double-precision kernel on a device without
`sycl::aspect::fp64` fails inside the driver at launch, with a message that names neither the device
nor the aspect; `require_fp64` moves that failure to discovery. A filter that matches nothing would
otherwise surface as "Queue index out of bounds" from the first allocator call, which is the first
thing a player on a machine with no SYCL device would see.

### `any_device_has_fp64`

`any_device_has_fp64` returns true when at least one SYCL device on the lane reports fp64. The
result decides whether a `require_fp64` runtime is expected to construct or to fail on that lane.

### `RequireFp64AdmitsOnlyFp64Devices`

Every device the filter admits reports fp64. The test asserts this against the discovery layer
directly, so the property is checked on the list and not inferred from a count that a deduplication
step could also explain.

### `RequireFp64StillConstructsOnAConformingLane`

The filter does not reject the whole machine: a lane that has an fp64 device still builds a runtime
with `require_fp64` set. The CPU lanes this suite runs on (POCL and the Intel OpenCL CPU runtime)
all report fp64, so this is the branch the test normally takes.

On a lane with no conforming device the failure must still be a `SushiException` from
`Runtime::create()`, and not a driver error from inside a kernel launch.

### `AnEmptyFilterFailsWithUnsupported`

A filter that admits nothing fails at construction with a code a caller can branch on. A machine
with no SYCL device at all has the same shape, which is why the error carries `Errc::unsupported`
and not a bare out-of-bounds message from the first queue lookup.

The message must name the filter, so the reader can tell a machine that has nothing from a filter
that excluded everything.

### `DefaultFilterIsUnaffected`

The default filter admits everything, so the empty-filter check leaves the common path alone: a
plain `Runtime::create()` still finds the devices of the lane.

## test_device_sized_primitives.cpp

The file covers phase P3 of `docs/design/GRAPH_REPLAY.md` (sections D.2 and G).

On every step a producer kernel writes a new live count, 0 and the capacity included. The scan, the
counting sort, the reductions and a `sized_from_device` task size themselves from that count on the
device. The chain and the forced scheduler must record the same per-step digests bit for bit.

## test_distributed_e2e.cpp

The file runs end-to-end offload on a real SYCL device. A master and one or more workers in the same
process exercise the full chain: `compile()` offload stamping, the dispatch branch, the buffer pull,
the remote kernel, the result writeback, the successor release and the graph completion. An atomic
counter in the kernel proves that the work ran on the worker and not on the local fallback.

### `register_state_step`

`register_state_step` registers the State-step kernel `next[i] = cur[i] + (i % 7)`.

The same rule is given as the fluent local fallback, so an offloaded run must match the closed form
`seed[i] + steps*(i % 7)` whichever physical buffer the double-buffer swap leaves the result in.
`ins[0]` is the current field of the step and `outs[0]` its next field; the runtime re-points both
on every step.

### `evolve_distributed_state`

`evolve_distributed_state` evolves a distributed `State<int>` for `steps` steps on an in-process
cluster with one worker and returns its final field.

It builds the cluster with `Distributed::Cluster::attach` on a fluent runtime (`State::distributed`,
`Graph::add(op, State, ...)`). The per-step rule `next[i] = cur[i] + (i % 7)` is registered
identically on the worker, so the caller can check the offloaded result against a closed form. The
helper throws if no SYCL device is present or the worker fails to register, and the caller turns
that into a clean skip.

### `OffloadVectorAddRoundTrip`

The test wires up the in-process cluster with one worker. It constructs both endpoints first so that
they register on the fabric, then brings the master up before the worker. In the other order the
worker's HELLO would reach a master that is not serving yet.

### `FanOutAcrossTwoWorkers`

Per-worker run counters prove which worker ran which task. A short sleep keeps both tasks in flight
so that the least-loaded policy spreads them.

The test asserts the total and not a per-worker split. Both tasks must have run on workers, but
which worker gets which task is a scheduling race. Requiring exactly one each is flaky: under load
the first task can finish before the second is dispatched, and both then go to the same idle worker.

### `WorkerDeathFallsBackToLocal`

The fake worker advertises `VEC_ADD`, heartbeats until it receives a dispatch, and then goes silent
for good: it never sends `TASK_RESULT`, as a worker that crashed in the middle of a task would.

The key assertion is implicit. The `execute()` call must return and not deadlock, although the
worker the task went to never answered.

### `LostBufferPushTimesOutInsteadOfHanging`

The fault policy drops every buffer the master tries to push to the worker, so the worker's input
pull can never complete.

The graph runs on a side thread, so a regression that hangs for good fails the test by timeout and
does not block the whole suite.

### `FluentDistributedStateParity`

The test checks transparent offload of a double-buffered `State` across even and odd step counts.

A `State` rotates its two allocations every step, so the compiled plan cannot bake its offload
handles. The runtime re-points the live pointer lists on every step and the coordinator resolves
handles at dispatch. An even and an odd number of steps leave the logical result in different
physical buffers, and no other test has that property, so this one is the sharpest check that the
live-IO indirection follows the swap. Both runs must match the closed form `seed[i] + steps*(i %
7)`, and both must have offloaded.

### `ClusterDetachAndDisplacement`

The test attaches three masters to one bare context, each on its own fabric with no worker.
`detach()` must leave `offload()` null, and a second `detach()` on the same handle must do nothing.
A third `attach()` displaces the second master; the second handle's `detach()` must then leave the
third installed, because a displaced handle no longer points at the context.

### `ClusterHandleOutlivesRuntime`

The test destroys the context while two copies of a handle are alive, then calls `detach()` and
reads counters. Under ASan this is the check that a handle never follows a pointer into a destroyed
context and that the master outlives the engine that held its address.

## test_distributed_jepsen.cpp

The file is a soak test in the style of Jepsen. It runs an offload graph under a seeded random fault
policy, the nemesis, which drops, duplicates, holds, reorders and partitions messages. It asserts
the safety property the offload model reduces to, determinism under faults: whatever the fault
schedule, the authoritative buffers must end byte-identical to a fault-free run (every `y == 2*x`).
It also asserts liveness: `execute()` must return.

The runtime is an offload accelerator with a single authoritative writer, the master. For that model
the determinism check is the practical equivalent of a linearizability check for a database. It
costs far less, and it is exactly the contract the runtime promises.

### `run_one_scenario`

The cluster assembles cleanly before any fault is injected, so the test exercises recovery at task
time and not the loss of a registration.

The nemesis is a per-message fault policy with a seed. The fabric mutex serializes the calls to it,
so one shared random number generator is safe.

The graph runs on a side thread, so a liveness regression fails by timeout and does not block the
whole suite.

The scenario tears the nemesis down and lets every in-flight delivery settle before it reads the
results and frees the memory.

## test_dynamic_graph.cpp

The file covers the dynamic, region-partitioned graph (`Runtime::dynamic_graph()`). Each test
asserts the numerical outcome and the composition behaviour: regions run together, data dependencies
across regions order correctly, dropping a region removes its work, and a run without a mutation
never recomposes.

### `IndependentRegionsRunAndComposeOnce`

Two independent regions each own a buffer and increment it once per step. Over several steps both
buffers advance, and a run that mutates nothing composes the combined plan exactly once.

### `CrossRegionDependencyOrders`

A producer region writes `x`, and a consumer in a different region reads `x` and writes `y`. Both
touch `x`, so the composer must wire an edge across the regions for the consumer to see the
producer's writes: `y[i] == 2*(i+1)`. Ordering across regions follows the ascending region key, so
the producer has key 10 and the consumer key 11, as the documented contract requires.

### `DropRegionStopsItsWork`

Dropping a region removes its work at the next step boundary. After the drop the surviving region
keeps advancing, and the buffer of the dropped region freezes.

### `AddRegionBetweenSteps`

Adding a region between steps recomposes exactly once, and the work of the new region takes effect
on the next run.

### `SingleRecomposePerMutation`

Mutating one region out of many recomposes once, and the unchanged regions are not rebuilt. The
proof is indirect: the results stay correct, and `compile_count` advances by exactly one per
mutation, not per region.

### `FindRegionReadsWithoutRecomposing`

Reading a region through `find_region()` is not a mutation. The plan must not recompose on the next
run, and an absent key returns `nullptr` where `region()` would create the region.

### `ThreeRegionChainThreadsValue`

The chain has three hops across three buffers: region 0 writes `x`, region 1 reads `x` and writes
`y`, and region 2 reads `y` and writes `z`. Each hop is a separate boundary. The composer must order
the regions 0, 1, 2, so that `z` carries the value threaded through both hops: `z[i] == 2*(i+1) +
1`.

### `ReadReadSharingStaysConsistent`

One producer writes `base`, and two consumer regions both read `base` and write their own outputs.
The two readers share no hazard, so the composer must not order them against each other; both see
the producer's writes. The observable is correctness: the output of each reader reflects `base`.

### `StreamInConsumerRewiresProducer`

Streaming a consumer in beside an existing producer must re-stitch the producer's boundary. Region
0, which writes `x`, runs alone first. Region 1, which reads `x`, then streams in and observes `x`
on the next run. The test exercises the path that re-wires a neighbour, where the exit pin of an
unchanged region gains a successor in another region.

### `DropProducerLeavesConsumerRunnable`

Dropping the producer must cleanly un-wire the boundary and leave the consumer runnable, now without
an upstream edge. Region 1 keeps running after region 0 is dropped, and the run neither deadlocks
nor faults.

`y` accumulates `px` across two steps. The test does not depend on the exact value; it checks that
the run completed and the consumer kept executing.

### `DisjointRangeRegionsAreNotOrdered`

Two regions that write disjoint element ranges of one buffer must not be ordered against each other.
No public interface exposes the boundary edges, so the test proves the property by behaviour. Both
region tasks rendezvous on a shared counter with a bounded deadline, and each records whether it saw
the other arrive. If the tasks run concurrently, both see it. If they are serialized, the first one
times out and only the second counts, which is what a boundary at allocation granularity produces.

The deadline means a failure reports and does not hang. The test is skipped on a machine that cannot
run two tasks at once, where the property cannot be observed.

### `OverlappingRangeRegionsStillSerialize`

This test is the counterpart of `DisjointRangeRegionsAreNotOrdered`: two regions that write the same
range of one buffer must still be ordered. When the tasks are serialized the earlier one cannot see
the later one arrive, so exactly one of the two records the rendezvous. The arithmetic is sensitive
to order and comes out to the ordered answer.

### `WholeBufferReaderOrdersAfterBothHalfWriters`

Boundary ordering on sub-ranges must still produce the right values. Two regions write disjoint
halves and a third reads the whole buffer, so the reader overlaps both writers and has to run after
each of them.

## test_dynamic_graph_node_events.cpp

The file covers the per-graph profiling switch of `DynamicGraph` and its registration index, which
is dense and ordered by ascending region key and then by `add()` order at every recompose. The
design is sections D, E (item 8) and F.6 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`, built as task 6 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-plan.md`.

### `DynamicGraphNodeEventsTestAccess`

`DynamicGraphNodeEventsTestAccess` is a friend used only by tests to reach `DynamicGraph::plan_`,
which has no public accessor. It mirrors `NodeEventRecordsTestAccess` on `Graph`.

### `registration_index_of`

`registration_index_of` returns the registration index of the one compiled node called `name`.
`plan.all_nodes` is populated only while the graph requests profiling (`commit()` in
`dynamic_graph.hpp`), and every test in the file relies on that.

### `NotRequestedRecordsNothing`

A graph that never requested profiling records nothing, whatever the default of the runtime is.

### `RequestedGraphProducesRecordsMatchingRenumberedIndex`

A `DynamicGraph` that requested profiling must produce records; a correctly stamped flag is not
enough. It needs its own call to `CompiledGraph::capture_profiling_state()`, because it has no
`capture_base_state()` of its own that could make the call for it.

### `IndexDenseAndOrderedByRegionThenAddOrder`

With two regions that each make more than one `add()` call, `node_index` is dense and ordered by
ascending region key first and by `add()` order within a region second.

Region 5 is added first but has a higher key than region 2, so its two calls must land after the one
call of region 2 once the order follows the key.

### `IndexStaysDenseAfterDropAndReAdd`

Dropping a region and adding it again must not leave a gap in the index space. The next recompose
renumbers every live region densely from zero.

## test_error_isolation.cpp

A task exception must abort only its own run and must not kill the worker pool. The run that threw
surfaces the error to the caller, and a fresh graph on the same `Runtime` must still run and produce
correct results afterwards. The defect this file guards against latched `shutdown_requested` on a
single throw and left the pool dead for good.

### `ThrowingKernelLeavesRuntimeReusable`

The command-group function of the first graph throws, so `execute()` must report the error to the
caller and not hang.

## test_fluent_api.cpp

The file covers the parts of the fluent API that `test_fluent_lifecycle.cpp` does not exercise:

- `run_for(duration)`
- the success path of `try_run()` and the fields of `RunReport`
- the `add(fn)` overload that takes a raw `sycl::handler`
- the `add(n, fn)` range kernel overload
- the pointer-tracked `add(reads, writes, n, fn)` overload
- `Reads` and `Writes` over `Buffer::region()`, which track a sub-region
- a standalone `Buffer<T>` allocation used in a graph
- `Graph::size()`
- the `Runtime::advanced()` and `unsafe_context()` accessors
- `run(0)`

### `TryRunSuccessPath`

`try_run()` on a non-empty graph must return a successful result with `total_tasks_executed` filled
in. The test proves that the non-throwing path returns a valid `RunReport` and not a
default-constructed one.

### `RunReportFieldsAfterMultiStep`

After a multi-step run, `total_tasks_executed` must equal the tasks per step times the steps, and
`total_duration_ms` must not be negative.

### `RunForRunsAtLeastOneStep`

`run_for(duration)` must run at least one step and then stop. The budget is very short so that the
test does not wait; the test validates the one-step guarantee.

### `RunForRunsMultipleStepsWithinBudget`

With a real budget, `run_for` should complete several steps and the report must reflect them. The
test measures steady-state throughput, not a cold start.

The kernel is warmed before the timing starts. The first dispatch pays for JIT compilation and for
queue and device setup once, and that alone can exceed the 50 ms budget. `run_for` fixes the
deadline before its first step, so an unwarmed kernel overshoots and stops after a single step. In
steady state this trivial kernel costs less than a millisecond, so a warmed run fits many steps.

### `RawHandlerKernelAdd`

`add_untracked(fn)` takes a function that receives `sycl::handler&` directly, the raw kernel
overload. The kernel must run and produce the expected values.

### `RangeKernelAdd`

`add_untracked(n, fn)` takes a function that receives `(begin, end, sycl::handler&)`, the range
kernel overload. The function must see `begin = 0` and `end = N` and launch its own work.

### `PointerTrackedAddOrdering`

`add(reads, writes, n, fn)` is the overload with explicit dependency vectors. Two tasks share one
buffer: a writer, then a reader that squares the values. The dependency tracker must order them
correctly without a `State`.

### `TrackedRangeKernelOrdering`

The tracked range-kernel overload runs the writer and squarer pair of `PointerTrackedAddOrdering`,
but the second task drives the handler itself. The declared `Reads` and `Writes` must still order it
after the writer. An untracked overload would let the square race the write.

### `TrackedRawKernelOrdering`

With the tracked raw-kernel overload the reader owns the whole command group, and its declared
regions still order it after the writer.

### `TrackedNdRangeOrdering`

With the tracked `NdRange` overload a tiled work-group kernel keeps dependency tracking and still
gets an `nd_item`. The kernel is ordered after the writer, and the `nd_item` indexing must cover the
whole global range.

### `SubRegionDisjointWritesThenWholeRead`

Two writers touch disjoint halves of one buffer, so the tracker leaves them unordered. A
whole-buffer reader then overlaps both and must run after each. The test proves sub-region identity
end to end through the fluent layer: disjoint slices are independent, and an overlapping access
still orders. An identity based on the base pointer would either serialize the halves or let the
reader observe an unwritten half.

### `BufferAllocateUseAndReadBack`

A standalone `Buffer<T>` is allocated, seeded on the host, used in a graph task through
`dependency_key()` and read back. The test verifies the lifecycle of owning USM.

### `GraphSizeTracksAdds`

`Graph::size()` tracks the number of `add()` calls made so far, also after a mutation that triggers
a recompilation.

### `ContextAccessorIsConsistent`

`Runtime::unsafe_context()` must return a reference to the `RuntimeContext` the fluent `Runtime`
wraps, and `advanced()` must read through to it. The test verifies both by comparing device counts.

### `RunZeroStepsIsNoOp`

`run(0)` is documented as zero steps. It must return immediately, and the `RunReport` should show no
work done.

## test_fluent_hardening.cpp

The file covers the hardened fluent surface:

- `add_untracked(Extent<2>, fn)`, a multidimensional per-element map
- `add_untracked(NdRange<2>, fn)`, a tiled work-group kernel
- `add(State, Extent<2>, fn)`, a multidimensional stepped stencil
- `add(out, in0, in1, fn)`, a variadic coupled update that reads three fields
- `add(Reads, Writes, n, fn)`, the handle-based dependency wrappers
- `add(Extent, In/Out, fn)`, buffer arguments captured automatically
- `add(State, Extent with offset, fn)`, an interior-only (halo) update

### `Extent2DPerElementMap`

With `add_untracked(Extent<2>, fn)` the kernel sees a `sycl::id<2>` that covers the whole box and
writes a row-major linear index, which proves 2-D iteration and indexing.

### `NdRange2DTiledKernel`

`add_untracked(NdRange<2>, fn)` runs a tiled kernel in the work-group model. Each item writes its
global linear index, and a correct result proves that `nd_item` indexing works.

### `State2DSteppedUpdate`

`add(State, Extent<2>, fn)` is a multidimensional stepped update. Each step adds the row index to
every cell, so after S steps a cell in row y holds `S*y`.

### `VariadicCoupledThreeInputs`

`add(out, in0, in1, fn)` is a coupled update that reads three fields, `out` and two others: `next =
cur + a + b`. All reads see data of the previous step (Jacobi).

### `ReadsWritesWrappersOrdering`

With `add(Reads, Writes, n, fn)` the dependency wrappers must order a writer before a dependent
reader exactly as the raw `void*` vectors do.

### `InOutCapturedArguments`

With `add(Extent, In/Out, fn)` the captured buffer arguments give the kernel raw pointers and
register the dependency in one step. The kernel computes `dst = src * 2`.

### `InteriorOnlyHaloUpdate`

With `add(Reads, Writes, Extent-with-offset, fn)` an interior-only update must leave the one-cell
border, the halo, untouched, which proves sub-range iteration. The test uses an in-place `Buffer` so
that the border keeps its initial value deterministically.

## test_graph_profiling_switch.cpp

The file covers the per-graph profiling switch: section B of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`, built as task 4 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-plan.md`. It checks that
`set_profiling_requested()` is independent across graphs, that the plan recompiles once, and that
every device-dispatch branch the scheduler can route to a profiling queue does so.

### `NumaFallbackBranchUsesProfilingQueue`

A node with no declared `Reads` or `Writes` leaves the scheduler's anchor unset. That forces the
NUMA-local fallback branch and not the cached-index branch that the other two tests of the file
exercise. Nothing public exposes which branch a node took, because `Node::cached_queue_index_` is
private to the scheduler, so the test asserts the observable effect of the fallback: a profiling
queue is built and timing is collected. If the fallback still landed on the plain queue, the
poller's timestamp read on that event would throw, because the plain queue is not built with
`enable_profiling`. A regression therefore fails loudly and does not merely lose timing.

One branch is left: `get_queue_for_ptr` for an anchor the USM registry does not recognise. A test
cannot force it safely. Reaching it needs a device kernel that dereferences a pointer the runtime
never allocated, which is an illegal access on a real GPU backend.

## test_heterogeneous_pipeline.cpp

The file combines the work-stealing scheduler, mixed host and device tasks, and concurrent device
execution through the full path from `TaskGraph` through `ExecutionEngine` to `TaskScheduler`. Many
independent chains of a host task, a device kernel and a host task run at once.

Shared USM is bound to a context: a buffer allocated on the queue of one device is not valid on the
queue of another. Each chain therefore pins its allocation and its kernel to the same explicit
device queue, and a chain is either all device or all host.

### `ManyChainsHostDeviceHost`

The test uses one explicit queue per device. It iterates the deduplicated queue set of `ctx` and not
`sycl::device::get_devices()`. The SYCL list can hold more devices than `ctx` exposes, for example
the duplicate OpenMP host device of AdaptiveCpp, so indexing `devices().queue()` by the raw SYCL
list overruns it and fails with "Queue index out of bounds".

## test_late_bindings.cpp

The file covers the persistent-graph, late-binding path, work package WP-1 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md`. A graph whose live size or per-step participation
changes between steps must be compiled exactly once and replayed, never recompiled. Each test
asserts the numerical result and `Graph::compile_count() == 1`.

### `DynamicSizePerElement`

`sized()` shrinks the iteration range to a live prefix on each step. Driving the live count from 1
to `CAP` over `CAP` successive `run(1)` calls increments `buf[i]` exactly on the steps where `i` is
inside the live range, so `buf[i] == CAP - i`. The plan must compile once and follow the changing
size on replay.

### `EnableTogglePerElement`

`when()` gates whether the node runs at all. Toggling the predicate over ten steps runs the
increment only on the enabled steps, so each element ends at the count of enabled steps, with a
single compile.

### `DisabledNodeStillReleasesSuccessors`

A disabled step must release its successors so that the rest of the graph still runs. A downstream
task that always runs should advance on every step whatever the enable state of the upstream node,
which proves that an instantly completed node drains the DAG.

Upstream writes the gate buffer. Downstream reads the gate buffer and writes the sink, so the
tracker orders downstream after upstream on the gate buffer.

### `DynamicSizeState`

`sized()` works on a stepped `State`: the live prefix grows on each step while the graph is compiled
once. Cells that are active on every step accumulate one increment per step, which is what a
single-threaded reference loop would produce for that prefix.

### `ComposedWhenAndSized`

`when()` and `sized()` compose. The node runs only while enabled, and when it runs it iterates the
live count. Over the run an element is incremented once per step that is both enabled and in range,
still with a single compile.

### `BaseOffsetIteratesASlice`

`based_at()` moves where a late-bound node starts, without a recompile. Graph colouring has this
shape: each colour is a contiguous slice of one constraint buffer, and every slice shifts as
constraints are added or removed. The callable still receives absolute indices, so a kernel written
without knowledge of bases stays correct.

### `BaseWithoutASizeRunsToTheEnd`

A base with no size runs from the base to the end of the capacity, not over the whole buffer. The
sentinel means "the rest", and with an offset the rest starts at the offset.

### `BaseAndSizeBothFromTheDevice`

`based_at_device()` takes the offset from a slot a predecessor wrote, so a colouring pass that
computes the start and the length of a slice on the device can hand over both in the same way. The
slot is registered as a read, so the producer is ordered first without the caller naming it.

### `WindowPastCapacityFailsTheRun`

A window that runs off the end of the capacity fails the run and names what it asked for. Clamping
would silently drop the tail of a colour, the kind of failure `sized()` and `based_at()` exist to
rule out.

### `BasedAtDeviceRejectsAnUnreadableSlot`

`based_at_device` rejects a slot it could not read at dispatch, and it does so at the call site, not
in the middle of a run.

### `RawSharedSlotIsRejectedAtAdd`

The raw-pointer setter takes no buffer, so `add()` checks the pointer: a slot inside `Shared` memory
is rejected before the graph records the task.

## test_late_count_ordering.cpp

The file covers the ordering claim behind the device slot of `LateCount`, which came out of the
second fix round of `docs/archive/superpowers/specs/2026-09-16-scan-and-counting-sort-plan.md`. A
count sourced from the device must be resolved when the consuming node is dispatched, after its
producer. It must not be polled at the step boundary, before the producer has run. A graph whose
resolver dropped the device slot from its `Reads` would still compile and run, but it would fold the
count of the previous step and not the one just written. The test is built to catch that regression.

### `ReduceSeesTheCountItsPredecessorJustWrote`

A producer node writes a fresh live count into a `Residency::Host` slot on every run, and a
late-bound `add_reduce` reads the same slot through `LateCount::device` in the same run. The reduce
must see the count the producer wrote in this run.

The producer reads its next count from a buffer and not from a host stack variable, because the
kernel may run on the device, which cannot reach the stack frame of the test.

## test_layer_equivalence.cpp

The file compares whole output buffers byte for byte and uses the public API only, so it holds
across a change of internals. All data is `std::uint32_t`.

Four shapes are covered: the same work as regions of a `DynamicGraph` and as one static `Graph`,
with a region dropped and recorded again; host tasks among kernels in one dependency chain;
steps through `run_async()` against `run(n)`; and a 2-D box with an origin offset through each
launch form. Where a layer choice applies, the chain layer is compared with the forced scheduler
layer, and the offset launches also with a reference computed on the host.

`NdRange` has no origin, so its kernels add the offset to the global id themselves.

`UntrackedOffsetLaunchesMatchAcrossLayers` skips on a runtime that drives more than one device;
the reason is in `docs/reference/KNOWN_ISSUES.md`.

## test_native_cpu_path.cpp

The file covers the native CPU execution path, work package WP-7 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md`. `add_host()` runs plain C++ on a worker thread
with no SYCL submission. The task is ordered into the graph by the data it touches and shares one
completion contract with kernel tasks.

### `HostTaskRunsAfterKernel`

A native host task ordered after a device kernel must see the kernel's output. The task is
dispatched only once the kernel's event has completed, so the shared USM the kernel wrote is visible
on the host. The ordering comes from the shared buffer `a`.

### `KernelRunsAfterHostTask`

A device kernel ordered after a native host task must see the host's writes. The kernel waits on the
inline completion of the host task before it is dispatched, and shared USM written on the host is
visible on the device.

### `UntrackedHostTaskRunsEveryStep`

An untracked native task runs once per step, which proves that the form without dependencies is both
a root and a leaf and replays on every step.

### `ThrowingHostTaskKeepsRuntimeUsable`

A native task that throws fails the run, which is the error isolation of work package WP-0, and the
runtime stays usable: a fresh graph runs cleanly afterwards. Native tasks therefore share the kernel
error path and do not latch the worker pool dead.

## test_node_event_records.cpp

The file covers per-dispatch node event records, the clock pairing and steady-state capture. The
design is section C and items 1, 2, 3, 4 and 6 of section E in
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`, built as task 5 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-plan.md`. The item numbers in the
subsections refer to section E of the design.

### `NodeEventRecordsTestAccess`

`NodeEventRecordsTestAccess` is a friend used only by tests to reach `Graph::plan_`. It mirrors
`RegistrationIndexTestAccess` in `test_registration_index.cpp`.

### `RequestedGraphRecordsOnePerDispatch`

Item 2: three independent kernel nodes and `run(5)` give fifteen records, invocations 0 to 4 per
node, and `submit <= start <= end` for every record.

### Steady-state capture

Item 3 is tested in `test_steady_state_allocation.cpp`, which is built into the
`sr_allocation_tests` executable and so runs in its own process. A replacement `operator new` in
`test_node_event_records.cpp` would see only the allocations of its own executable and never those
of the shared library. The section on `test_steady_state_allocation.cpp` in this document explains
why that test needs its own binary.

### `NodeIndexIsRegistrationOrder`

Item 4: `node_index` follows the registration order, not the compiled order, which is reversed. The
test cross-checks it against the plan's own `registration_index`.

A strict dependency chain, in which B reads what A wrote and C reads what B wrote, forces the
execution order A, B, C. That gives an observation of identity which does not depend on trusting the
`registration_index` read from the `Node` object. If the registration order of the plan and the
dispatch order of the run ever disagreed, sorting by earliest submit would catch it, even though
both would still read as {0, 1, 2} on their own.

### `HostNodeLandsOnEventClock`

Item 6: the converted record of a native host node lands inside the event window of the run, within
the `uncertainty_ns` of the pairing.

The host node reads what the kernel writes. That chains it strictly behind the kernel's completion;
the two are not independent roots. The window of the host record therefore sits inside the
event-clock window of the run, not merely near it.

The converted `submit_ns` and `start_ns` of a host record are identical, because the scheduler
stamps the same raw steady time into both. Those of a device record rarely are. The test tells the
two kinds apart by that difference and not by a stored flag, because `NodeEventRecord` stays field
for field identical to the engine's `NodeEvent`, as the global constraints of the design require.

The chain guarantees that the host node is dispatched no earlier than the kernel's completion. The
tolerance covers only the measurement uncertainty of the pairing, not scheduling jitter between
unrelated roots.

## test_observability.cpp

The file covers `RunReport` observability, section 5.5 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md`. Profiling is chosen at construction. With
profiling off, the report carries no profiling breakdown. With profiling on, it carries timing per
node and per worker. Explicit transfers between host and device are accounted in both cases.

### `ProfilingOnCapturesPerNodeTiming`

Both kernels appear as distinct plan nodes, each dispatched once per step. The magnitudes of the
device times depend on the backend and on noise, so the stable claim is structural: the per-node
breakdown is present and correctly counted.

### `NamingTheLastNodeReachesNodeTiming`

Any node can be given a diagnostic name that reaches `RunReport::NodeTiming`. This is requirement R8
of `docs/design/PHYSICS_SUBSTRATE_REQUIREMENTS.md`. Without a name every node reports as
"unnamed_task" whichever `add()` produced it, and a profile of a real graph is a list of identical
labels.

The name is set with `name_last()` and not with a trailing argument. Two of the `add()` shapes take
a trailing parameter pack whose last element is the callable, so a defaulted trailing name cannot be
expressed for them. One spelling that works for every shape is better than a trailing argument on
most shapes and an exception for the rest.

### `EveryAddShapeCanBeNamed`

The test names a node of every launch shape, including the two variadic ones that cannot take a
trailing argument. A first version of R8 reached only the `Dynamic` overloads, and this test would
have caught it.

### `UnnamedNodeStillReportsTheDefaultLabel`

A node added without a name keeps the documented default label, so a call site without a label is
still visible in the report.

### `TransferBytesAttributedToTheRunWindow`

A host node pushes data during the run. The bytes must land in the window of that run, not before
it.

## test_perf_controls.cpp

The file covers the rebalancer control: the rebalancer is off unless requested, execution is correct
in both states, and the engine reports the flag faithfully.

The default is the assertion that matters. The background thermal rebalancer migrates tasks between
workers on a 5 ms heartbeat. That changes which worker folds which tile and puts a periodic wakeup
under every frame, a throughput trade that gains a simulation frame nothing. When the rebalancer was
on by default, every consumer paid for it, and the primary consumer turned it off at every
construction.

## test_prefix_scan.cpp

The file covers `Graph::add_prefix_scan()`. Its design is sections A1 and B of
`docs/archive/superpowers/specs/2026-09-16-scan-and-counting-sort-design.md`, and it was built as
task 3 of `docs/archive/superpowers/specs/2026-09-16-scan-and-counting-sort-plan.md`. The tests
check exact agreement with a host reference at the tile edges, the exclusive contract (`offsets[n]
== total`), late-bound replay with one compile, and that every internal pass of one call shares one
`node_index` across steps.

### `host_exclusive_scan`

`host_exclusive_scan` is an exclusive prefix sum over `uint32`, modulo 2^32, which is the contract
of the scan: `offsets[i]` is the sum of `counts[0..i)`, and both `offsets[n]` and the return value
carry the grand sum.

### `CANARY`

`CANARY` is a sentinel that a real scan would never produce. A slot that keeps it was not written,
and a slot that loses it was written when it must not have been.

### `ExactAgreementWithHostReferenceAtTileEdges`

The tile edges are where an off-by-one in the level plan or in the base of the down-sweep would
show: 0, 1, one below, at and above a tile, one below, at and above the second level, and 10^6.
`offsets` and `total` are filled with the canary before the run, and `offsets` carries one extra
slot past `capacity + 1` that must survive untouched. Neither a stale zero nor a write past the
declared window can then pass by accident.

### `OffsetsAtLiveCountEqualsTotal`

The second half of the exclusive contract: `offsets[n]` is exactly the sum that `add_prefix_scan()`
also publishes through `total`, not an approximation of it.

### `LateBoundChangingCountKeepsOneCompile`

A live count that changes between runs must not force a recompile. The topology (levels and scratch)
is fixed at the capacity; only the live count and the counts it scans vary per run.

### `EachRunScansItsOwnCounts`

When the counts change between calls, and not only the live count, a run must scan what is in the
buffer at the time it runs, not a stale copy.

### `SharesOneNodeIndexAcrossStepsAndPasses`

Every internal pass that one `add_prefix_scan()` call emits (the late-count resolver, the up-sweep
levels, the down-sweep levels and the closing write) shares one `node_index`, and `run(steps)`
records one invocation per step for each of them. The node event records are specified in
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`.

`plan_tiled_levels(600)` gives two levels: level 0 folds 600 into 3 and level 1 folds 3 into 1. The
call therefore emits 1 node for the resolver, 1 for the up-sweep (every level but the top), 2 for
the down-sweep (every level) and 1 for the closing write: 5 nodes.

### `InternalNodeWidthsMatchTheDispatchTable`

The `launched_width` of each internal pass must be its own row of the width table; a test that only
checked that some width was stamped would prove less. Every pass reads a region the pass before it
wrote: `state_region`, then `up_scratch`, then `down_scratch`, then the whole-buffer
write-after-write on `offsets`. `report.node_events` therefore lands in that dependency order
whichever execution path ran the passes.

The shape is that of `SharesOneNodeIndexAcrossStepsAndPasses`: `plan_tiled_levels(600)` gives level
0 from 600 to 3 and level 1 from 3 to 1. The node order is resolver, up0, down-top, down0, closing.

### `ZeroCapacityWritesZeroTotal`

With capacity zero the live count is always zero, so `total` and `offsets[0]` are always zero. The
buffers start from the canary, so a zero that was already there cannot pass for a written one.

### `LateBoundAboveCapacityFailsTheRun`

A live count above its capacity fails the run. The alternative is a read or a write past the buffers
the scan was planned for.

## test_profiling_queue.cpp

The file covers the profiling queue of `RuntimeContext::devices()`, sections A and F2 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`, built as task 3 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-plan.md`. A profiled node needs the
SYCL `command_submit`, `command_start` and `command_end` times, which only a queue built with
`enable_profiling` exposes. The plain queue that `devices().queue(index)` returns must stay
unchanged for every existing consumer.

### `ProfilingQueueAddsThePropertyAndSharesTheContext`

With `RuntimeSettings::profiling` off, which is the default, the plain queue carries no
`enable_profiling` property and the profiling queue carries it once built. The two share one
`sycl::context`, so USM allocated through the plain queue stays legally accessible from the
profiling queue.

### `NoProfilingQueueExistsBeforeTheFirstRequest`

Nothing is built before the first request. `RuntimeSettings::profiling` is off, so
the `RuntimeContext` constructor must not have built the profiling queue of any device.

### `SettingsProfilingBuildsEveryDeviceEagerly`

`RuntimeSettings::profiling` asks the `RuntimeContext` constructor to build the profiling queue of
every device eagerly, before any `run()` could have requested one.

### `SettingsProfilingReturnsThePlainQueueItself`

With `RuntimeSettings::profiling` on, the plain queue already carries `enable_profiling`, so
a request for the profiling flavour must return that same queue handle and not a second, duplicate
one.

### `UsmFromThePlainQueueIsUsableOnTheProfilingQueue`

A kernel dispatched on the profiling queue correctly dereferences a USM allocation made through the
context of the plain queue. The shared context is usable, not only reported.

## test_reduce_dispatch_width.cpp

The file covers the dispatch widths of `Graph::add_reduce()` and `Graph::add_segmented_reduce()`, in
the style of `InternalNodeWidthsMatchTheDispatchTable` in `test_prefix_scan.cpp`.

### `EagerReduceLevelWidthsMatchTheDispatchTable`

`plan_reduction(600)` at `REDUCE_TILE = 256` has two levels: level 0 folds 600 values into ceil(600
/ 256) = 3 partials, and level 1 folds those 3 into ceil(3 / 256) = 1. Each level is its own node,
so the launch width per node is the `out_count` of that level, in level order.

### `EagerReduceZeroCountStampsWidthOne`

`n == 0` skips every level and emits one node that writes the identity. That node still has a real
launch width of one, not `WIDTH_UNKNOWN`.

### `LateBoundSegmentedReduceWidthsMatchResolverAndCapacity`

A `LateCount` left at its default, with no host provider and no device slot, behaves like a fixed
count equal to its capacity, which is the documented fallback of `detail::read_live_count()`. The
late-bound overload of `add_segmented_reduce()` therefore emits its resolver, a `single_task` of
width one, followed by its per-segment node, whose width is the capacity.

`segment_offsets` and `output` are each oversized past `segment_count` by a different amount. A
width stamp that read the size of either buffer, and not the launch range, would then fail and could
not pass by accident.

### `EagerSegmentedReduceWidthMatchesSegmentCount`

The fixed-count overload emits one node over exactly `segment_count` elements, so its launch width
is `segment_count`.

The buffers are oversized as in `LateBoundSegmentedReduceWidthsMatchResolverAndCapacity`:
`segment_offsets` and `output` each carry a different amount of padding past `segment_count`, which
pins the stamp to the launch range and not to either size.

## test_region_emitter.cpp

The file covers `add_emitter()`, the recording form that hands the caller the backing TaskGraph.
The emitters record device kernels, the way a library above the runtime does, and each kernel waits
on the dependency events it is handed.

### `record_device_task`

`record_device_task` records one `add_task` node whose work is a `parallel_for` on the queue it is
given. The tasks are device kernels, not host tasks, because a host access to a
`Residency::Shared` buffer while a kernel runs on the same GPU faults on Windows with CUDA; see
`docs/reference/KNOWN_ISSUES.md`.

### `EmittedNodesOrderAgainstANeighbourRegion`

One emitter records two kernels on a buffer, a fill and a tripling, and a higher-keyed region reads
that buffer through an ordinary `add()`. The consumer sees the tripled values, so the two emitted
nodes ran in their recorded order and the boundary edge ordered the consumer behind them.

### `AnEmitterIsOneRule`

An emitter that records two nodes reports a region size of one and a graph size of one. `size()`
counts rules, as it does for a reduction that lowers to several nodes.

### `AnEmitterReplaysOnlyWhenItsRegionIsRebuilt`

The emitter counts its own calls. Three replayed steps call it once, rebuilding the neighbouring
region leaves the count at one, and dropping and recording its own region again raises it to two.

### `AStaticGraphTakesAnEmitter`

A static `Graph` records an emitter beside an ordinary `add()` that reads the emitter's buffer.
Four steps advance both, so the form is not specific to regions and survives a chained replay.

## test_rule_registration.cpp

`Graph::rule_registration` is the only way a caller can read the node events of a `RunReport`. The
events number nodes by registration index, and the `add()` order a caller thinks in is a different
sequence. These cases check the mapping between the two.

## test_run_report_reuse.cpp

The file covers the overloads that take a caller-owned `RunReport`: `run(RunReport&)`,
`try_run(RunReport&)` and `RunHandle::wait(RunReport&)`.

The tests pin down two properties that pull against each other. The overloads exist so that a frame
loop keeps one report alive and the per-run vectors stop being reallocated, so the report must not
be rebuilt from scratch. A report carried across ticks must also never let the numbers of the
previous run survive into the current one, and not rebuilding invites exactly that. The resolution
is to reset and then refill: the capacity stays and the values do not. Both halves are asserted,
also on the failure paths, where stale numbers would mislead most: a caller that ignored the
returned error would otherwise read a report that looks successful.

### `record_write`

`record_write` zeroes `buf` and records the one task every test in the file runs: element i gets
i+1. It takes both handles by reference and does not return them, because a `Buffer` must not be
moved once a graph has recorded it; the graph would keep pointing at the old address.

### `ReuseReportsWhatTheValueFormReports`

The reuse overload must report what the value-returning one reports. If the two drivers ever
diverged, every number a consumer reads from the cheap path would silently become a different
measurement from the documented one.

### `SteadyStateRunsDoNotReallocateTheReport`

The overload exists so that the per-run vectors are refilled in place. Comparing the address of the
row before and after proves that no reallocation happened. A capacity check alone would pass even if
the vector were rebuilt into a new block of the same size.

### `EachRunOverwritesTheLastOne`

Reuse must not accumulate. A short run that follows a long one has to read as the short run, because
the count belongs to the run, not to the report.

### `ARejectedRunLeavesNoStaleNumbers`

A rejected run must clear the report it was handed. The dangerous case is a caller that checks the
returned error only sometimes. Reading a stale `is_successful` of true from a run that never
happened is worse than reading zeroes, so the failure path resets the report and marks it
unsuccessful.

### `TryRunReturnsTheCallersReport`

The non-throwing form returns the report it was given, so a caller can keep using the result of the
call without holding both.

### `AsyncWaitFillsTheCallersReport`

The asynchronous path settles into a caller-owned report too. The overlapped frame loop is the one
that most wants the overload.

## test_scheduler_integration.cpp

The scheduler checks in this file are fast and deterministic; heavy load is tested under
`tests/benchmark/stress`. The scheduler must run exactly the tasks submitted, honor priority bypass,
and survive a clean round of arm, submit and stop.

## test_steady_state_allocation.cpp

The file makes the steady-state allocation cost of a tick measurable and then holds it to a shape,
not to a number.

A frame loop runs the same compiled plan thousands of times. Anything the plan allocates per tick is
a `malloc` on the critical path of the simulation thread and a slow drift toward heap fragmentation,
and neither shows in a throughput benchmark that runs the graph once over a large input. The counter
is a replacement global `operator new`, gated by a thread-local flag so that it observes only the
thread that drives the run. Worker threads allocate for reasons of their own (event lists, SYCL
submissions), and their noise would drown the signal.

The assertion is deliberately not "zero", and on Windows it cannot be. SushiRuntime is a shared
library, so the replacement `operator new` of this file sees only allocations made by code compiled
into the test executable, never by code compiled into the DLL. The command-group functions handed to
`sycl::handler::parallel_for` are templates, instantiated where they are written. They are inline in
`SushiRuntime/api/graph/graph.hpp`, so their object code lands in whichever translation unit
includes that header. SYCL's own `StoreLambda()` allocates the stored kernel object on the heap on
every submission (`llvm-sycl/include/sycl/handler.hpp`, `MHostKernel.reset(new
detail::HostKernel<...>(...))`). The scheduler runs that submission from a worker thread, so the
thread-local flag of the counter never sees it. The chain runs it synchronously from the calling
thread (`modules/engine/execution/source/chain_run.cpp`, `ChainRun::submit_node()`), so the counter does see it. The
chain could never allocate nothing per tick once SYCL's kernel storage is inline. The rule the test
holds is that the chain adds no more allocation than SYCL's own submission of the same kernel
already costs on the same thread, and the baseline measures that cost directly.

The file asserts three relative claims, the last of them twice:

1. The value-returning `run()` costs at least one more allocation per tick than the reuse form. The
   extra allocation is the `RunReport`, and it shows that the counter can see the path it measures.
2. The per-tick growth of the reuse form, measured as the count for four times the ticks minus the
   count for one times the ticks, is no more than the per-tick growth of a plain `sycl::queue` that
   submits the identical kernel, in order, on the same device and context from the same thread. What
   the chain adds beyond bare SYCL is the runtime's own cost, and that cost must not scale.
3. On a run forced onto the scheduler (`RuntimeSettings::force_scheduler`), the count does not scale
   with the ticks, for the plain run and for the profiled one. The submission runs on a worker
   thread, so the counter cannot see it, and the count stays flat because it stays zero.

The raw per-tick figures for the chain, the baseline and the forced scheduler are recorded as test
properties, so every number is visible even where it cannot be asserted.

### Sanitizer detection

The preprocessor block detects a build whose sanitizer runtime owns the allocator. It checks both
spellings for each sanitizer, because Clang and IntelLLVM do not agree on which they define: the
`__has_feature` form is native to Clang, the `__SANITIZE_*` macros are the predefined GCC-style
ones, and a toolchain may offer only one of them. A guard that tested a single spelling, and only
for AddressSanitizer, let the ThreadSanitizer lane through. `tests/common/test_main.cpp` uses the
same idiom for `SR_TSAN_BUILD`.

### `SkippedUnderSanitizer`

AddressSanitizer and ThreadSanitizer each replace `operator new` and `operator delete` themselves:
ASan to place its redzones and track ownership, TSan to attach its own bookkeeping. A second
replacement in this translation unit measures nothing. It is a duplicate definition of the same
symbols and fails at link, on Windows against ASan and on Linux against `libclang_rt.tsan_cxx.a`.

The suite is skipped under a sanitizer, not adapted. Counting could be kept alive by installing
allocator hooks in place of the replaced `operator new`, but the numbers would then include the
sanitizer's own allocations, and the suite asserts on exact steady-state counts. A test that runs
while it measures something other than what it claims is worse than a test that reports it was
skipped. The target stays in the sanitizer lanes, where it still compiles, links and runs, so the
cost of the guard stays visible.

### Counter state

The counting flag and the counter are constant-initialized, so both are live before any dynamic
initialization that might allocate. The flag is per thread. The counter is shared only because the
counting thread is the only one that ever touches it.

### Replaced operator new

Every replaced form is paired with its matching deallocation form, so nothing allocated through a
replaced form can be freed by a different allocator. The over-aligned forms, which take
`std::align_val_t`, are deliberately not replaced. Leaving the whole aligned family at its default
keeps that pairing intact as well. The cost is that aligned allocations are not counted, and the run
path makes none.

### `allocations_for_reuse_ticks`

`allocations_for_reuse_ticks` runs `ticks` steps and returns how many allocations the calling thread
made. The graph is warmed first, so that compilation and the capacity growth of the first run fall
outside the measured window. The question is what a tick costs in steady state, not what the first
one costs.

### `allocations_for_baseline_ticks`

`allocations_for_baseline_ticks` returns what bare SYCL costs, on the calling thread, to submit the
same per-element kernel `ticks` times, in order, on `queue`. The cost of the chain is judged against
this figure. `queue` must be an in-order queue on the device of the plan, so the function measures
the submission path that `parallel_for` in `graph.hpp` takes, without the chain's dependency waits
and event ledger around it. It is warmed as `allocations_for_reuse_ticks` is, for the same reason:
the one-time cost of the first submission is not the steady state.

### `TheCounterObservesTheCallingThread`

The counter must be able to see what it measures before a claim made with it means anything. A
single vector allocation cannot be missed.

### `ReuseTicksDoNotAllocatePerTick`

The baseline uses the device, the context and the in-order queue that the chain submits to
(the in-order flavour of `Execution::RuntimeContext::devices()`). It therefore measures what SYCL's
own submission of the identical kernel costs on the calling thread, with none of the chain's
bookkeeping around it.

The test makes the three claims in order:

1. The value form builds a fresh report on every tick, and the reuse form refills one the caller
   owns. The difference is at least one allocation per tick, and observing it proves that the
   counter reaches the run path and not only the test's own code.
2. The per-tick growth of the chain must not exceed the per-tick growth of bare SYCL submitting the
   same kernel on the same queue. Growth beyond that is the runtime's allocation, not SYCL's, and it
   is the only part the chain controls.
3. The scheduler runs the same kernel from a worker thread, so the counter cannot see any of it. The
   count does not scale because it stays zero.

### `AsyncWaitIntoAReusedReportIsAmortised`

The test makes the same claim on the asynchronous path, which is the one an overlapped frame loop
uses: submit, do unrelated work, settle into a report the loop owns.

### `ProfiledReuseTicksDoNotAllocatePerTick`

The event ledger of a profiled run allocates only for a new shape, never on a later run that replays
the same shape. This is item 3 of section E in
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`.

The baseline uses the profiling flavour of the in-order queue the chain submits to, so it carries
the cost of profiling as well.

## test_sycl_execution.cpp

### `MultiStageChain`

Both buffers are pinned to one device so that they share a context. The `Reduce` task touches `data`
and `sum` together, and the co-location guard requires the buffers of a task to live on one device.
`devices().device_for_numa(numa)` round-robins across the devices of a NUMA node, which would
scatter the two buffers across contexts on a host with several devices. A `UsmRequest` with device
index 0 fixes the device.

## test_workload_coordinator.cpp

The file covers the heartbeat loop of `WorkloadCoordinator`. Fakes for all three collaborators
(telemetry, migrator and strategy) keep the test deterministic and on the host. The loop must drive
the strategy, start and stop cleanly, and swap strategies at runtime.

### `wait_until_at_least`

`wait_until_at_least` spins until the value reaches the target or the budget elapses. The tests use
it to synchronize their assertions with the background threads of the coordinator.
