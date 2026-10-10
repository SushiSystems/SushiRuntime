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

## test_ref_counted.cpp

The file covers the intrusive reference count `RefCounted` and its smart pointer `sushi_ptr`: single
ownership, copy and move semantics, `leak()`, polymorphic deletion, and concurrent counting without
a race.
