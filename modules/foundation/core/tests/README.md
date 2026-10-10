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
