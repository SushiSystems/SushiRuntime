# logging tests

The tests of the `logging` module, mirroring `modules/foundation/logging/`.
`tests/foundation/logging/CMakeLists.txt` builds them into `sr_logging_tests`. `sr test` runs them
with the rest; `sr run sr_logging_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_logger.cpp

The file covers the process-wide logger: the levels of each category, the record, the sink, the
bounded queue, `FATAL` and the two delivery modes.
The logger's state lives in the shared library, so every test changes state that the
next test would inherit.

### `Medium_LoggerTest`

The fixture puts the logger in synchronous mode and sets the level to `Warn` before each test, and
afterwards restores the default sink and the level the process had. A test sink captures a pointer
to a local vector, so the default sink has to be back before that vector dies.

The tests log under two categories of their own, `logger_test_alpha` and `logger_test_beta`, so a
level a test pins on one of them reaches no module of the library.

### `collect_into`

`collect_into` returns a sink that copies each record into a vector with the id of the thread that
ran it. The line is copied because the `std::string_view` a sink receives is valid only during the
call.

### `DisabledLevelDoesNotEvaluateItsArguments`

The argument of the log call is `++evaluations`. At `Error` a `Warn` call must leave the counter
at 0; at `Warn` the same call raises it to 1. The test uses `Warn` and not `Debug` because a release
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

### `SinkReceivesLevelAndText`

The record must carry the level, the category, a thread number and the file and line of the call,
and must not carry the name of the function, which the record left.

### `EachCategoryKeepsItsOwnLevel`

`logger_test_beta` is set to `Debug`, then every category to `Error`. The beta level must survive
the second call, so a `Debug` record in beta passes and one in alpha does not.

### `AFullQueueReportsTheRecordsItDropped`

The sink blocks inside the first record until the test releases it, so the logger's thread cannot
empty the queue. The test logs 4096 records, which fill it, and 5 more, which are dropped. Once
released, the sink must see the held record, then a `WARN` in the category `logging` that names 5
dropped records, then the first queued one.

### `FatalIsDeliveredAfterTheQueueOnTheCallingThread`

Two `WARN` records are queued in asynchronous mode, then a `FATAL` one is logged. When the `FATAL`
call returns the sink must have the three, in order, with the `FATAL` one delivered on the test's
own thread. The test does not end the process; a caller of `SR_LOG_FATAL` does.

### `Medium_LoggerEnvironment`

The library reads `SR_LOG_LEVEL` once per process, so these two tests run their check in a child
process, through a death test in the `threadsafe` style, which starts the test program again. The
child sets the variable before its first log or level call. `TheVariableSetsEveryCategoryThenTheNamedOnes`
reads `error, logger_test_beta=DEBUG` back as `Error` for every category and `Debug` for beta.
`AnUnreadableValueLeavesEveryLevelAtWarn` gives a level the logger does not know and expects the
report on stderr and every level at `Warn`.

### `AsynchronousRecordsArriveBeforeSynchronousModeReturns`

In asynchronous mode the sink runs on the logger's thread, so each record carries a thread id other
than the test's. `set_log_synchronous(true)` waits for the queue, which is what lets the test read
the vector without a sleep or a lock: all three records are there, in the order they were logged.

## test_log_gate.cpp

The file tests the log gate: the floor of the build and the levels it lets through.

| Test | What it proves |
| --- | --- |
| `TheFloorFollowsTheBuild` | The floor is Error with logging off, Debug in a release build and Trace otherwise. |
| `ALevelPassesAtOrAboveItsCategoryLevel` | A level passes the gate when it is at or above its category's level. |
| `ACategorySlotKeepsItsAddress` | A category's slot keeps its address, so a call site may keep it. |
