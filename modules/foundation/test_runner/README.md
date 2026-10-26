# test_runner

The module holds `main`, the entry point every test program shares: it puts the logger in
synchronous mode and runs every GoogleTest case, and `LogCapture`, the sink a test uses to read
what the runtime logged, and `RecordingObserver`, the observer a test uses to read a run's timing. It is an internal, test-only module: its source builds into the test
programs and never into the library.

It depends on `logging`, on `report` for the observer and on GoogleTest.

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

## recording_observer.hpp

`SushiRuntime::TestRunner::RecordingObserver` is a header-only `RunObserver` that copies every
`RunObservation` it receives under a mutex, since `on_run()` runs on the thread that learned the
run finished. `count()`, `runs()` and `last()` read the copies and `clear()` forgets them. A test
adds it with `Runtime::add_run_observer()` and keeps the returned handle while it reads runs.

## log_capture.hpp

`SushiRuntime::TestRunner::LogCapture` is header-only. Its constructor makes SushiLog synchronous,
sets the level of one project (the runtime's by default, `Warn`) and installs a sink that copies
each record into a vector, under a mutex since the sink runs on the logging thread. Its destructor
restores the default sink and the level the project had. `records()` returns the copies;
`count_at`, `count_in` and `has_message` answer the questions tests ask most. One capture lives
at a time, because the sink is a process-wide setting.

Tests that need the `Debug` level pass it to the constructor: `LogCapture capture(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Debug)`.
