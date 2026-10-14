# usm_guard

The module holds `USMShared`, the move-only guard that frees a shared USM allocation, so a failing
assertion never leaks one. It is an internal module built only with the tests: its headers are
never installed and the library does not link it.

It depends on SYCL and on no other module of the repository.

## Public entry points

| Header | Holds |
| --- | --- |
| `usm_shared.hpp` | `USMShared` |

Every name is declared in `SushiRuntime::Test`.
