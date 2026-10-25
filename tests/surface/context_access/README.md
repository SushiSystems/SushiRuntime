# context_access tests

The tests of the `context_access` module, mirroring `modules/surface/context_access/`.
`tests/surface/context_access/CMakeLists.txt` builds them into `sr_context_access_tests`. `sr test`
runs them with the rest; `sr run sr_context_access_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_context_access.cpp

The file covers the three functions of `Internal::ContextAccess` and their overloads for an
owner. The two tests that need a context build one with `Test::make_context()` of
`make_context()` of the `runtime_assembly` module, over the devices of the machine they run on.

### `HandleOwner`

`HandleOwner` keeps a `ContextHandle` in a private `context_` member and befriends
`Detail::ContextHandleAccess`, as `Runtime` does. It lets the owner overloads be tested below
`api`.

### `AHandleReturnsTheContextItWasBuiltFrom`

`handle()` around a context makes a handle that is not empty; `context()` returns the address of
that context and `shared()` a pointer equal to the one the handle was built from.

### `AnOwnerIsReadThroughItsHandle`

`context()` and `shared()` on a `HandleOwner` return the context of the handle it keeps.

### `ANullContextMakesAnEmptyHandle`

`handle(nullptr)` makes an empty handle, and `shared()` returns null for it.

### `EveryShareOfAHandleReachesOneCapabilityCache`

`capability_cache()` returns the same cache for a handle, a copy of it and an owner that keeps it.

### `EachHandleBuiltKeepsItsOwnCapabilityCache`

Two calls of `handle()` over one engine context build two surface contexts, each with its own
cache.
