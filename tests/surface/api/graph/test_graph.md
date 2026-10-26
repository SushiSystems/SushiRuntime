# test_graph.cpp

The file covers the native CPU execution path, work package WP-7 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN/`. `add_host()` runs plain C++ on a worker thread
with no SYCL submission. The task is ordered into the graph by the data it touches and shares one
completion contract with kernel tasks.

| Test | What it proves |
| --- | --- |
| `HostTaskRunsAfterKernel` | A native host task ordered after a device kernel must see the kernel's output. The task is dispatched only once the kernel's event has completed, so the shared USM the kernel wrote is visible on the host. The ordering comes from the shared buffer `a`. |
| `KernelRunsAfterHostTask` | A device kernel ordered after a native host task must see the host's writes. The kernel waits on the inline completion of the host task before it is dispatched, and shared USM written on the host is visible on the device. |
| `UntrackedHostTaskRunsEveryStep` | An untracked native task runs once per step, which proves that the form without dependencies is both a root and a leaf and replays on every step. |

### `ThrowingHostTaskKeepsRuntimeUsable`

A native task that throws fails the run, which is the error isolation of work package WP-0, and the
runtime stays usable: a fresh graph runs cleanly afterwards. Native tasks therefore share the kernel
error path and do not latch the worker pool dead.

The file covers per-dispatch node event records, the clock pairing and steady-state capture. The
design is section C and items 1, 2, 3, 4 and 6 of section E in
the g1 node event records design spec in the version history, built as task 5 of
the g1 node event records plan spec in the version history. The item numbers in the
subsections refer to section E of the design.

| Test | What it proves |
| --- | --- |
| `NodeEventRecordsTestAccess` | `NodeEventRecordsTestAccess` is a friend used only by tests to reach the compiled plan of a `Graph`. It mirrors `RegistrationIndexTestAccess` in `graph/test_recorder.cpp`. |
| `RequestedGraphRecordsOnePerDispatch` | Item 2: three independent kernel nodes and `run(5)` give fifteen records, invocations 0 to 4 per node, and `submit <= start <= end` for every record. |

### Steady-state capture

Item 3 is tested in `test_sushiruntime.cpp`, which is built into the
`sr_api_allocation_tests` executable and so runs in its own process. A replacement `operator new` in
`graph/test_graph.cpp` would see only the allocations of its own executable and never those
of the shared library. The opening of this file explains why that test needs its own binary.

### `NodeIndexIsRegistrationOrder`

Item 4: `node_index` follows the registration order, not the compiled order, which is reversed. The
test cross-checks it against the plan's own `registration_index`.

A strict dependency chain, in which B reads what A wrote and C reads what B wrote, forces the
execution order A, B, C. That gives an observation of identity which does not depend on trusting the
`registration_index` read from the `Node` object. If the registration order of the plan and the
dispatch order of the run ever disagreed, sorting by earliest submit would catch it, even though
both would still read as {0, 1, 2} on their own.

### `HostNodeLandsAfterItsKernelOnTheSteadyClock`

Item 6: the record of a native host node is on the steady clock and carries `K_HOST_DEVICE_INDEX`. The
kernel's record goes through its device's pairing, and the host record must not start before the kernel
ended, within the `uncertainty_ns` of that pairing.

The host node reads what the kernel writes. That chains it strictly behind the kernel's completion;
the two are not independent roots. The test tells a host record from a device record by its device
index, which `NodeEventRecord` carries as its field.

The chain guarantees that the host node is dispatched no earlier than the kernel's completion. The
tolerance covers only the measurement uncertainty of the pairing, not scheduling jitter between
unrelated roots.

The file covers the overloads that take a caller-owned `RunReport`: `run(RunReport&)`,
`run(RunReport&)` and `RunHandle::wait(RunReport&)`.

The tests pin down two properties that pull against each other. The overloads exist so that a frame
loop keeps one report alive and the per-run vectors stop being reallocated, so the report must not
be rebuilt from scratch. A report carried across ticks must also never let the numbers of the
previous run survive into the current one, and not rebuilding invites exactly that. The resolution
is to reset and then refill: the capacity stays and the values do not. Both halves are asserted,
also on the failure paths, where stale numbers would mislead most: a caller that ignored the
returned error would otherwise read a report that looks successful.

| Test | What it proves |
| --- | --- |
| `record_write` | `record_write` zeroes `buf` and records the one task every test in the file runs: element i gets i+1. It takes both handles by reference and does not return them, because a `Buffer` must not be moved once a graph has recorded it; the graph would keep pointing at the old address. |
| `ReuseReportsWhatTheValueFormReports` | The reuse overload must report what the value-returning one reports. If the two drivers ever diverged, every number a consumer reads from the cheap path would silently become a different measurement from the documented one. |
| `SteadyStateRunsDoNotReallocateTheReport` | The overload exists so that the per-run vectors are refilled in place. Comparing the address of the row before and after proves that no reallocation happened. A capacity check alone would pass even if the vector were rebuilt into a new block of the same size. |
| `EachRunOverwritesTheLastOne` | Reuse must not accumulate. A short run that follows a long one has to read as the short run, because the count belongs to the run, not to the report. |

### `ARejectedRunLeavesNoStaleNumbers`

A rejected run must clear the report it was handed. The dangerous case is a caller that checks the
returned error only sometimes. Reading a stale `is_successful` of true from a run that never
happened is worse than reading zeroes, so the failure path resets the report and marks it
unsuccessful.

| Test | What it proves |
| --- | --- |
| `TryRunReturnsTheCallersReport` | `run(report)` returns a pointer to the report it was given, so a caller can keep using the result of the call without holding both. |

### `AsyncWaitFillsTheCallersReport`

The asynchronous path settles into a caller-owned report too. The overlapped frame loop is the one
that most wants the overload.

A run must be cancellable from another thread. An otherwise unbounded `run_until` has to return
promptly when a second thread calls `cancel()`, and the pool must stay usable so that a later run
completes normally. Cancellation is reset per run; it does not latch.

The file covers layer 1 of `docs/design/GRAPH_REPLAY/README.md` (sections C and G, phase P1).

A chainable plan runs as an in-order chain, matches the scheduler bit for bit, and names the path it
took in `RunReport::layer`.

### `ANamedNodeRunsOnTheChainWithNoClusterAttached`

A kernel and a State rule each name an operation and one parameter in their `NodeOptions`. No
cluster is attached, so nothing can take either node. The run of three steps must report
`ExecutionLayer::Chain` and leave the values an unnamed graph leaves: a named node is a candidate
for another machine only where one can run it.

The file covers the reusable completion latch, section 5.1 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN/`. `execute()` allocates no promise and no future
per step. One latch on the engine is reset, armed and waited on each step. These tests pin the
behaviour the latch must preserve: a long replayed run drives every step and reaches the right
state, and the same plan re-runs cleanly afterwards, which requires the latch to reset and the
sentinel to re-wire.

