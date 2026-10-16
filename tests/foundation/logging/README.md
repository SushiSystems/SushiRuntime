# logging tests

The tests of the `logging` module, mirroring `modules/foundation/logging/`.
`tests/foundation/logging/CMakeLists.txt` builds them into `sr_logging_tests`. `sr test` runs them
with the rest; `sr run sr_logging_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_logger.cpp

The file covers the process-wide logger: the levels of each project and category, the record,
the sink, the bounded queue, `FATAL` and the two delivery modes. Forwarding and the C sink are
tested beside their headers, in `tests/foundation/log/`.
The logger's state lives in the shared library, so every test changes state that the
next test would inherit.

### `Medium_LoggerTest`

The fixture puts the logger in synchronous mode and sets the test project to `Warn` before each
test, and afterwards restores the default sink and the level the project had. A test sink captures a pointer
to a local vector, so the default sink has to be back before that vector dies.

The tests log under a project of their own, `logger_test`, with the modules `alpha` and `beta`,
so a level a test sets reaches no module of the library. A second project, `logger_other`, has a
module also called `alpha`, which the first project's variable must leave.

### `collect_into`

`collect_into` returns a sink that copies every field of each record into a vector, with the id
of the thread that ran it. The fields are copied because the views of a `LogRecord` are valid only
during the call.

### `DisabledLevelDoesNotEvaluateItsArguments`

The argument of the log call is `++evaluations`. At `Error` a `Warn` call must leave the counter
at 0; at `Warn` the same call raises it to 1. The test uses `Warn` and not `Debug` because a release
build removes `Debug` calls at compile time, which would pass the first half for the wrong reason.

### `EmptySinkRestoresTheDefault`

After `set_log_sink(nullptr)` the record must appear on stderr and the test sink's count must stay
at 1. GoogleTest's `CaptureStderr` and `CaptureStdout` redirect the two file descriptors, which is
what the default sink writes to from inside the library. Stdout must stay empty: the default sink
writes every level to stderr.

### `ThrowingSinkLeavesTheRecordOnStderr`

A sink that throws must not take the log call down with it, and the record must not be lost. The
test looks for both the exception's text and the record on stderr.

### `SinkReceivesEveryFieldAndTheLine`

Each field must arrive apart: the level, `logger_test` and `alpha`, the file without its folder, the
line, a thread number, a time no earlier than the call and the message alone. The line must join
them as `[WARN] [logger_test.alpha] [tN] [test_logger.cpp:L] message`.

### `EachCategoryKeepsItsOwnLevel`

`beta` is set to `Debug`, then the whole test project to `Error`. The beta level must survive
the second call, so a `Debug` record in beta passes and one in alpha does not.

### `AFullQueueReportsTheRecordsItDropped`

The sink blocks inside the first record until the test releases it, so the logger's thread cannot
empty the queue. The test logs 4096 records, which fill it, and 5 more, which are dropped. Once
released, the sink must see the held record, then a `WARN` in the category `sushiruntime.logging` that names 5
dropped records, then the first queued one.

### `FatalIsDeliveredAfterTheQueueOnTheCallingThread`

Two `WARN` records are queued in asynchronous mode, then a `FATAL` one is logged. When the `FATAL`
call returns the sink must have the three, in order, with the `FATAL` one delivered on the test's
own thread. The test does not end the process; a caller of `SR_LOG_FATAL` does.

### `Medium_LoggerEnvironment`

The library reads a project's variable once per process, so these two tests run their check in
a child process, through a death test in the `threadsafe` style, which starts the test program
again. The child sets `SR_LOGGER_TEST_LOG_LEVEL` before the project's first use.
`TheVariableSetsItsProjectThenTheNamedModules` reads `error, BETA=DEBUG` back as `Error` for the
project and `alpha`, `Debug` for `beta`, and `Warn` for `alpha` of the other project.
`AnUnreadableValueLeavesItsProjectAtWarn` gives a level the logger does not know and expects the
report, naming the variable, on stderr and every level of the project at `Warn`.

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
