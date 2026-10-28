# topology tests

The tests of the `topology` module, mirroring `modules/platform/topology/`.
`tests/platform/topology/CMakeLists.txt` builds them into `sr_topology_test_objects`, linked into `sr_platform_tests`. `sr test` runs them
with the rest; `sr run sr_platform_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_affinity.cpp

A pin request does not throw. The tests pin from a fresh worker thread, so the affinity of the test
runner is left untouched. Three tests read the thread's affinity from the OS before and after the
request. A request for core -1, or for core 100000, leaves that affinity unchanged. A request for
core 0 leaves core 0 as the only core of the thread; the test is skipped when the affinity before the
request does not list core 0. Three further tests pin to core 0 or to core 100000 and assert that no
call throws. `PinIsRepeatable` makes 16 requests for core 0 on one thread.

## test_host_identity.cpp

`host_name()` returns the same name twice and the name holds no null byte. `host_operating_system()`
names the system the test was compiled for: Windows, Linux, or Unknown elsewhere.

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

### `LogicalIDsAreSequentialAndEveryDeviceIsNamed`

Discovery over every device class finds at least one device, numbers the devices 0, 1, 2 in order
and gives each a name.

## test_matrix_distance_model.cpp

| Test | What it proves |
| --- | --- |
| `MatrixDistanceModelObeysACPIConvention` | A two-node matrix gives 10 on the diagonal and the stated distance off it. |
| `MatrixDistanceModelSupportsAsymmetricFabric` | A three-node matrix whose rows differ answers each direction with its own value. |

## test_remapping_topology_provider.cpp

| Test | What it proves |
| --- | --- |
| `RemappingProviderSplitsRealDevicesAcrossFakeNodes` | The real devices hwloc reports, CPU and GPU, all land on a node within the four synthetic ones. |

## test_topology_view.cpp

### `ANullDistanceModelStops`, `ANullInnerProviderStops`

A null model or a null inner provider stops the process, and the line names the argument.

### `ARaggedDistanceMatrixStopsWithItsRow`

The matrix has three rows and its second row has two entries. The line carries the row index,
the length of that row and the number of rows.

### `Small_HwDiscovery.LogsAPciAddressThatDoesNotParse`

`parse_pci_address` reads `0000:01:00.0` into its four fields and logs nothing. A string that holds no
address returns nothing and logs one `DEBUG` in the `topology` module with the string and the device name.

## test_hwloc_topology_provider.cpp

The file tests HwlocTopologyProvider and HwlocDistanceModel against the machine the test runs on.

| Test | What it proves |
| --- | --- |
| `EveryDeviceSitsOnAKnownNode` | Every device found sits on a node the distance model knows. |
| `TheCoreMapAndTheDistancesAgree` | The core map names a node for every core, and the model is local-cheapest. |

## test_numa_mapper.cpp

The file tests NUMAMapper: the local distance and the nodes it maps on this machine.

| Test | What it proves |
| --- | --- |
| `ALocalNodeIsTenAway` | A node is 10 from itself, the ACPI local distance. |
| `ARemoteNodeIsFartherThanALocalOne` | A remote node is farther than the local distance of 10. |
| `MappingPlacesEveryDevice` | Mapping leaves every discovered device on a node, and an empty list alone. |

## test_topology_provider.cpp

The file tests the TopologyProvider and NUMADistanceModel contracts over a fake of each.

| Test | What it proves |
| --- | --- |
| `AProviderAnswersThroughTheInterface` | Reports two devices on two nodes and keeps the default core map. */ class TwoNodeProvider final : public TopologyProvider { public: [[nodiscard]] Result<std::vector<HardwareNode>> enumerate(const DeviceFilter&) override { std::vector<HardwareNode> nodes(2); nodes[0].logical_id = 0; nodes[0].numa_node_id = 0; nodes[1].logical_id = 1; nodes[1].numa_node_id = 1; return nodes; } }; /** @brief Answers 10 on the diagonal and 20 off it, over two nodes. */ class TwoNodeDistances final : public NUMADistanceModel { public: int distance(int numa_a, int numa_b) const override { return numa_a == numa_b ? 10 : 20; } int node_count() const override { return 2; } }; } /** @brief Tests that a provider is called through the interface, and the core map defaults empty. |
| `ADistanceModelAnswersThroughTheInterface` | Checks a distance model through the interface: local is the cheapest. |
