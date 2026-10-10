# topology tests

The tests of the `topology` module, built into one program, `sr_topology_tests`, which
`modules/platform/topology/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_topology_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_affinity.cpp

Thread-to-core pinning is best-effort: on failure it logs a warning and does not throw. The tests
assert that no call throws. They pin from a worker thread, so the affinity of the test runner is
left untouched.

## test_hw_discovery.cpp

The file covers what `HardwareDiscovery` and `HwlocTopologyProvider` return for a filter. It
enumerates the SYCL devices of the machine it runs on.

### `AFilterThatAdmitsNoClassYieldsNoDeviceAndDoesNotThrow`

A filter that excludes every device class gives an empty vector from both `probe_all_devices` and
`enumerate`. In this module an empty result is a value: `DeviceQueueSet` accepts it and stays empty.
The failure for it, `SushiException` with `Errc::unsupported`, is raised by `RuntimeContext`, and
`tests/integration/test_error_types.cpp` covers it.

Discovery itself throws `SushiException` with `Errc::unsupported` only when SYCL enumeration
throws. No test reaches that path, because a test cannot make `sycl::device::get_devices` fail.

## test_topology_view.cpp

### `refusal_of`

`refusal_of` runs a constructor call and returns the `Core::Error` of the `SushiException` it
throws. It records a test failure when the call throws nothing.

### `NullDistanceModelIsRejected`, `NullInnerProviderIsRejected`

A null model or a null inner provider throws `SushiException` with `Errc::invalid_access`, and the
message names the argument.

### `RaggedDistanceMatrixIsRejectedWithItsRow`

The matrix has three rows and its second row has two entries. The message carries the row index,
the length of that row and the number of rows.
