# device tests

The tests of the `device` module, mirroring `modules/platform/device/`.
`tests/platform/device/CMakeLists.txt` builds them into `sr_device_tests`. `sr test` runs them with
the rest; `sr run sr_device_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

No test file of this module has notes yet.

## test_device_descriptor.cpp

The file tests DeviceDescriptor: it carries a SYCL device and the description of it.

| Test | What it proves |
| --- | --- |
| `KeepsTheDeviceAndItsDescription` | A descriptor keeps the device and the description it was given. |

## test_device_queue_set.cpp

The file tests DeviceQueueSet's inventory, queue table, NUMA routing and index checks.


## test_queue_flavour.cpp

The file tests QueueFlavour: the plain default and equality over both fields.

| Test | What it proves |
| --- | --- |
| `TheDefaultIsThePlainQueue` | The default is the plain queue: out of order, no profiling. |
| `EqualityComparesBothFields` | Two flavours are equal only when both fields are. |
