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
