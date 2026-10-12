# Unit tests

The tests in `tests/unit` each exercise one component and name headers of more than one module by
path, which is why they are here and not in a module. A module's own tests are documented in
`modules/<tier>/<module>/tests/README.md`. This document records what each test proves and why it
is built the way it is. A section is named after a test file, and a subsection after the test,
helper or topic it describes. The source comments state one invariant per line and cite this file.

## test_node_dispatch_width.cpp

The width of a node is stamped after the node is added, as its name is: the emitters build the
node before they know which of several internal widths it took.

`Node` is a non-literal type, so `Node{}` cannot be an operand of a `static_assert`; the compiler
reports "non-literal type 'Node' cannot be used in a constant expression".

### `BakedNodeReportsOneWidthTwice`

A node with no late bindings reports its baked width as both launched and live, because nothing
narrows it at run time.

### `DynamicNodeSeparatesCapacityFromLiveCount`

A dynamic node launches over its capacity and does work over its live count. The instrument has to
show both, or the narrow kernels look wide.

### `UseBakedFallsBackToTheBakedWidth`

`K_USE_BAKED` means "the rest of the capacity" and is not a count, so it must not be reported as a
measured live width.

### `OversizedWidthReportsUnknownRatherThanTruncating`

A width that does not fit in `uint32_t` is reported as unknown, not truncated. `launched_width` is
32 bits wide, and a wrapped value would read as a real, wrong measurement.

## test_numa_steal.cpp

The file validates NUMA-aware hierarchical work stealing on single-node hardware by injecting a
synthetic topology. A `RemappingTopologyProvider` spreads the cores across N nodes (core i goes to
node i % N) with a matching `MatrixDistanceModel`. The tests then assert the locality invariant:
victims on the local node come before remote ones.

## test_nvidia_topology.cpp

The file validates that an NVIDIA GPU is discovered through the CUDA SYCL
backend and mapped to a NUMA node by hwloc. The tests skip when there is no CUDA device.

## test_topology.cpp

The file covers hardware discovery and NUMA mapping, edge cases included. The invalid-NUMA fallbacks
check that the runtime degrades without failing on bad input.

## test_topology_abstraction.cpp

The file validates the injectable topology abstraction, `TopologyProvider` and
`NUMADistanceModel`. It exercises multi-NUMA logic deterministically on single-node hardware, which
the real hwloc path cannot do on a machine with one socket.
