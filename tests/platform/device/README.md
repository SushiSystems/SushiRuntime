# device tests

The tests of the `device` module, mirroring `modules/platform/device/`.
`tests/platform/device/CMakeLists.txt` builds them into `sr_device_test_objects`, linked into `sr_platform_tests`. `sr test` runs them with
the rest; `sr run sr_platform_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

Sizes and the shared fixture base are described in [the tests README](../../README.md#sizes). The medium tests here skip where SYCL reports no device.

## compile_device_descriptor.cpp and compile_device_log_category.cpp

Each file includes one header alone. See [the scheduler tests](../../engine/scheduler/README.md)
for why such a file stands in for a test.

## test_device_queue_set.cpp

The file tests DeviceQueueSet's inventory, queue table, NUMA routing and index checks. The inventory,
flavour, profiling, rotation and stop tests run as rows; a loop over the devices sits in a named
helper that returns the first defect, and the test asserts that it is empty.

### `CountAndInventoryFollowTheDescriptors`

Two rows: every device SYCL reports, and two descriptors that carry the same stale index. The set
copies the inventory and numbers the devices by position.

### `IsBuiltOnceWithThePropertiesAsked`

One row per flavour. Each row names whether the plain queue itself serves that flavour when the plain
queue is in order and when it is out of order.

### `HandsOutTheDevicesOfTheNodeInTurn`

The rotation cursor is per thread, so each row reads it on a fresh thread. The cursor is process-wide
state of the module, so injecting it is a change to the production code; that change is not made here.

### `ABadLookupStopsNamingItsCause`

One death test with a row per call: a bad index through each accessor, the index equal to the count,
and the lookups on an empty set.

### `TheDeviceLineNamesTheBackend` and `TheDeviceLineNamesTheDriver`

A `LogCapture` at `Info` sees the constructor's device line, and each test asserts one word of it.

### `TheFirstBuildOfALazyQueueFlavourIsLogged` and `ASecondRequestForABuiltFlavourLogsNothing`

A `LogCapture` at `Debug` starts after construction. The first request for an in-order profiling
queue logs one `DEBUG` with the flavour and the build time; a second request logs nothing.

### The NUMA routing tests

`DeviceForNUMA*`, `AnInvalidNUMAIDInADescriptorBecomesNodeZero` and `QueueOrNUMALocalReturnsTheNamedDevice`
decide on values but build their set from SYCL devices, so they stay medium until the module builds a
route table from NUMA ids.

## test_queue_flavour.cpp

The file tests QueueFlavour: equality over both fields.

| Test | What it proves |
| --- | --- |
| `EqualityComparesBothFields` | Two flavours are equal only when both fields are. |
