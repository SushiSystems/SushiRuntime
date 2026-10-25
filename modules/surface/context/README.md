# context

The share of a runtime's context that every handle of the surface holds: `ContextHandle`, the
opaque name of what it shares, and the one friend that builds a handle and reads it back. The
module is header-only and its headers are installed.

It depends on the C++ standard library and on no other module of the repository.

## Public entry points

| Header | Holds |
| --- | --- |
| `context.hpp` | `Detail::Context`, the name the surface gives the context of a runtime |
| `context_handle.hpp` | `ContextHandle` |
| `context_handle_access.hpp` | `Detail::ContextHandleAccess`: `wrap()`, `share()` and `handle_of()` |

`ContextHandle` is declared in `SushiRuntime`. A consumer receives handles inside the types of
`api` and writes none of these names.

## context.hpp

### Context

`Detail::Context` is declared here and defined only in `context_access.cpp`. A handle holds a
`std::shared_ptr<Detail::Context>` and reads nothing through it, so no installed header names a
type of the engine and the engine can rename or reshape its context without one changing. The
`context_access` module is the one place that converts between this name and the engine's
context.

## context_handle.hpp

### ContextHandle

A ContextHandle shares ownership of one runtime's context. A `Runtime` and every graph,
recorder, region, buffer and view it hands out hold one, so the context lives as long as the
last of them. A program that holds a handle can copy it and test it for emptiness, and nothing
more: the constructor that takes a context is private, and the handle has one friend,
`Detail::ContextHandleAccess`.

## context_handle_access.hpp

### ContextHandleAccess

ContextHandleAccess is the way into a handle for the library's own sources. `wrap()` builds a
handle around an opaque context and `share()` returns the share a handle holds. Both speak of
`Detail::Context` only, so the header is installed and names nothing of the engine: a consumer
that calls them gets a pointer to a type it cannot complete.

`handle_of()` returns the handle an owner keeps in its `context_` member. It is a template over
the owner, which befriends this struct, so this module names no type of the modules above it.
`Runtime` is such an owner, and that is how the distributed extension and the tests go from a
`Runtime` to its context without `Runtime` having a member that returns it.
