# context tests

The tests of the `context` module, built into one program, `sr_context_tests`, which
`modules/surface/context/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_context_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_context_handle.cpp

The file covers a `ContextHandle` that shares no context. It includes the two headers of the
module and nothing of the engine, so it cannot build a context; a handle that shares one is
tested in `modules/surface/context_access/tests/test_context_access.cpp`.

### `ADefaultHandleIsEmpty`

A default handle converts to `false`, and the share `ContextHandleAccess::share()` returns for
it is null.

### `AHandleWrappedAroundNothingIsEmpty`

`ContextHandleAccess::wrap(nullptr)` makes a handle that converts to `false`, and a copy of it
does too.
