# log tests

The tests of the `log` module, mirroring `modules/foundation/log/`.
`tests/foundation/log/CMakeLists.txt` builds them into `sr_log_tests`. `sr test` runs them with the
rest; `sr run sr_log_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_sushiruntime_log_project.cpp

| Test | What it proves |
| --- | --- |
| `TheProjectIsTheRuntimeReadFromItsVariable` | `K_SUSHIRUNTIME_LOG_PROJECT` is `sushiruntime` with `SR_LOG_LEVEL`, at compile time. |

## test_logger.cpp

The file covers the `SR_LOG_*` macros over SushiLog: their gates, the record they make and
`FATAL`. The logger itself, its queue, its threads and its level variables, is SushiLog's and is
tested there. Its state lives in the SushiLog library, so every test changes state that the next
test would inherit.

### `Medium_LoggerTest`

The fixture puts the logger in synchronous mode and sets the test project to `Warn` before each
test, and afterwards restores the default sink and the level the project had. A test sink captures a pointer
to a local vector, so the default sink has to be back before that vector dies.

The tests log under a project of their own, `logger_test`, with the module `alpha`, so a level
a test sets reaches no module of the library.

### `collect_into`

`collect_into` returns a sink that copies every field of each record into a vector, with the id
of the thread that ran it. The fields are copied because the views of a `LogRecord` are valid only
during the call.

### `DisabledLevelDoesNotEvaluateItsArguments`

The argument of the log call is `++evaluations`. At `Error` a `Warn` call must leave the counter
at 0; at `Warn` the same call raises it to 1. The test uses `Warn` and not `Debug` because a release
build removes `Debug` calls at compile time, which would pass the first half for the wrong reason.

### `SinkReceivesEveryFieldAndTheLine`

Each field must arrive apart: the level, `logger_test` and `alpha`, the file without its folder, the
line, a thread number, a time no earlier than the call and the message alone. The line must join
them as `[WARN] [logger_test.alpha] [tN] [test_logger.cpp:L] message`.

### `FatalIsDeliveredAfterTheQueueOnTheCallingThread`

Two `WARN` records are queued in asynchronous mode, then a `FATAL` one is logged. When the `FATAL`
call returns the sink must have the three, in order, with the `FATAL` one delivered on the test's
own thread. The test does not end the process; a caller of `SR_LOG_FATAL` does.

## test_log_gate.cpp

The file tests the log gate: the floor of the build and the levels it lets through.

| Test | What it proves |
| --- | --- |
| `TheFloorFollowsTheBuild` | The floor is Error with logging off, Debug in a release build and Trace otherwise. |
| `ALevelPassesAtOrAboveItsCategoryLevel` | A level passes the gate when it is at or above its category's level. |