| Test | What it proves |
| --- | --- |
| `ReRunsCleanlyOnTheSamePlan` | The test makes three separate `run()` calls on one compiled plan. The latch must reset between them, and the sentinel, which is wired once, must keep signalling. |

### `SingleStepRunsRepeatedlyWithoutLeak`

Many independent single-step runs stress the reset, arm and wait cycle hardest. Each run allocates
nothing on the completion path.

A single kernel cannot dereference USM from two device contexts that are not peers. The scheduler
detects such a node on its first dispatch and fails the run with a precise error, before the node
can make an illegal cross-context access. The guard is section 5.4 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN/`. Splitting data together with the work across
devices belongs to work package WP-5 of the same document, and until that is built the guard
enforces the co-location contract. The check needs at least two device contexts, so the test skips
on a single-device machine.

### `NodeSpanningTwoContextsFailsCleanly`

The node reads data in context 0 and writes data in context 1, and no single queue can run it
correctly.

The run fails with a clear error and not with an illegal memory access, and the runtime stays usable
afterwards, which is the error-isolation contract.

The file is the determinism conformance suite: the same graph on the same machine must produce
byte-identical output however many workers ran it.

Rollback netcode and replay files are built on this guarantee, and two independent things can break
it. The scheduler can: if a node could start before a node it reads from had finished, or a
work-item could see a neighbour's partial result, a two-worker run and a sixteen-worker run would
disagree in the low bits and nothing would report an error. The compiler can: fast-math licenses reassociation, so the same source may be
folded in a different order in two builds, and FMA contraction rounds once where the source says
twice. This suite varies the pool size, which the `worker_count` setting from item BB-2 of
`docs/design/ENGINE_BACKBONE_REFACTOR/` makes possible, and it separately asserts the compiler
contract, which no scheduling discipline can replace.

Every comparison is on the raw bytes. A check with a tolerance would pass on exactly the divergence
that ruins a replay a thousand steps later.

### `order_sensitive_value`

The input is one large value followed by many small ones. Each small value vanishes when added to
the running total but survives when the small values are summed together first. A sum over this
input gives a different number for a different combination order, so the bit comparison can fail.
Without such an input the test would pass on a runtime that reassociated freely.

| Test | What it proves |
| --- | --- |
| `alternating_value` | `alternating_value` is a second pattern that alternates in sign, so that a divergence which happened to cancel in the first total still shows in the output. |

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

| Test | What it proves |
| --- | --- |
| `bytes_equal` | `bytes_equal` compares bytes, not numbers. A difference of one ulp must fail, and NaN payloads must compare as the bits they are. |
| `first_difference` | `first_difference` returns the index of the first differing element, so that a failure message can name the element. |

### `TheFloatingPointContractReachesConsumerCode`

The test asserts the compiler half of the contract where a consumer would feel it. This translation
unit is not part of the library and compiles against the installed-style interface, so a definition
that arrives in it is evidence that the floating-point contract travels with the target and does not
stop at the library edge. Restoring `/fp:fast` or `-ffast-math` fails the test at compile time,
which is the property item BB-3 of `docs/design/ENGINE_BACKBONE_REFACTOR/` asks the suite to have.

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

| Test | What it proves |
| --- | --- |
| `TheRebalancerDoesNotChangeTheBytes` | The whole machine runs the workload with the background rebalancer off and then on. Task migration reassigns work-items to workers in the middle of a run, and the output must not move by a byte. |

### `ReplayDoesNotDriftAcrossSteps`

A replayed graph must not drift. Every step of a multi-step run recomputes the same total from
the same input, so step 1 and step `STEPS` have to produce the same bytes. A drift would mean that
per-step state of the plan had leaked into the arithmetic.

The file covers how an added observer reaches the graphs of a runtime: section B of
the g1 node event records design spec in the version history, built as task 4 of
the g1 node event records plan spec in the version history. It checks that an observer turns timing
on for every graph of its runtime, that no plan recompiles, and that
every device-dispatch branch the scheduler can route to a profiling queue does so.

### `NUMAFallbackBranchUsesProfilingQueue`

A node with no declared `Reads` or `Writes` leaves the scheduler's anchor unset. That forces the
NUMA-local fallback branch and not the cached-index branch that the other two tests of the file
exercise. Nothing public exposes which branch a node took, because `Node::cached_queue_index_` is
private to the scheduler, so the test asserts the observable effect of the fallback: a profiling
queue is built and timing is collected. If the fallback still landed on the plain queue, the
poller's timestamp read on that event would throw, because the plain queue is not built with
`enable_profiling`. A regression therefore fails loudly and does not merely lose timing.

One branch is left: the NUMA fallback of `devices().queue_or_numa_local()`, taken when
`memory().owning_device()` does not recognise an anchor. A test
cannot force it safely. Reaching it needs a device kernel that dereferences a pointer the runtime
never allocated, which is an illegal access on a real GPU backend.

The file covers the observation of a run, section 5.5 of
`docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN/`. Device timing is on while an observer is added to the
runtime. With none, a run hands out no observation. With one, the observation carries the node table
and one record per dispatch. Explicit transfers between host and device are counted by the runtime in
both cases.

| Test | What it proves |
| --- | --- |
| `ProfilingOnCapturesPerNodeTiming` | Both kernels appear as distinct plan nodes, each dispatched once per step. The magnitudes of the device times depend on the backend and on noise, so the stable claim is structural: the per-node breakdown is present and correctly counted. |

### `NamingTheLastNodeReachesTheNodeTable`

Any node can be given a diagnostic name that reaches its row of the observation's `NodeTable`. This is requirement R8
of `docs/design/PHYSICS_SUBSTRATE_REQUIREMENTS/README.md`. Without a name every node reports as
"unnamed_task" whichever `add()` produced it, and a profile of a real graph is a list of identical
labels.

The test sets the name with `name()`, which names the rule the verb before it recorded.

| Test | What it proves |
| --- | --- |
| `EveryAddShapeCanBeNamed` | The test names a node of every launch shape: counted, captured, a State rule, host work and a dynamic kernel. |
| `UnnamedNodeStillReportsTheDefaultLabel` | A node added without a name keeps the documented default label, so a call site without a label is still visible in the report. |
| `TransferBytesAttributedToTheRunWindow` | A host node pushes data during the run. The bytes must land in the window of that run, not before it. |
