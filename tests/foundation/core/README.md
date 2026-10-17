# core tests

The tests of the `core` module, mirroring `modules/foundation/core/`.
`tests/foundation/core/CMakeLists.txt` builds them into `sr_core_tests`. `sr test` runs them with
the rest; `sr run sr_core_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_assert.cpp

The file covers `SR_ASSERT`, `SR_VERIFY`, `SR_FATAL` and `set_assertion_handler()`. It defines
`SR_ENABLE_ASSERTIONS` as 1 before the include, so `SR_ASSERT` is checked in a release build of
the tests too.

### Death tests

Each of the three death tests runs its macro in a child process and matches the line the default
handler writes against the message or the condition's text, followed by `test_assert.cpp:` and
the line of the macro.

### `AReplacedHandlerIsCalledBeforeTheProcessStops`

The handler is installed inside the child process, so the parent keeps the default. The handler
returns, and the child still dies.

## test_delegate.cpp

The file covers `Delegate`: the empty state, invocation and return values, argument
forwarding, move semantics (a move leaves the source empty), destruction of the erased target, and
that a move-only target is held without a copy.

### `DestroysTarget`

The lambda owns a copy of the `shared_ptr`. Destroying the `Delegate` must run the destructor of the
lambda and release that copy.

### `VoidSignatureDiscardsResultOfValueCallable`

A `Delegate<void(int)>` holds a lambda that returns an `int`. The call runs the lambda and drops
the value it returns.

### `HoldsNothrowMovableMoveOnlyTarget`

The lambda captures a `unique_ptr`, so it cannot be copied and its move constructor is `noexcept`;
two `static_assert`s state both. After the `Delegate` is moved, the source is empty and the
destination returns the pointee.

## test_error.cpp

The file covers `ErrorCode` and its `to_string`.

### `EveryEnumeratorKeepsItsNumber`

`ErrorCode` is stored as a `std::uint8_t` and crosses the library boundary inside `Error`. A consumer
built against an older header compares the numbers it compiled in, so a new enumerator goes at the
end and the test pins each number, `ProtocolMismatch` at 10.

## test_ref_counted.cpp

The file covers the intrusive reference count `RefCounted` and its smart pointer `sushi_ptr`: single
ownership, copy and move semantics, `leak()`, polymorphic deletion, and concurrent counting without
a race.

## test_result.cpp

The file covers the accessors of `Result`: `value()` and `operator*` on a const, a non-const and
an expiring `Result`, `operator->` on a const and a non-const one, and `error()` on both. It covers
`Result<void>` too. An accessor used on the wrong side stops the process, so those cases are death
tests in the suite `Medium_ResultDeathTest`, which GoogleTest runs before the other suites.

### `K_ERROR_OF_VALUE`, `K_VALUE_OF_ERROR`, `lost_device`

`K_ERROR_OF_VALUE` is the pattern of the line `error()` makes the handler write on a value.
`K_VALUE_OF_ERROR` is the text a value accessor hands the handler when the error type is not
`Error`. `lost_device` returns `ErrorCode::DeviceLost` with the message "lost the device".

### `SuccessValueIsReadThroughEveryValueAccessor`

The test reads an `int` through `value()` and `operator*`, writes it through `value()`, and reads it again
through a const reference. It reads a `std::string` through `operator->` on a value and on a const
reference.

### `AnExpiringResultGivesItsValueAway`, `AValueAccessorOnAnExpiringErrorStops`

`value()` and `operator*` on a temporary `Result` of `std::unique_ptr<int>` move the pointer out,
which a copy could not do. On a temporary that holds `lost_device()` both stop the process with
the stored message.

### `ErrorOnErrorReturnsTheStoredError`

`error()` on a `Result` that holds an error returns the stored code and message, and a write
through the non-const form is read back through the const one.

### `TheErrorTypeDefaultsToError`

`Result<int>` is `Result<int, Error>`, and `Result<void>` is `Result<void, Error>`.

### `EveryValueAccessorOnAnErrorStopsWithTheStoredMessage`

Each of the six value accessors on a `Result` that holds `lost_device()` stops the process, and
the line the handler writes holds "lost the device".

### `EveryValueAccessorOnAnErrorOfAnotherTypeStops`

For a `Result` whose error type is `int`, the same six accessors stop the process with the text
of `K_VALUE_OF_ERROR`.

### `ErrorOnAValueStops`

`error()` on a `Result` that holds a value, const or non-const, stops the process.

### `VoidSuccessHoldsNoErrorAndValueReturns`, `VoidErrorReturnsTheStoredError`

A default-constructed `Result<void>` is a success, and `value()` returns. One built from
`make_error` reports no value and returns the stored error from both forms of `error()`.

### `VoidValueOnAnErrorStopsWithTheStoredMessage`, `VoidErrorOnSuccessStops`

`value()` on a failed `Result<void>` stops the process with the stored message, or with
`K_VALUE_OF_ERROR` when the error type is `int`. `error()` on a success stops the process.

## test_span.cpp

The file covers `SushiRuntime::span`: construction, subviews and the container binding rules. The binding
rules are static_asserts: a non-const span does not bind to an rvalue container, and a const span binds to
one. Under C++20 the file also checks conversion to and from `std::span`.

### `sum`

`sum` adds the elements of a span of const ints.

### `DefaultIsEmpty`

A default span is empty, has size 0 and a null data pointer.

### `ConstructsFromPointerAndCount`

A span built from a pointer and a count points at the first element and reports the count as its size
and the byte count as its `size_bytes`. Its element 1 reads 20.

### `ConstructsFromCArray`

A span built from a C array has the array's size, and its `back()` is the last element.

### `ConstructsFromLvalueVector`

A span over an lvalue vector has the vector's data pointer and size. A write through the span changes the
vector's first element.

### `ConstructsFromStdArray`

A span over a `std::array` has the array's size and its `front()` element.

### `ConstViewOverConstContainer`

A const span over a const vector has the vector's size and data pointer.

### `ConstViewOverNonConstLvalue`

A const span over a non-const vector sums the elements to 6.

### `AcceptsTemporaryContainerForConstElements`

A const span accepts a temporary vector, and `sum` over that vector returns 6.

### `FirstLastAndSubspanSelectElements`

On a span of the four values 1 to 4, `first(2)` holds 1 and 2, `last(2)` holds 3 and 4, `subspan(1, 2)`
holds 2 and 3, and `subspan(1)` has size 3 and ends at 4.

### `ConvertsToAndFromStdSpan`

Compiled under C++20 only. A `std::span` over an array converts to a span and back. Both conversions keep
the data pointer and the size of 3.

## test_strong_id.cpp

The file covers `StrongID`. Its `static_assert`s state that an id is trivially copyable, has
the size of its representation and does not convert from an integer.

### `DefaultIsZero`, `ExplicitConstructionKeepsValue`

A default id wraps zero, and an id built from an integer returns that integer from `value()`.

### `EqualityComparesValues`, `OrderingFollowsValues`

`==`, `!=` and `<` compare the wrapped integers.

### `UsableAsUnorderedMapKey`, `UsableAsOrderedMapKey`, `HashForwardsToRepresentation`

An id is a key of `std::unordered_map` and of `std::map`, and `std::hash` of an id equals
`std::hash` of its integer.

## test_sushi_ptr.cpp

The file covers the construction, comparison and ownership rules of `sushi_ptr`. Three static_asserts check
that a raw pointer converts to `sushi_ptr` only explicitly, and that this construction does not throw.

### `Probe`

`Probe` is a `RefCounted` payload that holds a reference to an external counter. Its destructor increments
that counter.

### `DerivedProbe`

`DerivedProbe` is a `Probe` subclass. It compares a base-typed `sushi_ptr` with a derived one.

### `RawPointerComparisonKeepsCount`

The test compares an owner with its own raw pointer, const and non-const, and with another object's raw
pointer, in both orders. The owner's reference count is unchanged afterwards, and nothing is destroyed.

### `BaseTypeComparisonKeepsCount`

A base-typed `sushi_ptr` and a derived one compare equal in both orders. Neither reference count changes.

### `NullComparisonsAreUnambiguous`

An empty and a non-empty `sushi_ptr` compare with `nullptr` and with a null `const Probe*`, in both orders.
A non-empty `sushi_ptr` does not compare equal to a null raw pointer.

### `SelfAssignmentKeepsObject`

Copy-assigning a `sushi_ptr` to itself, and then move-assigning it to itself, keeps the object. Its count is
1 afterwards, and nothing is destroyed.

### `ResetReleasesOldAndTakesNew`

Resetting the first owner to the second owner's object destroys the object the first owner held and gives
the second object a count of 2. Resetting the first owner to null leaves the second object with a count of
1 and destroys nothing more.

### `LeakRoundTripHandsReferenceBack`

`leak()` empties the owner and leaves the reference count of 1 on the raw pointer. Constructing a new
`sushi_ptr` from that pointer makes the count 2, and the count returns to 1 when the new owner is destroyed.
A `release()` on the raw pointer then destroys the object.

## test_version.cpp

The file covers `version_matches()`, `require_matching_version()` and the comparison behind them.

### `TheTestProgramMatchesTheLibraryItLoaded`

The test program and the library it loads are built from one tree, so `require_matching_version()`
returns here. The mismatch cases call `Detail::require_same_release`, the function it forwards to,
with two versions the test chooses.

### `APatchDifferenceIsAMatch`

Only the major and the minor are compared. A patch release changes no header a consumer compiled.

### `AnOlderLibraryStopsWithBothVersions`, `ANewerLibraryStopsAndIsCalledNewer`

A mismatch stops the process. The line the handler writes is compared, because it is what a person
reads when the loader picked up a stale `sushiruntime` library: both versions, and which of the
two is the older.

## test_bit.cpp

The file tests popcount_u64() and countr_zero_u64() on edge and mixed words.

| Test | What it proves |
| --- | --- |
| `PopcountCountsEverySetBit` | Checks the bit count of an empty, a full and a mixed word. |
| `CountrZeroFindsTheLowestSetBit` | Checks the trailing zero count, including 64 for a word with no bit set. |

## test_common.cpp

The file tests the constants of common.hpp: their values and the powers of two they promise.

| Test | What it proves |
| --- | --- |
| `ConstantsHoldTheirDocumentedValues` | Checks the values the scheduler and the allocators are sized by. |
| `AlignmentsArePowersOfTwo` | Both alignments are powers of two and the default covers a cache line. |

## test_error_translation.cpp

The file tests translate_exceptions(): success, a standard exception, bad_alloc and an unnamed
throw.

| Test | What it proves |
| --- | --- |
| `AReturningFunctionGivesAValue` | A function that returns gives a value and runs once. |
| `AStandardExceptionKeepsItsText` | A standard exception becomes the given code with its text. |
| `BadAllocIsAllocationFailed` | `bad_alloc` is AllocationFailed whatever code the caller gave. |
| `AnUnnamedThrowGetsTheFixedMessage` | A thrown non-exception is named, and the default code is KernelFailed. |

## test_export.cpp

The file tests the export macros: each one is defined, and SR_API spells SUSHIRUNTIME_API.

| Test | What it proves |
| --- | --- |
| `ShortSpellingExpandsLikeTheLongOne` | The short spelling expands to the same attribute as the long one. |
| `AConsumerImportsTheSymbols` | A consumer of the library imports, which a test program is. |

## test_operation_id.cpp

The file tests OperationID: the FNV-1a hash, the literal, naming and ordering.

| Test | What it proves |
| --- | --- |
| `HashIsFNV1a` | Checks the hash against the FNV-1a offset basis and a published value. |
| `LiteralMatchesTheExplicitHash` | The literal hashes at compile time and equals the explicit form. |
| `DefaultIsUnnamedAndOrderFollowsTheValue` | The default names nothing and ordering follows the value. |
