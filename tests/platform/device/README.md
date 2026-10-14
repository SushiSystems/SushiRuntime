# device tests

The tests of the `device` module, mirroring `modules/platform/device/`.
`tests/platform/device/CMakeLists.txt` builds them into `sr_device_tests`. `sr test` runs them with
the rest; `sr run sr_device_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

No test file of this module has notes yet.
