# Integration tests

The tests in `tests/integration` run a graph through a `Runtime` or the execution engine and name
headers of more than one module by path, which is why they are here and not in a module. A module's
own tests are documented in `modules/<tier>/<module>/tests/README.md`. This document records what
each test proves and why it is built the way it is. A section is named after a test file, and a
subsection after the test, helper or topic it describes. The source comments state one invariant per
line and cite this file.

Three design documents are cited by path throughout:

- `docs/design/GRAPH_REPLAY.md`, the replay layers and their phases.
- `docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md`, the work packages WP-0 to WP-7 and their
  numbered sections.
- `docs/design/ENGINE_BACKBONE_REFACTOR.md`, the backbone items BB-0 to BB-8.

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
it. The scheduler can: if a node could start before a node it reads from had finished, or a
work-item could see a neighbour's partial result, a two-worker run and a sixteen-worker run would
disagree in the low bits and nothing would report an error. The compiler can: fast-math licenses reassociation, so the same source may be
folded in a different order in two builds, and FMA contraction rounds once where the source says
twice. This suite varies the pool size, which the `worker_count` setting from item BB-2 of
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
to cancel in the first total still shows in the output.

### `record_tiled_total`

`record_tiled_total` is the rule under test, written in the test over `Recorder`. One work-item
per tile of 256 values adds its tile left to right into recorder scratch, and a second node adds
the 157 partials left to right into the total. Which values meet which is fixed by the tile size,
so the total has one correct bit pattern, and that pattern differs from the plain left-to-right
sum of the same input.

### `run_workload`

`run_workload` runs one fixed workload on a runtime built for `worker_count` workers, where 0 means
the whole machine, and copies the results out. Two independent rules compete for the pool, so the
worker that adds a given tile varies with the pool size. A map then mixes both totals into every
output element, so a divergence in either rule reaches the bytes being compared.

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

### `TheRebalancerDoesNotChangeTheBytes`

The whole machine runs the workload with the background rebalancer off and then on. Task migration
reassigns work-items to workers in the middle of a run, and the output must not move by a byte.

### `ReplayDoesNotDriftAcrossSteps`

A replayed graph must not drift. Every step of a multi-step run recomputes the same total from
the same input, so step 1 and step `STEPS` have to produce the same bytes. A drift would mean that
per-step state of the plan had leaked into the arithmetic.

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

## test_error_types.cpp

The file covers the type a caller catches when the library refuses a request. It is here and not
in a module because the refusal it uses is raised by `RuntimeContext` and caught through `Runtime`.

### `AFilterThatExcludesEveryDeviceThrowsUnsupported`

A filter that admits no device class gives `SushiException` with `Errc::unsupported`. Topology
returns an empty list for that filter and `RuntimeContext` turns the empty list into the failure;
`test_device_selection.cpp` checks that the message names the filter.

### `AHandlerForRuntimeErrorReceivesTheSameFailure`

`SushiException` derives from `std::runtime_error`, so a handler written for the base class still
catches it. The exception is thrown inside the shared library and caught in the test program, and
the `dynamic_cast` shows its type survives that boundary.

## test_error_isolation.cpp

A task exception must abort only its own run and must not kill the worker pool. The run that threw
surfaces the error to the caller, and a fresh graph on the same `Runtime` must still run and produce
correct results afterwards. The defect this file guards against latched `shutdown_requested` on a
single throw and left the pool dead for good.

### `ThrowingKernelLeavesRuntimeReusable`

The command-group function of the first graph throws, so `execute()` must report the error to the
caller and not hang.

## test_graph_profiling_switch.cpp

The file covers the per-graph profiling switch: section B of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`, built as task 4 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-plan.md`. It checks that
`set_profiling()` is independent across graphs, that the plan recompiles once, and that
every device-dispatch branch the scheduler can route to a profiling queue does so.

### `NUMAFallbackBranchUsesProfilingQueue`

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

## test_logging_defaults.cpp

The file covers what a program sees of the logger when it creates a `Runtime`. It is here and not
in the `logging` module because the lines it looks for are logged by `device` and `topology` while
`Runtime::create()` runs.

### `Integration_LoggingDefaults`

The fixture puts the logger in synchronous mode with the default sink, and afterwards restores the
default sink and the level the process had. Synchronous mode also waits for records that earlier
tests of the program queued, so none of them lands inside a capture.

### `capture_streams_around_create`

The helper creates and destroys a default `Runtime` between GoogleTest's `CaptureStdout` and
`CaptureStderr`, which redirect the two file descriptors. The streams are captured this way and not
through a sink because the claim is about what reaches the terminal, including anything written
past the logger.

### `CreateIsSilentAtTheDefaultLevel`

At `Warn` both streams are empty. The test sets `Warn` itself, since `SR_LOG_LEVEL` in the
environment of the test run would otherwise decide the level. Every start-up line of a healthy
machine is `Info`; a machine on which start-up warns fails this test, and the captured text is in
the failure message.

### `DeviceLineArrivesThroughTheSinkAtInfo`

At `Info` with a sink installed, the sink receives the `Device: ...` line of
`device_queue_set.cpp` at level `Info`, and both streams stay empty because the sink replaced the
default.

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
of the shared library. The section on `test_steady_state_allocation.cpp` in
`tests/allocation/README.md` explains why that test needs its own binary.

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

## test_offload_seam_shutdown.cpp

`RuntimeContext::shutdown()` must stop the installed offload coordinator before the engine stops
the worker pool, as the destructor does. A coordinator returns the nodes it still holds through the
sink inside `stop()`, and a pool that has already joined its workers would never run them. The test
names `RuntimeContext`, an execution header, so it is here and not beside the scheduler tests of
the seam in `modules/engine/scheduler/tests/test_offload_seam.cpp`.

### `PoolProbingCoordinator`

In its first `stop()` the fake hands a probe node to the sink's `resubmit()` and waits up to ten
seconds for a worker to run it. The probe was never accepted by `try_offload()`; the fake adds the
one reference `resubmit()` consumes. A later `stop()` only counts the call.

### `ShutdownStopsCoordinatorBeforeEngine`

The stop count is 1 after `shutdown()` and the probe has run, which only a live pool can do. The
count is 2 after the context is destroyed: the destructor stops the coordinator again, which
`OffloadCoordinator::stop()` allows.

## test_perf_controls.cpp

The file covers the rebalancer control: the rebalancer is off unless requested, execution is correct
in both states, and the engine reports the flag faithfully.

The default is the assertion that matters. The background thermal rebalancer migrates tasks between
workers on a 5 ms heartbeat. That changes which worker folds which tile and puts a periodic wakeup
under every frame, a throughput trade that gains a simulation frame nothing. When the rebalancer was
on by default, every consumer paid for it, and the primary consumer turned it off at every
construction.

## test_scheduler_integration.cpp

The scheduler checks in this file are fast and deterministic; heavy load is tested under
`tests/benchmark`. The scheduler must run exactly the tasks submitted, honor priority bypass,
and survive a clean round of arm, submit and stop.
