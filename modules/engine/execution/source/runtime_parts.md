# runtime_parts

**Describes:** `modules/engine/execution/source/runtime_handle_parts.hpp`, `modules/engine/execution/source/runtime_coherence_parts.hpp`

How `RuntimeContext` holds its subsystems and what it hands out. The [module README](../README.md)
links this page.

## Handle ports

`RuntimeContext::handle_ports()` returns a `HandlePorts`: the handle table by role. `registry` mints,
describes and retires; `residency` reads and writes what each handle holds where; `host_writes`,
`history` and `retirement` are the host-write log, the coherence history and the retire observer
slot. The table's class, `ResidencyDirectory`, is named only by `RuntimeHandleParts`. The API layer
mints through `registry`, planners and the scheduler read through `residency`, and the distributed
layer installs its observer through `retirement`. A new role adds a port to `HandlePorts` and leaves
the accessor alone.

## Owned parts

The context owns its subsystems as parts, each built by one factory in its own unit.
`RuntimeHandleParts` holds the directory, the Borrowed index and the coherence horizon.
`RuntimeCoherenceParts` holds the memory spaces, the transfer paths and the observer they report to,
the copy cost, the eviction policy and the partitioner; `make_coherence_parts()` builds it. A new
subsystem is one more part, held by one member of `Impl` and reached by one accessor. The existing
accessors keep their signatures.
