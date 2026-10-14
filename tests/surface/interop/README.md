# interop tests

The tests of the `interop` module, mirroring `modules/surface/interop/`.
`tests/surface/interop/CMakeLists.txt` builds them into `sr_interop_tests`. `sr test` runs them with
the rest; `sr run sr_interop_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_interop_capabilities.cpp

The file covers the default values of the interop vocabulary.

### `ADefaultDescriptorStatesNoHandleKind`

A default `ExternalMemoryDescriptor` has the kind `Unspecified`, a handle of 0, and an allocation size
and an offset of 0.

## test_device_uuid.cpp

The file tests DeviceUUID: sixteen zero bytes by default, equal when every byte is.

| Test | What it proves |
| --- | --- |
| `EqualOnlyWhenEveryByteIs` | Two UUIDs are equal only when every byte is. |

## test_external_memory.cpp

The file tests ExternalMemoryDescriptor: an unspecified handle of no bytes by default.

| Test | What it proves |
| --- | --- |
| `DefaultsNameNoHandle` | Checks the defaults of a descriptor and the three handle kinds. |
