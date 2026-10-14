# numa_lookup

The module finds the NUMA node local to a hwloc object, the one lookup that hardware discovery, the
topology provider and the NUMA mapper share. It is an internal module: its headers reach the modules
that name it and the tests, and are never installed. Its names are in
`SushiRuntime::Internal::Topology`.

It depends on `core` and on hwloc.

## Public entry points

| Header | Holds |
| --- | --- |
| `numa_lookup.hpp` | `local_numa_os_index()` |

## numa_lookup.cpp

In hwloc 2 a NUMA node is a memory child, not a link in an object's parent chain, so
`hwloc_get_ancestor_obj_by_type(..., HWLOC_OBJ_NUMANODE, ...)` finds none. `local_numa_os_index()`
walks up from the object to the first one with a non-empty `nodeset` and returns the OS index of
the lowest node in it. A PU or core has its own nodeset; a PCI device has none and takes its
first non-I/O ancestor's. The three lookups of this module (`hw_discovery.cpp`,
`hwloc_topology_provider.cpp`, `numa_mapper.cpp`) go through it, and each keeps its own fallback
for an empty answer.
