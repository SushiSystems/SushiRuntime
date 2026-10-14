# context tests

The tests of the `context` module, mirroring `modules/surface/context/`.
`tests/surface/context/CMakeLists.txt` builds them into `sr_context_tests`. `sr test` runs them with
the rest; `sr run sr_context_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_context_handle.cpp

The file covers a `ContextHandle` that shares no context. It includes the two headers of the
module and nothing of the engine, so it cannot build a context; a handle that shares one is
tested in `tests/surface/context_access/test_context_access.cpp`.

### `ADefaultHandleIsEmpty`

A default handle converts to `false`, and the share `ContextHandleAccess::share()` returns for
it is null.

### `AHandleWrappedAroundNothingIsEmpty`

`ContextHandleAccess::wrap(nullptr)` makes a handle that converts to `false`, and a copy of it
does too.
