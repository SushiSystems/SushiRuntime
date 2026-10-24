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
dispatch: R15 — haiku high, 68 turns, passed, rung 1
```
