# Layered execution: report

**Status:** Open — R1 to R8 and half of R9 landed on 2026-10-24; all but R1 are not built yet.

## R1

A node carries a `PlacementClass` and an optional placement key from `NodeOptions` through its
stamp and its `TaskMetadata` to `PlanNode`.

The two fields sit after `parameters` in `NodeOptions`. Placed after `operation`, they broke
the positional initialisation `NodeOptions{width, name, operation, parameters}` in
`tests/surface/api/graph/test_node_options.cpp`, and would have broken any caller written the
same way.

Verification: `sr build -j 4` passed; `sr test` passed 801 of 801, three skipped as before
(`Medium_DevicePipelineTest.DiamondAllKernelNodes`,
`Medium_InteropHostExport.ImportOnAQueueWithoutTheExtensionReturnsUnsupported`,
`Medium_SteadyStateAllocation.ProfiledReuseTicksDoNotAllocatePerTick`).

## R3

`Runtime::topology()` returns the machine's `NodeFacts`, built once in `create()` before the
context, so a refused label builds nothing. The facts sit in `Runtime` by value, beside the
context handle. The host name and the operating system come from the adapters
`host_identity_windows.cpp` and `host_identity_posix.cpp`; an unreadable host name with no
`RuntimeConfig::node_name` makes `create()` return `Unsupported`.

Seen and kept: `network_socket` has its own Winsock `host_name()`, which `api` cannot reach from
its tier, so the host name is read in two places.

Verification: `clang++ -fsyntax-only` of every changed source; the comment, naming, layering,
test-mirror, docs-truth and changelog checkers pass. Not built: the owner ruled that each
repository is built once at the end.

## R2

`Recorder::within()` holds the open scope in `placement_scope_`, set and put back by a
`PlacementGuard`, so a throwing callable closes its scope. `Recorder::record()` is the only place
a stamp is built, so every recording verb, and the `Graph`, `Region` and `DynamicGraph` verbs that
reach the recorder through `BuilderSurface::sink()`, take the scope. A malformed scope name stops
the process through `SR_VERIFY`. `PlacementScope` has a public constructor: it guards no
invariant, the name rule being `Runtime::placement_scope()`'s.

Seen and kept: an exception from the callable passes through `within()` unchanged, as it does
from the caller's own code; the library adds none.

Verification: syntax checks and the checkers, as for R3. Not built.

## R4

The new public module `placement` holds the rule records and `PlacementRules`. `add()` checks a
rule in the order match, class pattern, target, policy, and reports the first refusal; a rule's
hashes are kept beside it in one private `Entry` vector. `resolve()` is a linear pass, run once
per node per compile. A `Local` or `Fail` rule ignores its `bound`.

Verification: syntax checks without `-fsycl` and without a SYCL include root, which proves the
module includes no SYCL header; the checkers pass. Not built.

## R5

The rule set and its generation sit in `RuntimeContext` as plain members; the refusal during a
run keeps a set and a read apart for a caller that sets rules and starts runs on one thread, and
the doc comment says a caller on two threads orders them itself. `PlanCache::stale()` takes the
context and compares the generation the plan was built under. The compile resolves each node in
the replay callback into `TaskMetadata::placement_rule`, and the plan cache state keeps the rule
set it was built with alive.

Left for later: `DynamicGraph` and `RegionComposer` resolve no rule, so a node of a dynamic graph
reports none.

## R7

`HELLO` gains the operating system, a device list and a label list after its version 3 fields,
as `std::string_view` records into the payload; counts are bounded by the bytes left.
`TASK_DISPATCH` gains a u32 device index after the output handles, so earlier offsets stay; the
master sends 0 until R10 and the worker ignores it until R9. The worker reads labels from
`SR_WORKER_LABELS` first, then from each `--label`; a key cannot hold `=` because the text splits
at the first one, and a key or value over 255 bytes is refused, the limit of a `HELLO` string.
`WorkerAgent` copies `Runtime::topology()` into the link options.

The golden bytes were worked out by hand and have not met the encoder: the final build runs
them.

Seen and kept: `wire_messages.hpp` holds 18 types and its source is over 450 lines; splitting it
is its own task.

## R8

The worker directory keeps one `NodeFacts` per worker name in a map whose entries are replaced
in place and never erased, so a reconnect under the same name finds them and a lost peer leaves
them. A `HELLO`'s facts wait in the session until `accept_proof()` accepts, so a peer that has
not proved the secret cannot overwrite a registered worker's facts. `pick()` calls the policy
under the directory's mutex, which every write to the map also takes, so `WorkerLoad::facts` is
a raw pointer valid for the call. Two live workers with one name share an entry.

## R6

