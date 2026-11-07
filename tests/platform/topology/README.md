# topology tests

The tests of the `topology` module, mirroring `modules/platform/topology/`.
`tests/platform/topology/CMakeLists.txt` builds them into `sr_topology_test_objects`, linked into `sr_platform_tests`. `sr test` runs them
with the rest; `sr run sr_platform_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.
Sizes are described in [the tests README](../../README.md#sizes). A test that reads the machine's
hwloc topology or its SYCL devices is medium.

## compile_topology_log_category.cpp

The file includes one header alone. See [the scheduler tests](../../engine/scheduler/README.md)
for why such a file stands in for a test.

## test_affinity.cpp

A pin request does not throw: a throw in a worker thread ends the process, so every test below would
fail on one. The tests pin from a fresh worker thread, so the affinity of the test runner is left
untouched, and read the thread's affinity from the OS before and after the request.

### `ACoreOutOfRangeLeavesAffinityUnchanged`

Two rows: core -1 and core 100000. Neither changes the affinity of the thread.

### `CoreZeroBecomesTheOnlyCoreOfTheThread` and `PinIsRepeatable`

A request for core 0 leaves core 0 as the only core of the thread. `PinIsRepeatable` makes 16 requests
for core 0 on one thread and asserts the same. Both skip when the affinity before the request does not
list core 0.

## test_host_identity.cpp

`HostNameIsStable` and `HostNameHoldsNoTerminator` state two promises of `host_name()`: the same name
twice, and no null byte. `host_operating_system()` names the system the test was compiled for:
Windows, Linux, or Unknown elsewhere.

## test_host_topology.cpp

| Test | What it proves |
| --- | --- |
| `MapsItsCoreToNodeZeroAndHasNoHardwareNode` | The host provider maps core 0 to node 0 and enumerates no node. |
| `HasOneNodeAtTheLocalDistance` | The host distance model holds one node at distance 10. |
| `NamesEachDeviceForItsIndexAsACPU` | Each host device is a CPU named `host device <i>`, at its index. |

## test_hw_discovery.cpp

The file covers what `HardwareDiscovery` returns for a filter and what `parse_pci_address` reads.

### The small discovery tests

`Small_HardwareDiscovery` runs discovery over an `InMemoryDeviceEnumerator` and an
`InMemoryNUMAReader`: a filter that admits no class gives an empty vector and no error; the
GPU-only filter keeps the GPUs of a mixed list; the kept devices are numbered 0, 1, 2 with no gap
where a duplicate CPU was dropped; a CPU takes the reader's node, or -1; a second device at one
PCI address is dropped; a GPU whose driver names no address takes the one the reader lists; a
device without fp64 is dropped when the filter requires it; and a failed enumeration is
`ErrorCode::Unsupported` naming the runtime's text.

### `NoTwoNodesShareAPCIAddress` and `EveryDeviceIsNamed`

Medium, over the SYCL enumerator and the hwloc reader of the machine. The first asserts that no
PCI address the machine's backends report repeats after discovery, and the second that every
device found is named. The dropping rules themselves are the small tests.

### The PCI address tests

`parse_pci_address` reads `0001:02:03.4` into its four fields, and a valid address writes no log record.
A string that holds no address returns nothing, and logs one `DEBUG` in the `topology` module with the
string and the device name.

## The adapter tests

`compile_device_enumerator.cpp` and `compile_numa_reader.cpp` compile each port header alone.
`Small_InMemoryDeviceEnumerator` and `Small_InMemoryNUMAReader` run `DeviceEnumeratorContract`
and `NUMAReaderContract` on the in-memory adapters; `Medium_SYCLDeviceEnumerator` and
`Medium_HwlocNUMAReader` run them on the machine, the boundary the in-memory adapters stand in
for. The files are `test_in_memory_device_enumerator.cpp`, `test_sycl_device_enumerator.cpp`,
`test_in_memory_numa_reader.cpp` and `test_hwloc_numa_reader.cpp`.

## test_in_memory_topology_provider.cpp

| Test | What it proves |
| --- | --- |
| `ReturnsTheNodesItWasGiven` | `enumerate()` returns the nodes the provider was built with. |
| `ReturnsTheCoreMapItWasGiven` | `cpu_core_to_numa()` returns the core map the provider was built with. |
| `CoreMapIsEmptyWhenNoneIsGiven` | The core map is empty when the provider is built without one. |
| `FailingFormReturnsItsError` | A provider built by `failing()` returns the error it was given. |

## test_matrix_distance_model.cpp

### `ReadsTheSourceRow`

A distance is the matrix entry of the row of its source node. The rows are a two-node matrix with 10
on the diagonal and 21 off it, read in both directions, a two-node matrix whose two directions differ,
and a three-node matrix with a cheap hop and a two-hop distance. Each row also asserts the node count.

## test_remapping_topology_provider.cpp

### `SplitsDevicesAcrossFakeNodes`

The inner provider is an `InMemoryTopologyProvider` whose devices all sit on node 0. The rows are six
devices over four synthetic nodes, three over two, and a node count below one, which becomes one. The
devices are spread round-robin.

## test_topology_view.cpp

### `CoreMapMatchesSyntheticProvider`

The view reports the core map of an `InMemoryTopologyProvider` built with that map.

### `ANullDistanceModelStops`, `ANullInnerProviderStops`

A null model or a null inner provider stops the process, and the line names the argument.

### `ARaggedDistanceMatrixStopsWithItsRow`

The matrix has three rows and its second row has two entries. The line carries the row index,
the length of that row and the number of rows.

## test_hwloc_topology_provider.cpp

The file tests HwlocTopologyProvider and HwlocDistanceModel against the machine the test runs on. Each
test is medium, since it reads the machine's devices or its hwloc topology, and a helper returns the
first defect.

| Test | What it proves |
| --- | --- |
| `EveryDeviceSitsOnAKnownNode` | Every device found sits on a node the distance model knows. |
| `TheCoreMapNamesKnownNodes` | The core map names a node the distance model knows for every core. |
| `TheDistancesMakeLocalNoFartherThanRemote` | No node is farther from itself than from another node. |

## test_numa_mapper.cpp

The file tests NUMAMapper: the local distance and the nodes it maps on this machine.

| Test | What it proves |
| --- | --- |
| `Small_NUMAMapper.ALocalNodeIsTenAway` | A node is 10 from itself, the ACPI local distance. |
| `Small_NUMAMapper.ARemoteNodeIsFartherThanALocalOne` | A remote node is farther than the local distance of 10. |
| `Medium_NUMAMapper.MappingAnEmptyListLeavesItAlone` | Mapping an empty list leaves it empty; the mapper loads hwloc for any list. |
| `Medium_NUMAMapper.MappingPlacesEveryDevice` | Mapping leaves every discovered device on a node. |

## test_topology_provider.cpp

The file tests the TopologyProvider and NUMADistanceModel contracts over the in-memory form of each:
`InMemoryTopologyProvider` and `MatrixDistanceModel`.

| Test | What it proves |
| --- | --- |
| `AProviderAnswersThroughTheInterface` | A provider is called through the interface, and the core map defaults empty. |
| `ADistanceModelAnswersThroughTheInterface` | A distance model answers through the interface: local is the cheapest. |
