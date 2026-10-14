# topology tests

The tests of the `topology` module, mirroring `modules/platform/topology/`.
`tests/platform/topology/CMakeLists.txt` builds them into `sr_topology_tests`. `sr test` runs them
with the rest; `sr run sr_topology_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_affinity.cpp

A pin request does not throw. The tests pin from a fresh worker thread, so the affinity of the test
runner is left untouched. Three tests read the thread's affinity from the OS before and after the
request. A request for core -1, or for core 100000, leaves that affinity unchanged. A request for
core 0 leaves core 0 as the only core of the thread; the test is skipped when the affinity before the
request does not list core 0. Three further tests pin to core 0 or to core 100000 and assert that no
call throws. `PinIsRepeatable` makes 16 requests for core 0 on one thread.

## test_hw_discovery.cpp

The file covers what `HardwareDiscovery` and `HwlocTopologyProvider` return for a filter. It
enumerates the SYCL devices of the machine it runs on.

### `AFilterThatAdmitsNoClassYieldsNoDeviceAndDoesNotThrow`

A filter that excludes every device class gives an empty vector from both `probe_all_devices` and
`enumerate`. In this module an empty result is a value: `DeviceQueueSet` accepts it and stays empty.
The failure for it, `ErrorCode::Unsupported`, is returned by `RuntimeContext::create()`, and
`tests/surface/api/runtime/test_runtime_config.cpp` covers it.

Discovery itself returns `ErrorCode::Unsupported` only when SYCL enumeration
throws. No test reaches that path, because a test cannot make `sycl::device::get_devices` fail.

### `NoTwoNodesShareAPCIAddress`

The test probes every device without a filter. For each node that has a PCI address, it asserts that
the domain, bus, device and function of that address have not been returned for an earlier node.
Nodes without a PCI address are skipped.

## test_numa_mapper.cpp

The file covers `local_numa_os_index`, which returns the OS index of the NUMA node that a hwloc object
belongs to. Each test loads a synthetic machine described as two NUMA nodes, each with two cores of one
PU, and destroys the topology on scope exit.

### `SyntheticTopology`

`SyntheticTopology` loads a hwloc synthetic description and destroys the topology in its destructor.
`get()` returns null when hwloc refuses the description. Copy and move are deleted.

### `EveryPuOfATwoNodeMachineMapsToItsOwnNode`

The test asserts that the machine has two NUMA nodes and four PUs. For each PU, `local_numa_os_index`
returns the OS index of the node that holds the PU, which is the PU's index divided by two.

### `ANUMANodeIsLocalToItself`

For the second NUMA node, `local_numa_os_index` returns that node's own OS index.

### `ANullObjectHasNoNode`

`local_numa_os_index` returns no value for a null object.

## test_topology_view.cpp

### `ANullDistanceModelStops`, `ANullInnerProviderStops`

A null model or a null inner provider stops the process, and the line names the argument.

### `ARaggedDistanceMatrixStopsWithItsRow`

The matrix has three rows and its second row has two entries. The line carries the row index,
the length of that row and the number of rows.
