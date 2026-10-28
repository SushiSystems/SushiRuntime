# interop tests

The tests of the `interop` module, mirroring `modules/surface/interop/`.
`tests/surface/interop/CMakeLists.txt` builds them into `sr_interop_tests`. `sr test` runs them with
the rest; `sr run sr_interop_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_interop_capabilities.cpp

The file covers the default values of the interop vocabulary.

### `DefaultCapabilitiesOfferNothing`

Default `InteropCapabilities` offer no host export, no import, no UUID and no importable kind.

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

## test_external_handle_kind_set.cpp

The file tests `ExternalHandleKindSet`, one bit per handle kind.

| Test | What it proves |
| --- | --- |
| `ADefaultSetIsEmpty` | A default set holds no kind. |
| `AKindSetHoldsExactlyTheKindsInserted` | A set holds the kind inserted and no other, and is empty again once that kind is erased. |
| `InsertAndEraseAreIdempotent` | A second insert keeps one member; erasing a kind the set does not hold changes nothing. |
| `EraseRemovesOnlyItsKind` | Erasing one kind leaves the others. |
| `SetsCompareByMembers` | Two sets are equal exactly when they hold the same kinds. |
| `WorksAtCompileTime` | Insert and contains work in a constant expression. |
