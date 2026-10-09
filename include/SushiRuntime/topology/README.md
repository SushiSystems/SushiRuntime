# topology

Hardware discovery, NUMA mapping and thread affinity.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## affinity.hpp

### pin_current_thread_to_core

pin_current_thread_to_core() uses the OS affinity API (SetThreadAffinityMask on Windows,
pthread_setaffinity_np on POSIX) to bind the thread so the scheduler never migrates it to a
different core.

## hw_discovery.hpp

### require_fp64

A kernel using `double` on a device without `sycl::aspect::fp64` fails at launch with a
driver-shaped error that names neither the device nor the reason. Setting require_fp64 moves the
failure to discovery, where the offending device is simply not selected and the choice is logged. It
is off by default: a single-precision workload has no reason to lose a device over it.

## i_topology_provider.hpp

### ITopologyProvider

RuntimeContext depends on this interface instead of the concrete hwloc and SYCL machinery, which
makes topology substitutable at construction time. HwlocTopologyProvider does real bare-metal
discovery. RemappingTopologyProvider puts a synthetic NUMA layout over real devices, so multi-node
logic runs on single-node hardware.

### cpu_core_to_numa

The result is indexed by the same logical-core id that thread pinning uses
(pin_current_thread_to_core), so a worker bound to core c has NUMA node result[c]. With the default
settings that is worker i on core i, and under a reserved-core configuration it is worker i on core
(first_core + i). The map enables NUMA-aware, hierarchical work stealing in the scheduler.

The default implementation returns an empty vector, and callers then fall back to a single flat tier
(NUMA-blind stealing). A provider overrides it to supply a real or synthetic map.

## matrix_distance_model.hpp

### MatrixDistanceModel

A MatrixDistanceModel holds an explicit N x N SLIT-style matrix to validate multi-node distance and
placement logic deterministically on any machine.

This example has two sockets and a remote hop that costs 21:

```cpp
MatrixDistanceModel model{{{10, 21},
                           {21, 10}}};
```

## topology_view.hpp

### TopologyView

A TopologyView holds two things: the core-to-NUMA map, copied from the provider in the constructor,
and a shared pointer to the distance model. It does not keep the provider, so the map is the one the
provider reported at construction and later changes in the provider do not reach it. The constructor
throws `std::invalid_argument` for a null distance model.

RuntimeContext builds the view from the provider and the distance model it was given, or from the
hwloc defaults. The scheduler reads it once, when it builds the steal victim order of each worker:
`core_to_numa()` gives the node of the core a worker is pinned to, and `numa_distance()` sorts the
other workers by distance from that node.

`numa_distance()` forwards both ids to the model without checking them, so an id the model does not
know gets the model's own answer. MatrixDistanceModel and HwlocDistanceModel both answer 10 when the
two ids are equal and 21 otherwise.

The view has no mutating member. Any thread may read it without a lock, provided the distance model
is itself safe to read concurrently; both models in this module are, because they only read state
set in their constructors.

The header forward-declares ITopologyProvider and INumaDistanceModel. A reader of the view therefore
does not include `i_topology_provider.hpp`, and through it the device discovery types.