`PlacementScope` holds its class and key, the runtime's `ContextHandle` and a
`shared_ptr<const NodeFacts>`; `Runtime::topology_` became that shared pointer so a scope stays
valid after its runtime moves. `PlacementScope::device()`, defined in `placement_scope.cpp`, is
the one place a scope reads rules. `buffer<T>()` allocates through `Buffer<T>::allocate`, which
now has `PlacementScope` as a friend beside `Runtime` and `Interop`. `On(scope)` resolves when
the kernel is recorded; a failed rule leaves a refusal on the recorder, which `run()` returns,
and records no node. A scope built from its public constructor has no runtime: device 0, and
`buffer()` returns `InvalidState`.

Seen and kept: `Buffer<T>` gains a third friend; the vocabulary header of `On` now includes the
scope's header; `topology()` on a moved-from `Runtime` dereferences null. The two-device tests
check the buffer's device and the node's write region, since a plan node reports no device.

## R9, first half

The executor takes the worker's `DeviceQueueSet` and calls each kernel on the queue of the device
its dispatch names; `K_DEVICE_INDEX` now names only the cache's device. A dispatch for a device
the worker lacks, or whose queue sits in another context than device 0's, fails with
`KernelFailed` and one WARN, and its kernel is not called. Shared USM on device 0 is reachable
from every device of one context, so a dispatch on device 1 reads the same bytes.

Not done: the kernel does not run through the worker's scheduler, and dispatches do not overlap.
No public verb hands a body a `sycl::queue&` on a named device, and no `Buffer` adopts USM the
worker's cache already holds, so the scheduler route needs a public interface change; it waits
on the owner. One exec thread stays; `pulling_` assumes one pull at a time.

Seen and kept: `DispatchExecutor` has seven duties (queue, exec thread, pull, pins, kernel call,
output stream, held ids); `KernelFailed` cannot tell a missing device from a kernel that threw.

## R15

The owner ruled on 2026-10-24 that a node's determinism class is a runtime `NodeOptions` field
rather than a parameter slot or a SushiBLAS-only fact. `Determinism` sits in `core` beside
`PlacementClass`, and the field follows R1's path through the stamp and `TaskMetadata` to
`PlanNode`. The runtime stores and reports it and never branches on it.

## R10

A placement is a `PlacementDecision`, a `std::variant` of run on a worker and device, run
locally, wait until a time, or fail with an error, visited once in
`OffloadDispatcher::try_offload`; `OffloadPolicy::pick_worker` keeps its signature and the
dispatcher wraps its answer. `RulePlacement` decides when a rule set is in force and the node has
a winning rule: a rule whose machine part matches the master's topology, or that names no machine,
runs locally; otherwise `WorkerDirectory::pick_target` answers picked, busy or absent under the
directory's lock. Absent applies `if_absent`. Busy holds the node in `HeldNodes` whatever
`if_absent` says, so a pinned node never runs on the master while its worker lives. Held nodes
are offered again on a registration, after a task result, after a resubmission and when a bound
passes; a hold that a release overtook is stale and decided again. `Fail` reaches
`OffloadSink::fail`, which records the error into the run's outcome. A rule's `deadline` rides in
`RunOnWorker` into `PendingTasks::insert`. The matching of a target against a machine's facts
moved into `placement` and serves the local scopes too.

The owner ruled the busy hold on 2026-10-24, for SushiAI's repeatability.

Seen and kept: the distributed coordinator holds a `const RuntimeContext&` whose lifetime its type
does not enforce; a pinned node whose dispatch fails twice runs locally after
`K_MAXIMUM_OFFLOAD_ATTEMPTS`; a short rule deadline is applied up to one monitor period (at most
250 ms) late; every release re-offers every held node.

Owed: `docs/guides/DISTRIBUTED_GUIDE.md` has no section on placement rules, and the page is at its
600-line ceiling; it is split when the programme closes.

## R9, second half

The owner chose a borrowed buffer on 2026-10-24. `Interop::borrow<T>(address, count, device,
residency, owner)` returns a plain `Buffer<T>` that keeps a weak observer of the owner's liveness
token and frees nothing; a run after the owner lets go is refused as `DanglingResource`. A new
`Recorder::task` overload states a node's device. The worker's executor records each dispatch as
one placed task over borrowed cache buffers and runs a batch of them in one `Graph::run()`, since
the runtime runs one graph at a time; a job that reads what a job of the batch writes starts the
next batch. Each task body waits for its dependency events, calls the kernel and copies its
outputs; output streams send that copy and hold no cache pin, which closes a race where a later
writer could overwrite bytes a stream was still sending. Pulls stay serial on the exec thread.

The worker cache allocates with `sycl::malloc_shared` on device 0 instead of the runtime's
allocator: the scheduler places a tracked node by the owner of its memory first, so recorded
memory would pull every task to device 0. Letting a stated placement win over the memory's owner
when both share a context would be the cleaner rule; it is an engine change for the owner.

Seen and kept: `DispatchExecutor` holds queueing, batching, recording and settling in about 1000
lines; a task body blocks a pool thread while it waits, because a registered kernel takes no
events; a job that arrives while a batch runs waits for that run.

