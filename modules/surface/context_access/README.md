# context_access

The way from a `ContextHandle` to the engine's `RuntimeContext` and back, for the library's own
sources, the distributed extension and the tests. The module is internal: its header is in
`include/SushiRuntime/context_access/` for the modules of the library to include, and it is not
installed.

It depends on `core`, `execution` and `context` in its header, and on `memory_exchange` in its
source.

## Public entry points

| Header | Holds |
| --- | --- |
| `context_access.hpp` | `context()`, `shared()`, `capability_cache()` and `handle()`, in `SushiRuntime::Internal::ContextAccess` |

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

### The runtime's capability cache

`capability_cache()` returns the `CapabilityCache` of `memory_exchange` that the handle's runtime
owns, for a handle or for its owner. Every copy of a handle reaches the same cache, and it dies
with the last of them. `api` passes it to `query_capabilities()` and `import_device_memory()`.

## context_access.cpp

### The surface context

`Detail::Context` is defined here and nowhere else. It holds the engine's `RuntimeContext` and
the runtime's `CapabilityCache`, declared in that order, so the cache is destroyed first and every
`sycl::device` it holds is released before the engine context goes. `handle()` builds one
`Detail::Context` per call, which costs one heap allocation, and hands it to
`Detail::ContextHandleAccess::wrap()`; `handle(nullptr)` builds none and returns an empty handle.
`context()` reads the engine context through it, and `shared()` returns a `std::shared_ptr` to the
engine context on the surface context's control block, so the engine context lives as long as any
share of either.

This module is the one owner because it already defines what a runtime is at the surface: every
handle of a runtime shares this one object.
