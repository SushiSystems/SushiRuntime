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

### `CountAndInventoryFollowTheDescriptions`

Small, on a host set. Two rows: a CPU and a GPU, and two descriptions that carry the same stale index.
The set copies the inventory and numbers the devices by position.

### `IsBuiltOnceWithThePropertiesAsked`

One row per flavour. Each row names whether the plain queue itself serves that flavour when the plain
queue is in order and when it is out of order.

### `ABadLookupStopsNamingItsCause`

One death test with a row per call: a bad index through each accessor, the index equal to the count,
and the lookups on an empty set.

### The in-memory set

`InMemorySet` builds a set over an `InMemoryQueueFactory`; the factory's `builds()` shows each
queue the set asked for. The `Small_DeviceQueueSet` cases build no queue: construction asks for
none, `TheDeviceLineNamesTheBackendAndTheDriver` reads the constructor's device line through a
`LogCapture` at `Info`, and an index past the set has no profiling queue. Every case that asks
for a queue is `Medium_DeviceQueueSetBuild`, the queue-building path: the plain queue built once
on first use, the lazy flavours in the plain queue's context, the in-order rule as the factory
sees it, the profiling flag, and the `DEBUG` line of a first build. The in-order rule itself is
`Small_QueueFlavourServed`, rows over `served_flavour()` in `test_queue_flavour.cpp`.

### `AnInvalidNUMAIDInADescriptionBecomesNodeZero`

Small, on a host set. The wiring check of the NUMA routes: the set writes the node the route table places each device on
into its inventory, and resolves node 0 through the table. The routing decisions themselves are
tested small in `test_numa_route_table.cpp`.

## test_queue_factory.cpp

Small. `properties_of()` asks for no property for the plain flavour, and for exactly the
properties a flavour names otherwise.

## test_in_memory_queue_factory.cpp and test_sycl_queue_factory.cpp

`Medium_InMemoryQueueFactory` runs `QueueFactoryContract` on an in-memory factory of two devices.
It is medium because `make()` returns a `sycl::queue`, which the factory builds on the CPU device;
`sycl::queue` stays the runtime's execution currency (decision 64). `Small_InMemoryQueueFactory`
checks the factory's record of builds and its in-order list. `Medium_SYCLQueueFactory` runs the same
contract on the device of the program's queue, the boundary the in-memory factory stands in for.

## test_numa_route_table.cpp

The file tests NUMARouteTable over NUMA ids given as values, with no device.

| Test | What it proves |
| --- | --- |
| `Small_NUMARouteTableLookup.ResolvesTheFirstDeviceOfTheNodeOrDeviceZero` | Rows over devices on nodes 2, 0 and 2: node 0 resolves to device 1 and node 2 to device 0; a node with no device, an id past the table and a negative id resolve to device 0. |
| `Small_NUMARouteTablePlacement.PlacesAnInvalidIDOnNodeZero` | Rows: an id in range stays, and -1 and 4096 are placed on node 0. |
| `Small_NUMARouteTable.DevicesOnAnInvalidIDServeNodeZero` | Devices that report -1 and 4096 serve node 0, and the device on node 1 serves node 1. |
| `Small_NUMARouteTable.ANamedDeviceWinsOverTheRotation` | `device_or_next()` returns the device it is given, whatever node the rotation would run over. |
| `Small_NUMARouteTable.NamesTheNodesThatHoldNoDevice` | Over devices on nodes 3 and 0 the table has four nodes, two populated, and names nodes 1 and 2 as empty. |
| `Small_NUMARouteTableRotation.HandsOutTheDevicesOfTheNodeInTurn` | Over devices on nodes 0, 2 and 2, four calls for node 2 on one cursor hand out devices 1, 2, 1, 2. |
| `Small_NUMARouteTableRotation.ANodeWithNoDeviceRotatesOverAll` | Four calls for device-less node 1 hand out 0, 1, 2, 0 on the cursor's fallback position. |
| `Small_NUMARouteTableRotation.TwoCursorsKeepTheirOwnPositions` | A call on one cursor does not move the other. |

## test_queue_flavour.cpp

The file tests QueueFlavour: equality over both fields.

| Test | What it proves |
| --- | --- |
| `EqualityComparesBothFields` | Two flavours are equal only when both fields are. |
