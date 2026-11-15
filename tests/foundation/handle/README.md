# handle tests

The tests of the `handle` module, mirroring `modules/foundation/handle/`.
`tests/foundation/handle/CMakeLists.txt` builds them into `sr_handle_test_objects`, linked into
`sr_foundation_tests`. `sr test` runs them with the rest; `sr run sr_foundation_tests --
--gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file.

## test_global_handle.cpp

| Test | What it proves |
| --- | --- |
| `TheDefaultHandleIsNull` | The default handle equals `null()` and is null; generation 1 is not. |
| `EachFieldRoundTripsAtItsExtremes` | Each field, at its largest value with the others at zero, and all of them at once, comes back from the two packed words. |
| `EachFieldSitsAtItsDocumentedBits` | A handle with a distinct byte in each field packs into the two words the README states. |
| `FlagsCombineAndTest` | `operator\|` combines flags and `has_flags()` finds each one set and no other. |
| `OrderComparesUniverseMachineSlotGenerationKindFlags` | Seven handles, each greater than the last in one field of the documented order, compare as their positions do under every operator, and sorting them reversed restores the order. |
| `EqualHandlesHashEqualAndTheHashIsFixed` | Equal handles hash equal, and 1024 slots give 1024 distinct hashes. |
| `IncludesNoSYCLHeaderAndNoOtherModule` (suite `Small_HandleModule`) | No `#include` line in the module's headers names SYCL, and every repository include stays inside `SushiRuntime/handle/`. |

## test_device_key.cpp

| Test | What it proves |
| --- | --- |
| `TextRoundTripsEveryKind` | A UUID key prints as the documented text, and keys of each backend and id kind, the largest ordinal included, parse back equal. |
| `MalformedTextDoesNotParse` | Empty text, a missing field, an unknown backend or kind, a short or upper-case id, an empty, negative or too large ordinal all give nothing. |
| `OrderComparesBackendKindIDOrdinal` | Keys order by backend, id kind, id and ordinal, and sorting them reversed restores the order. |
| `EqualKeysHashEqualAndFieldsChangeTheHash` | Equal keys hash equal; changing any one field changes the hash. |

## test_handle_resolver.cpp

A fake resolver keeps the current generation of each slot and the address of each instance per
memory space.

| Test | What it proves |
| --- | --- |
| `ResolvesACurrentHandleOnItsSpace` | A current handle resolves to its instance's address on its space and is not stale; on another space it is `NotResident` with a null address. |
| `RefusesNullStaleAndForeignHandlesWithANullAddress` | The null handle, a handle of an old generation and one of another universe give `NullHandle`, `StaleHandle` and `ForeignUniverse`, each with a null address. |

## test_handle_space.cpp

A fake space mints from a fixed number of slots, lowest free slot first.

| Test | What it proves |
| --- | --- |
| `MintsDistinctHandlesInSlotOrderUntilExhausted` | Three mints give three distinct handles in slots 0, 1, 2 with the space's universe and the asked flags; the fourth gives `SpaceExhausted` and the null handle. |
| `RetireReusesTheSlotUnderAHigherGeneration` | A retired slot is minted again under a higher generation, so the new handle differs from the old. |
| `RetireRefusesNullStaleAndForeignHandles` | Retiring the null handle, a handle of another universe and an already retired handle gives `NullHandle`, `ForeignUniverse` and `StaleHandle`. |

## test_handle_status.cpp

| Test | What it proves |
| --- | --- |
| `EveryValueIsDistinctAndOkIsZero` | The six statuses have six distinct values, and `Ok` is 0 and the default. |

## test_local_handle_space.cpp

| Test | What it proves |
| --- | --- |
| `MintsInSlotOrderInTheLocalUniverseWithHostOnlyBorrowedFlags` | Three mints give slots 0, 1, 2, generation 1, the reserved universe, the space id in the machine field and the flags `HostOnly` and `Borrowed`. |
| `TheSameSequenceOfMintsGivesTheSameHandles` | Two spaces built with one id give equal handles for the same mints. |
| `SpacesWithDifferentIDsMintDistinctHandles` | Two ids give different first handles. |
| `ConcurrentMintsGiveEveryHandleOnce` | Four threads of 1000 mints give 4000 distinct handles over slots 0 to 3999. |

## test_transfer_path_id.cpp

| Test | What it proves |
| --- | --- |
| `DefaultNamesNoPath` | The default id is 0, is not set and differs from the id of the empty name. |
| `SameNameGivesSameIDAndDistinctNamesApart` | A name gives one id, and the four path names give four ids, all set. |
| `KeepsAFixedValueAcrossRuns` | The id of a known name equals its documented FNV-1a value. |
| `OrdersByValue` | Two ids order one way or the other and no id is below itself. |
| `EqualIDsHashEqual` | Equal ids hash equal and distinct ids differ. |

## test_memory_space_id.cpp

| Test | What it proves |
| --- | --- |
| `KeepsItsValueAndDefaultsToZero` | The id keeps 0 and the largest value. |
| `ComparesByValue` | Equality and order follow the value. |
| `EqualIDsHashEqualAndDistinctIDsApart` | Equal ids hash equal, and 64 ids give 64 hashes. |
