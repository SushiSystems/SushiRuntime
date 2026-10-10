# device tests

The tests of the `device` module, built into one program, `sr_device_tests`, which
`modules/platform/device/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_device_tests -- --gtest_filter='<pattern>'` runs it alone.

No test file in this folder has notes yet. A section, when one is written, is named after its test
file.
