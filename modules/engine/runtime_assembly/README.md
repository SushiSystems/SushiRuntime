# runtime_assembly

The module builds the runtime objects a test runs on: a `RuntimeContext` from the arguments its
factory takes, and a `TaskScheduler` from the parts of a context. It is an internal module built
only with the tests: its headers are never installed and the library does not link it.

It depends on `execution`, `scheduler`, `topology` and `machine`.

## Public entry points

| Header | Holds |
| --- | --- |
| `context_factory.hpp` | `make_context()` |
| `scheduler_factory.hpp` | `make_scheduler()` |

Every name is declared in `SushiRuntime::Test`.

## context_factory.hpp

`make_context(...)` takes the arguments of `RuntimeContext::create()` and returns the
`std::shared_ptr<RuntimeContext>` it built. `RuntimeContext` has no public constructor, so a test
writes `const auto ctx_owner = Test::make_context(); RuntimeContext& ctx = *ctx_owner;`. When no
context can be built, `value()` stops the process with the error's text.

## scheduler_factory.hpp

`make_scheduler(context)` builds a `TaskScheduler` on a context's settings, topology, device
set and allocator, the four arguments the constructor takes. A test that drives the scheduler
without the execution engine writes `TaskScheduler scheduler = make_scheduler(ctx);`. The
scheduler is not movable; the function returns it by guaranteed elision, so it is built in the
caller's variable. The context must outlive it, and no worker thread starts until `start()`.
