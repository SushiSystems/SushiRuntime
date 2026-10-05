# tests/common

Building blocks the test suites share: a thread barrier, host tasks, a USM guard and the
entry point of every test binary.

## test_barrier.hpp

`std::barrier` is C++20 and the project compiles as C++17, so tests use `Barrier`, a minimal
equivalent. `arrive_and_wait()` blocks until the configured number of threads has arrived,
then releases them all and resets for the next phase. A generation counter makes the barrier
reusable across waves without a lost wakeup: a late arrival of the previous phase cannot pass
the next one by mistake. The count given at construction must be at least 1.

## test_helpers.hpp

The header holds an order-recording host task, an atomic-counter host task, and `UsmShared`,
an RAII guard for USM memory, so a failing assertion never leaks an allocation.

`make_order_recorder` takes an optional delay. Sleeping before the task records its ID widens
the window in which an ordering bug can surface.

## test_main.cpp

`SR_TSAN_BUILD` marks a ThreadSanitizer build on every compiler: Clang and IntelLLVM answer
the `__has_feature(thread_sanitizer)` test, and GCC predefines `__SANITIZE_THREAD__`.

Under ThreadSanitizer `main` flushes its output and calls `std::_Exit` with the GoogleTest
result, skipping C++ teardown. On the TSan CI lane the DeviceSanitizer layer of the Intel
oneAPI SYCL runtime crashes deterministically in its own global and `atexit` teardown. The
crash comes after every test has passed and the worker pool has joined, and it turned green
runs into a SEGV that failed the job. It lies wholly inside the uninstrumented Intel runtime's
shutdown, not in this code: by that point the workers are joined and every `RuntimeContext` is
destroyed. Data races are reported live during the run, and `halt_on_error=1` aborts at once,
so skipping the teardown loses nothing. The exit is scoped to TSan so that ASan's leak check
and ordinary builds keep a full, ordered shutdown.
