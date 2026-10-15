# memory_exchange tests

The tests of the `memory_exchange` module, mirroring `modules/surface/memory_exchange/`.
`tests/surface/memory_exchange/CMakeLists.txt` builds them into `sr_memory_exchange_tests`. `sr
test` runs them with the rest; `sr run sr_memory_exchange_tests -- --gtest_filter='<pattern>'` runs
one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_memory_exchange.cpp

The file tests the memory exchange: capabilities, a host export, and an import that names no
mechanism.

| Test | What it proves |
| --- | --- |
| `CapabilitiesOfDeviceZero` | Device 0 reports its capabilities. |
| `AHostExportGivesTheSizeAsked` | A host export gives an allocation of the size asked when the device can export. |
| `AnImportWithNoKindIsRefused` | An import naming no handle kind is refused with InvalidAccess; skipped on a device that cannot import, which refuses first with Unsupported. |
