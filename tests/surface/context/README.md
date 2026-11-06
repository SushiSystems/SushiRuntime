# context tests

The tests of the `context` module, mirroring `modules/surface/context/`.
`tests/surface/context/CMakeLists.txt` builds them into `sr_context_test_objects`, linked into `sr_surface_tests`. `sr test` runs them with
the rest; `sr run sr_surface_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_context_handle.cpp

The file covers a `ContextHandle` that shares no context. It includes the module's handle header
and nothing of the engine, so it cannot build a context; a handle that shares one is
tested in `tests/surface/context_access/test_context_access.cpp`.

### `ACopyOfADefaultHandleIsEmpty`

A default handle converts to `false`, and so does a copy of it.

## test_context.cpp

The file has no test. It holds one compile-time check, at file scope, that the surface can hold
`Detail::Context` by shared pointer while the name has no definition. A run of the test program
does not list it.

`ContextHandleAccess::handle_of` has no test here: it is tested through its public owner, in
`tests/surface/api/runtime/test_runtime.cpp`, `AHandleItHandsOutReachesItsOwnContext`.
