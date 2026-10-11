# interop tests

The tests of the `interop` module, built into one program, `sr_interop_tests`, which
`modules/surface/interop/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_interop_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_interop_vocabulary.cpp

The file covers the default values of the interop vocabulary.

### `ADefaultDescriptorStatesNoHandleKind`

A default `ExternalMemoryDescriptor` has the kind `Unspecified`, a handle of 0, and an allocation size
and an offset of 0.
