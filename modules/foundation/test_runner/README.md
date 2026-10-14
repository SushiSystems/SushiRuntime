# test_runner

The module holds `main`, the entry point every test program shares: it puts the logger in
synchronous mode and runs every GoogleTest case. It is an internal, test-only module: its source
builds into the test programs and never into the library, and it has no headers.

It depends on `logging` and on GoogleTest.

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
