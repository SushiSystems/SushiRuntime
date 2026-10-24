# Layered execution: report

**Status:** Open — R1 and R3 landed on 2026-10-24; R3 is not built yet.

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

## Dispatches

```
dispatch: R1 — sonnet medium, 36 turns, passed, rung 1
dispatch: R3 — sonnet medium, 57 turns, passed, rung 1
```
