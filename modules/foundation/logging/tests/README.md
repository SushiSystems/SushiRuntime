# logging tests

The tests of the `logging` module, built into one program, `sr_logging_tests`, which
`modules/foundation/logging/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_logging_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_logger.cpp

The file covers the process-wide logger: the level gate, the sink and the two delivery modes.
The logger's state lives in the shared library, so every test changes state that the
next test would inherit.

### `Unit_LoggerTest`

The fixture puts the logger in synchronous mode and sets the level to `Warn` before each test, and
afterwards restores the default sink and the level the process had. A test sink captures a pointer
to a local vector, so the default sink has to be back before that vector dies.

`SR_LOG_LEVEL` is not tested here. The library reads it once per process, and by the time a test
runs the fixture has already asked for the level.

### `collect_into`

`collect_into` returns a sink that copies each record into a vector with the id of the thread that
ran it. The line is copied because the `std::string_view` a sink receives is valid only during the
call.

### `DisabledLevelDoesNotEvaluateItsArguments`

The argument of the log call is `++evaluations`. At `Error` a `Warn` call must leave the counter at
0; at `Warn` the same call raises it to 1. The test uses `Warn` and not `Debug` because a release
build removes `Debug` calls at compile time, which would pass the first half for the wrong reason.

### `LogWriteBypassesTheLevel`

`log_write` is the entry for a caller that has a finished line. It does not consult the level, so a
`Debug` line arrives at `Off`, and it arrives without the prefix the macros add.

### `EmptySinkRestoresTheDefault`

After `set_log_sink(nullptr)` the record must appear on stderr and the test sink's count must stay
at 1. GoogleTest's `CaptureStderr` and `CaptureStdout` redirect the two file descriptors, which is
what the default sink writes to from inside the library. Stdout must stay empty: the default sink
writes every level to stderr.

### `ThrowingSinkLeavesTheRecordOnStderr`

A sink that throws must not take the log call down with it, and the record must not be lost. The
test looks for both the exception's text and the record on stderr.

### `AsynchronousRecordsArriveBeforeSynchronousModeReturns`

In asynchronous mode the sink runs on the logger's thread, so each record carries a thread id other
than the test's. `set_log_synchronous(true)` waits for the queue, which is what lets the test read
the vector without a sleep or a lock: all three records are there, in the order they were logged.