KI-6 and RW-17 close in the commit that records the final build's passing tests.

## R12

The version already moved: `complete_local()` bumps the stored output handles of a fixed-buffer
node, or the live writes of a double-buffered one, when a node that stayed local completes. The
two new end-to-end tests pin the case the old test missed, a worker that already holds the old
version, and the cost of two local writes before one dispatch: one pull. No source changed.

## R11

Both answers are no, by the code, and four tests pin them. `SchedulerExecutionEngine::submit()`
sets the run marker for the whole run, and `entry_refusal()` refuses a `Graph::run()`, `compile()`
or `run_async()` while it is set, before the pool is touched; a registered kernel on a worker and
its local fallback on the master both run inside a run of their runtime. A second `Runtime` has
its own marker and runs the inner graph. The tests bound every wait at 10 s, so a hang would fail
them. `test_registered_task.cpp` moved into `test_registered_task/` beside the probe, as the test
mirror wants a unit's extra test files in a folder of its name.

## R9, the placement rule

The owner ruled that a stated placement wins over the device owning a node's memory in one SYCL
context. `placement_of()` returns a `Result`: a stated device over owned memory wins when it
shares the owner's context and is refused as `InvalidGraph` naming both devices when it does not;
a node with no stated device runs on its owner; a node with no owner runs on its stated device or
device 0. The context check reads `USMAllocator::shares_context()`, since the allocator holds the
queues. `NodeDescription::device` became optional: `Recorder::task(access, body)` used to state
device 0 for every tracked task, which the new rule would have turned into an override. The
worker's cache allocates through the runtime again.

Owed at the final build: the ASan lane, since the scheduler's placement path changed.

Seen and kept: `dispatch_placement.cpp` repeats the anchor selection `node_placement.cpp` keeps
privately.

## R16a and R16b, the branch review

A review of the branch found that a rule's device part never reached a compiled node and that a
dynamic graph ignored rules. One resolver in `placement`, `device_on()`, answers where a target
puts a node on a machine (its machine part matches and its device part picks a device there);
`RulePlacement`, the worker directory, `PlacementScope` and both compiles use it, and the compile
writes the picked device into the node's stated placement, so the stated-placement path runs it.
With no coordinator, the compile applies the absent policy itself. A dynamic graph stamps each
rebuilt node at commit and goes stale on a rule change. A wait bound or deadline is at most one day,
the sum saturates, a local scope's `Wait` is immediate, and machine names compare by their
lower-case first DNS label, so two hosts that differ only after it compare equal.

The rule set is one immutable record swapped atomically; a change takes an idle-change bit of the
run marker by compare-exchange, so a run cannot start between the check and the swap, and a run
pays one branch. A refused re-offer forgets its node, `PendingTasks` publishes its earliest deadline
to the monitor, and a worker fact over 255 bytes is refused before connecting instead of cut. The
host identity lives in the topology module; the dispatch executor's recording and settling are
their own units.

R16c: the context hands out the rules, their generation and whether a coordinator is installed as
one snapshot, which every compile and commit takes once; the region composer had read the rules at
a commit's start and the generation at its end. Installing or removing a coordinator publishes a new
snapshot, so plans compiled under the other absent policy recompile. The machine's facts live in the
context, so a graph carries none. `task_resubmission` sits in the offload-coordination module, where
its forget is tested. `DistributedCoordinator::prepare()` still reads the snapshot again during a
compile, since its interface cannot receive the compile's; R16d hands it the snapshot.

## Dispatches

```
dispatch: R1 — sonnet medium, 36 turns, passed, rung 1
dispatch: R3 — sonnet medium, 57 turns, passed, rung 1
dispatch: R2 — sonnet medium, 46 turns, passed, rung 1
dispatch: R4 — sonnet medium, 36 turns, passed, rung 1
dispatch: R5 — sonnet medium, passed, rung 1
dispatch: R7 — sonnet medium, 76 turns, passed, rung 1
dispatch: R8 — sonnet medium, 45 turns, passed, rung 1
dispatch: R6 — sonnet medium, 73 turns, passed, rung 1
dispatch: R9 — sonnet medium, 44 turns, partial (public interface needed), rung 1
dispatch: R16a — opus low, 116 turns, passed, rung 1
dispatch: R16b — opus low, 160 turns, passed, rung 1
dispatch: R16c — opus low, 125 turns, passed, rung 1
dispatch: R15 — haiku high, 68 turns, passed, rung 1
dispatch: R10 — opus low, 170 turns over two rounds, passed, rung 1
dispatch: R9 second half — opus low, 146 turns, passed, rung 1
dispatch: R12 — sonnet medium, 28 turns, passed, rung 1
dispatch: R11 — sonnet medium, 45 turns, passed, rung 1
dispatch: R9 placement rule — opus low, passed, rung 1
```
