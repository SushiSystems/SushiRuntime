# interop tests

The tests of the `interop` module, mirroring `modules/surface/interop/`.
`tests/surface/interop/CMakeLists.txt` builds them into `sr_interop_test_objects`, linked into `sr_surface_tests`. `sr test` runs them with
the rest; `sr run sr_surface_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_interop_capabilities.cpp

The file tests the device UUID of the interop vocabulary.

| Test | What it proves |
| --- | --- |
| `UUIDsCompareByEveryByte` | Two UUIDs are equal only when every byte is. |

## test_external_handle_kind_set.cpp

The file tests `ExternalHandleKindSet`, one bit per handle kind.

| Test | What it proves |
| --- | --- |
| `HoldsOnlyTheKindInserted` | A set holds the kind inserted and no other. |
| `IsEmptyAgainAfterErasingItsKind` | A set is empty again once its only kind is erased. |
| `InsertingAKindTwiceKeepsOneMember` | A second insert of a kind the set holds changes nothing. |
| `EraseRemovesOnlyItsKind` | Erasing one kind leaves the others. |
| `SetsCompareByMembers` | Two sets are equal exactly when they hold the same kinds. |

The constant-expression use of the set is checked by a `static_assert` at file scope, which the build compiles and no test runs.

## compile_device_uuid.cpp

Includes `device_uuid.hpp` alone, so the build fails if the header misses an include. It holds no test.

## compile_external_memory.cpp

Includes `external_memory.hpp` alone, so the build fails if the header misses an include. It holds no test.
