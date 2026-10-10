# Unit tests

The tests in `tests/unit` each exercise one component and name headers of more than one module by
path, which is why they are here and not in a module. A module's own tests are documented in
`modules/<tier>/<module>/tests/README.md`. This document records what each test proves and why it
is built the way it is. A section is named after a test file, and a subsection after the test,
helper or topic it describes. The source comments state one invariant per line and cite this file.

## test_node_dispatch_width.cpp

The width of a node is stamped after the node is added, as its name is. `add_node` returns `void`,
and the emitters build the node before they know which of several internal widths it took.

`Node` is a non-literal type, so `Node{}` cannot be an operand of a `static_assert`; the compiler
reports "non-literal type 'Node' cannot be used in a constant expression".

### `BakedNodeReportsOneWidthTwice`

A node with no late bindings reports its baked width as both launched and live, because nothing
narrows it at run time.

### `DynamicNodeSeparatesCapacityFromLiveCount`

A dynamic node launches over its capacity and does work over its live count. The instrument has to
show both, or the narrow kernels look wide.

### `UseBakedFallsBackToTheBakedWidth`

`USE_BAKED` means "the rest of the capacity" and is not a count, so it must not be reported as a
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

The file validates that an NVIDIA GPU (a GTX 1060, sm_61) is discovered through the CUDA SYCL
backend and mapped to a NUMA node by hwloc. The tests skip when there is no CUDA device.

## test_registration_index.cpp

The file covers `Node::registration_index` and the grouping of the tile levels of
`Graph::add_reduce()` under one index. The design is sections D and F5 of
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`.

### `AddReduceGroupSurvivesRebuild`

The group of `add_reduce()` must survive a rebuild onto a fresh `BackingGraph`, whose registration
counter restarts at 0.

The test adds a plain node after `add_reduce()`, which marks the graph dirty, and then runs twice,
so that `build_plan()` replays every spec into a second `BackingGraph`. The levels of the reduce
must still share one index, distinct from the index of every other node, and the indices of all
nodes must be dense from 0 to n-1 in registration order.

### `LateBoundAddReduceGroupsTileLevelsUnderOneIndex`

The `LateCount` overload of `add_reduce()` groups its tile levels, and the trailing identity write
for a zero count, under one index.

The test mirrors `AddReduceGroupsTileLevelsUnderOneIndex`, but the count is bound through a
`LateCount` host provider planned for a capacity of at least three tile levels. That exercises the
registration token of the dynamic emitter and not the fixed-count path.

## test_topology.cpp

The file covers hardware discovery and NUMA mapping, edge cases included. The invalid-NUMA fallbacks
check that the runtime degrades without failing on bad input.

## test_topology_abstraction.cpp

The file validates the injectable topology abstraction, `ITopologyProvider` and
`INumaDistanceModel`. It exercises multi-NUMA logic deterministically on single-node hardware, which
the real hwloc path cannot do on a machine with one socket.
