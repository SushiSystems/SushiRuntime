# thread_barrier

The module holds `Barrier`, the reusable thread barrier the tests use in place of C++20's
`std::barrier`. It is an internal module built only with the tests: its headers are never installed
and the library does not link it.

It depends on the C++ standard library and on no other module of the repository.

## Public entry points

| Header | Holds |
| --- | --- |
| `barrier.hpp` | `Barrier` |

Every name is declared in `SushiRuntime::Test`.

## barrier.hpp

`std::barrier` is C++20 and the project compiles as C++17, so tests use `Barrier`, a minimal
equivalent. `arrive_and_wait()` blocks until the configured number of threads has arrived,
then releases them all and resets for the next phase. A generation counter makes the barrier
reusable across waves without a lost wakeup: a late arrival of the previous phase cannot pass
the next one by mistake. The count given at construction must be at least 1.
