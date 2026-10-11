# tests/common

Building blocks the test suites share: a thread barrier, host tasks, a USM guard, a device
probe, a measurement helper and the entry point of every test binary.

## measurement.hpp

`measure` calls a callable that times its own region and returns one figure, five times by
default, and returns the median, the minimum and the maximum. `print_measurement` writes one
line per case that starts with `SR_MEASUREMENT` and holds `key=value` pairs: benchmark, case,
unit, median, min, max, runs, build, workers and size. A before-and-after comparison greps
those lines out of the benchmark output.

`CompletionSignal` closes a timed region: the task that finishes the work raises it and the
test waits on it with a budget, so no polling sleep sits inside a measurement. A region that
ends on the signal includes one thread wake-up.

The header includes nothing from SushiRuntime.

## scheduler_fixture.hpp

`make_scheduler(context)` builds a `TaskScheduler` on a context's settings, topology, device
set and allocator, the four arguments the constructor takes. A test that drives the scheduler
without the execution engine writes `TaskScheduler scheduler = make_scheduler(ctx);`. The
scheduler is not movable; the function returns it by guaranteed elision, so it is built in the
caller's variable. The context must outlive it, and no worker thread starts until `start()`.

## test_context.hpp

`make_context(...)` takes the arguments of `RuntimeContext::create()` and returns the
`std::shared_ptr<RuntimeContext>` it built. `RuntimeContext` has no public constructor, so a test
writes `const auto ctx_owner = Test::make_context(); RuntimeContext& ctx = *ctx_owner;`. When no
context can be built, `value()` stops the process with the error's text.

## test_barrier.hpp

`std::barrier` is C++20 and the project compiles as C++17, so tests use `Barrier`, a minimal
equivalent. `arrive_and_wait()` blocks until the configured number of threads has arrived,
then releases them all and resets for the next phase. A generation counter makes the barrier
reusable across waves without a lost wakeup: a late arrival of the previous phase cannot pass
the next one by mistake. The count given at construction must be at least 1.

## test_helpers.hpp

The header holds an order-recording host task, an atomic-counter host task, and `USMShared`,
an RAII guard for USM memory, so a failing assertion never leaks an allocation.

`has_any_sycl_device` asks SYCL whether it reports a device and constructs nothing of the
runtime. A test that needs a device skips on its answer and builds its contexts outside any
catch, so a constructor that throws fails the test. An exception from SYCL itself also fails
it.

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
