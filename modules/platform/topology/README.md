# topology

Hardware discovery, NUMA mapping and thread affinity.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## affinity.hpp

### pin_current_thread_to_core

pin_current_thread_to_core() uses the OS affinity API (SetThreadGroupAffinity on Windows,
pthread_setaffinity_np on POSIX) to bind the thread so the scheduler never migrates it to a
different core. The body is in `source/affinity.cpp`, so the header pulls in no OS header.

The core id counts logical cores from 0. On Windows it runs through the processor groups in
order, so a core above 63 can be named. An id below 0, or at or above the number of logical cores
the OS reports (`GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)` on Windows; the smaller of
`CPU_SETSIZE` and `sysconf(_SC_NPROCESSORS_CONF)` on POSIX), logs one warning and leaves the
thread's affinity as it was. So does an id the OS refuses.

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
throws `SushiException` with `Errc::invalid_access` for a null distance model.

RuntimeContext builds the view from the provider and the distance model it was given, or from the
hwloc defaults. The scheduler reads it once, when it builds the steal victim order of each worker:
`core_to_numa()` gives the node of the core a worker is pinned to, and `numa_distance()` sorts the
other workers by distance from that node.

`numa_distance()` forwards both ids to the model without checking them, so an id the model does not
know gets the model's own answer. HwlocDistanceModel answers 10 for two equal ids, whatever its
matrix holds, and 21 for two different ids when either is missing from its matrix.
MatrixDistanceModel answers 10 for equal ids and 21 otherwise only when an id is outside the
matrix; for ids inside it, equal ones included, it returns the matrix entry.

The view has no mutating member. Any thread may read it without a lock, provided the distance model
is itself safe to read concurrently; both models in this module are, because they only read state
set in their constructors.

The header forward-declares ITopologyProvider and INUMADistanceModel. A reader of the view therefore
does not include `i_topology_provider.hpp`, and through it the device discovery types.

## Implementation

Hardware discovery and NUMA mapping. The headers are in `include/SushiRuntime/topology/`.
This page records why the sources behave as they do; the source comments state the invariant
and point here.

### hw_discovery.cpp

#### One CPU per NUMA node

Some backends expose the same physical CPU twice. Under AdaptiveCpp the host CPU appears
both through the OpenCL backend and as the native "OpenMP host device", which also reports a
bogus `global_mem_size`. Discovery keeps at most one CPU per NUMA node, so the scheduler
does not build duplicate queues for one processor. The second CPU seen on a NUMA node is
logged and dropped, and its logical id is given back.

#### One GPU or accelerator per PCI address

A GPU that two backends expose is enumerated twice, and every entry of the device list gets its
own queues, so the second entry would give one physical device two queue sets. Discovery keeps
the first GPU or accelerator enumerated at a resolved PCI address; a later one at the same
address is logged and dropped, and its logical id is given back. A device with no resolved PCI
address is never dropped, because nothing tells it apart from a second physical device.

#### fp64 is checked at discovery

`require_fp64` is checked while devices are enumerated, not at kernel launch. A
double-precision kernel on a device without fp64 fails inside the driver with a message that
names neither the device nor the aspect. Discovery names the device it dropped and why.

#### PCI address resolution

The address of a GPU or accelerator is looked up in three steps.

1. The Intel device-info extension, which covers Intel GPUs and the Codeplay NVIDIA backend.
   The code is guarded on `SYCL_EXT_INTEL_DEVICE_INFO >= 5`, because AdaptiveCpp and other
   SYCL implementations do not expose the aspect; there discovery goes straight to the
   fallbacks.
2. A regex scan of the vendor string, which catches some Codeplay and NVIDIA combinations.
3. An hwloc OS-device scan that matches the GPU name against hwloc's PCI tree and walks up
   to the PCI object for the bus address. This works for NVIDIA GPUs exposed through the
   CUDA backend; the comment this page replaces gives "GTX 1060 = GeForce GTX 1060" as the
   observed match.

When none of them yields an address, NUMA affinity is estimated and a warning says so.

### hwloc_topology_provider.cpp

`cpu_core_to_numa()` sizes its map by the highest OS cpu index, so `result[i]` is indexed by
the same logical core id that thread pinning uses.

`HwlocDistanceModel` loads and caches the NUMA distance matrix in its constructor. It is the
single place that touches the hwloc SLIT table, so the expensive topology load happens once
per model instance. Without a SLIT table the model answers with the ACPI defaults: 10 for
the local node and 21 for one remote hop.

### numa_mapper.cpp

hwloc 2.x does not include PCI and I/O devices by default.
`HWLOC_TYPE_FILTER_KEEP_ALL` is required for `hwloc_get_pcidev_by_busid()` to find the GPU.

A GPU or accelerator with a PCI address takes the NUMA node local to its PCI device, and node 0
when hwloc reports no such node or does not know the PCI device. A GPU without a PCI address
takes node 0. Every other device takes the NUMA node covering the process's CPU binding.

### numa_lookup.cpp

In hwloc 2 a NUMA node is a memory child, not a link in an object's parent chain, so
`hwloc_get_ancestor_obj_by_type(..., HWLOC_OBJ_NUMANODE, ...)` finds none. `local_numa_os_index()`
walks up from the object to the first one with a non-empty `nodeset` and returns the OS index of
the lowest node in it. A PU or core has its own nodeset; a PCI device has none and takes its
first non-I/O ancestor's. The three lookups of this module (`hw_discovery.cpp`,
`hwloc_topology_provider.cpp`, `numa_mapper.cpp`) go through it, and each keeps its own fallback
for an empty answer.

### remapping_topology_provider.cpp

The provider spreads the real devices across synthetic NUMA nodes round-robin. The devices
stay real, so queues and kernels still work; only `numa_node_id` is faked.

`cpu_core_to_numa()` overlays the same round-robin layout on the real cores, so hierarchical
work stealing sees several NUMA nodes on single-node hardware. The index is the logical core
id and stays aligned with thread pinning.
