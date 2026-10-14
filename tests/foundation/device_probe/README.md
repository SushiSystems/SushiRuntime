# device_probe tests

The tests of the `device_probe` module, mirroring `modules/foundation/device_probe/`.
`tests/foundation/device_probe/CMakeLists.txt` builds them into `sr_device_probe_tests`. `sr test`
runs them with the rest; `sr run sr_device_probe_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_device_probe.cpp

The file tests has_any_sycl_device() against the device list SYCL reports.

| Test | What it proves |
| --- | --- |
| `AgreesWithTheDeviceList` | The probe agrees with the device list. |
