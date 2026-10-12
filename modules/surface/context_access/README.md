# context_access

The way from a `ContextHandle` to the engine's `RuntimeContext` and back, for the library's own
sources, the distributed extension and the tests. The module is internal: its header is in
`include/SushiRuntime/context_access/` for the modules of the library to include, and it is not
installed.

It depends on `core`, `execution` and `context` in its header.

## Public entry points

| Header | Holds |
| --- | --- |
| `context_access.hpp` | `context()`, `shared()` and `handle()`, in `SushiRuntime::Internal::ContextAccess` |

## context_access.hpp

### From a handle to the context

A `ContextHandle` holds its context under the opaque name `Detail::Context` of the `context`
module. `context()` returns the engine's context a handle shares and `shared()` returns a share
of it; `handle()` builds the handle around a context the library created, which is how
`Runtime::create()` makes its own. `shared()` returns a new `std::shared_ptr` on the same control
block, so it costs one reference count; the callers are allocations and the start of a worker.
`context()` returns a reference and counts nothing.

### From an owner to the context

`context()` and `shared()` also take an owner of a handle: a type that keeps a `ContextHandle`
in a `context_` member and befriends `Detail::ContextHandleAccess`. `Runtime` is one. The
overloads are templates over the owner, so this module names no type of `api`, which sits above
it.

## context_access.cpp

### The one conversion

`engine_context()` and `surface_context()` are the two pointer casts between `Detail::Context`
and `Internal::Execution::RuntimeContext`. They are private to this file and no other source of
the library casts between the two types. A context is created as the engine's type and handed
to `handle()`, so every `Detail::Context` pointer a handle holds came out of `surface_context()`.
