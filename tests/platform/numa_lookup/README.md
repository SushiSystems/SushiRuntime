# numa_lookup tests

The tests of the `numa_lookup` module, mirroring `modules/platform/numa_lookup/`.
`tests/platform/numa_lookup/CMakeLists.txt` builds them into `sr_numa_lookup_test_objects`, linked into `sr_platform_tests`. `sr test` runs
them with the rest; `sr run sr_platform_tests -- --gtest_filter='<pattern>'` runs one program
alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_numa_lookup.cpp

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
