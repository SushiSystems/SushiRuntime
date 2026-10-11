# core tests

The tests of the `core` module, built into one program, `sr_core_tests`, which
`modules/foundation/core/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_core_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_delegate.cpp

The file covers `Core::Delegate`: the empty state, invocation and return values, argument
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

The file covers `Core::Errc`, its `to_string` and `Core::SushiException`.

### `EveryEnumeratorKeepsItsNumber`

`Errc` is stored as a `std::uint8_t` and crosses the library boundary inside `Error`. A consumer
built against an older header compares the numbers it compiled in, so a new enumerator goes at the
end and the test pins each number, `invalid_state` at 9.

### `AHandlerForRuntimeErrorCatchesSushiException`

Code written before the library threw one type catches `std::runtime_error`. The test catches
through that base, checks `what()`, and recovers the `Errc` with a `dynamic_cast`.

## test_ref_counted.cpp

The file covers the intrusive reference count `RefCounted` and its smart pointer `sushi_ptr`: single
ownership, copy and move semantics, `leak()`, polymorphic deletion, and concurrent counting without
a race.

## test_result.cpp

The file covers the value and error accessors of `Result`, and the `SushiException` that each misused
access throws.

### `exception_from`

`exception_from` runs an access that must throw and returns the `SushiException` it threw. It throws
`std::logic_error` when the access throws nothing else.

### `expect_device_lost`, `expect_value_read_on_error`, `expect_error_read_on_value`

The three helpers check the error code and the message of an exception. `expect_device_lost` expects
`Errc::device_lost` with the message "lost the device". `expect_value_read_on_error` expects
`Errc::invalid_state` with the message "Result::value(): the result holds an error.".
`expect_error_read_on_value` expects `Errc::invalid_state` with the message "Result::error(): the
result holds a value.".

### `SuccessValueIsReadThroughEveryValueAccessor`

The test reads an `int` through `value()` and `operator*`, writes it through `value()`, and reads it again
through a const reference. It reads a `std::string` through `operator->` on a value and on a const
reference.

### `ValueOnErrorThrowsTheStoredErrorUnchanged`

`value()` on a `Result` that holds an error throws a `SushiException` that carries the stored error, with
the same code and message.

### `EveryValueAccessorOnErrorThrowsTheStoredError`

Each value accessor on an error throws the stored `device_lost` error. The accessors are `value()`,
`operator*` and `operator->`, each on a `Result` of `int` and of `std::string`, and each on a const and a
non-const `Result`.

### `ValueOnNonErrorErrorTypeThrowsInvalidState`

For a `Result` whose error type is `int`, every value accessor on an error throws `Errc::invalid_state`
with the message "Result::value(): the result holds an error.".

### `ErrorOnSuccessThrowsInvalidState`

`error()` on a `Result` that holds a value, const or non-const, throws `Errc::invalid_state` with the
message "Result::error(): the result holds a value.".

### `ErrorOnErrorReturnsTheStoredError`

`error()` on a `Result` that holds an error returns the stored error, const or non-const, with its code
and message.

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

## test_strong_id.cpp

The file covers `Core::StrongID`. Its `static_assert`s state that an id is trivially copyable, has
the size of its representation and does not convert from an integer.

### `DefaultIsZero`, `ExplicitConstructionKeepsValue`

A default id wraps zero, and an id built from an integer returns that integer from `value()`.

### `EqualityComparesValues`, `OrderingFollowsValues`

`==`, `!=` and `<` compare the wrapped integers.

### `UsableAsUnorderedMapKey`, `UsableAsOrderedMapKey`, `HashForwardsToRepresentation`

An id is a key of `std::unordered_map` and of `std::map`, and `std::hash` of an id equals
`std::hash` of its integer.

## test_version.cpp

The file covers `version_matches()`, `require_matching_version()` and the comparison behind them.

### `mismatch_message`

The test program and the library it loads are built from one tree, so `require_matching_version()`
cannot be made to throw here. `mismatch_message` calls `Detail::require_same_release`, the function
it forwards to, with two versions the test chooses, and returns the message of the
`SushiException` or an empty string.

### `APatchDifferenceIsAMatch`

Only the major and the minor are compared. A patch release changes no header a consumer compiled.

### `AnOlderLibraryIsRefusedWithBothVersions`

The whole message is compared, because it is what a person reads when the loader picked up a stale
`sushiruntime` library: both versions, and which of the two is the older.
