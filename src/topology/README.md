# topology

Hardware discovery and NUMA mapping. The headers are in `include/SushiRuntime/topology/`.
This page records why the sources behave as they do; the source comments state the invariant
and point here.

## hw_discovery.cpp

### One CPU per NUMA node

Some backends expose the same physical CPU twice. Under AdaptiveCpp the host CPU appears
both through the OpenCL backend and as the native "OpenMP host device", which also reports a
bogus `global_mem_size`. Discovery keeps at most one CPU per NUMA node, so the scheduler
does not build duplicate queues for one processor. The second CPU seen on a NUMA node is
logged and dropped, and its logical id is given back.

### fp64 is checked at discovery

`require_fp64` is checked while devices are enumerated, not at kernel launch. A
double-precision kernel on a device without fp64 fails inside the driver with a message that
names neither the device nor the aspect. Discovery names the device it dropped and why.

### PCI address resolution

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

## hwloc_topology_provider.cpp

`cpu_core_to_numa()` sizes its map by the highest OS cpu index, so `result[i]` is indexed by
the same logical core id that thread pinning uses.

`HwlocDistanceModel` loads and caches the NUMA distance matrix in its constructor. It is the
single place that touches the hwloc SLIT table, so the expensive topology load happens once
per model instance. Without a SLIT table the model answers with the ACPI defaults: 10 for
the local node and 21 for one remote hop.

## numa_mapper.cpp

hwloc 2.x does not include PCI and I/O devices by default.
`HWLOC_TYPE_FILTER_KEEP_ALL` is required for `hwloc_get_pcidev_by_busid()` to find the GPU.

A GPU takes the NUMA node that is an ancestor of its PCI device, and node 0 when hwloc knows
no such ancestor or does not know the PCI device. A CPU device takes the NUMA node covering
the process's CPU binding.

## remapping_topology_provider.cpp

The provider spreads the real devices across synthetic NUMA nodes round-robin. The devices
stay real, so queues and kernels still work; only `numa_node_id` is faked.

`cpu_core_to_numa()` overlays the same round-robin layout on the real cores, so hierarchical
work stealing sees several NUMA nodes on single-node hardware. The index is the logical core
id and stays aligned with thread pinning.
