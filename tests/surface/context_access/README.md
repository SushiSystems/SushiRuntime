# context_access tests

The tests of the `context_access` module, mirroring `modules/surface/context_access/`.
`tests/surface/context_access/CMakeLists.txt` builds them into `sr_context_access_test_objects`, linked into `sr_surface_tests`. `sr test`
runs them with the rest; `sr run sr_surface_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## compile_context_access.cpp

Includes `context_access.hpp` alone, so the build fails if the header misses an include. It holds
no test: every promise of `Internal::ContextAccess` is internal, and no test reaches an internal
name (decision 14 of `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md`). The public paths that use
it are tested through `Runtime`.

Under decision 17 of that report, the five promises the review dropped from this folder are held
as follows:

| Promise | Where it is held |
| --- | --- |
| A handle returns the context it was built from | `Medium_RuntimeEntry.AHandleItHandsOutReachesItsOwnContext` in `tests/surface/api/runtime/test_runtime.cpp` |
| An owner is read through its handle | The same test: it reads the context through the `Runtime` that owns the handle |
| A null context makes an empty handle | Untested: no public caller passes a null context, so the promise has no effect a caller sees |
| Every share of a handle reaches one capability cache | Untested: the cache changes no answer `Interop::capabilities()` gives, only how often the device is probed |
| Each handle built keeps its own capability cache | Untested, for the same reason |
