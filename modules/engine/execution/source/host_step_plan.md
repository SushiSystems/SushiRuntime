# host_step_plan

**Describes:** `modules/engine/execution/include/SushiRuntime/execution/host_reachable_memory.hpp`, `modules/engine/execution/include/SushiRuntime/execution/host_step_plan.hpp`, `modules/engine/execution/source/host_step_plan.cpp`

The check of [the RUN_PLACEMENT page](../../../../docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md)
that says whether a plan's steps may run on the calling thread. The [module README](../README.md)
links this page.

## HostReachableMemory

The port `host_step_refusal()` reads memory through: `host_reachable(base)` says whether the
calling thread may touch the allocation that starts at an address. The USM adapter answers true for
an allocation the context's `USMAllocator` recorded as host or shared USM, and false for device USM,
adopted memory with no kind, and an address it holds no record of. It lives in
`source/host_step_plan.cpp`, and a test passes a table of its own.

## host_step_refusal

Says whether a plan's steps may run on the calling thread when a graph's run placement policy
answers `RunPlacement::Host` (`docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md`). It returns a
string literal naming the first refusal, or null. Any doubt keeps the plan on the device: an empty
plan, a node with neither native host work nor a host form, a node an offload coordinator prepared,
a node that names no region, and a region or a live address that the memory port does not reach.
A native host node is accepted whatever it names, since it already runs on a host thread. The form
that takes a `RuntimeContext` wraps the context's allocator in the USM adapter. It walks every node
and asks the allocator once per region, so the step loop calls it only for a run the policy placed
on the host.
