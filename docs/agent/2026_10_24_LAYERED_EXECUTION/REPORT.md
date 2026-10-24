# Layered execution: report

**Status:** Open — R1 to R5 landed on 2026-10-24; R2 to R5 are not built yet.

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

## Dispatches

```
dispatch: R1 — sonnet medium, 36 turns, passed, rung 1
dispatch: R3 — sonnet medium, 57 turns, passed, rung 1
dispatch: R2 — sonnet medium, 46 turns, passed, rung 1
dispatch: R4 — sonnet medium, 36 turns, passed, rung 1
dispatch: R5 — sonnet medium, passed, rung 1
```
