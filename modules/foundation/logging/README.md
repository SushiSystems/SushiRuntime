# logging

The module holds the runtime's logger. One logger exists per process: its level table, its sink and
its thread live in `source/logger.cpp` inside the shared library, so a program and the library it loads
log through the same state. The module is internal: its headers are for the other modules of the
library, and they are not installed.

It depends on the C++ standard library, on `core` (`export.hpp`, `error.hpp`) and on `log`, whose
functions `source/logger.cpp` defines.

## Public entry points

`logger.hpp`, which includes `log_gate.hpp` and the two headers of `log`:

| Name | What it does |
| --- | --- |
| `SR_LOG_TRACE`, `SR_LOG_DEBUG`, `SR_LOG_INFO` | Log one record in a category; each `{}` takes the next argument |
| `SR_LOG_WARN`, `SR_LOG_ERROR` | The same, at the two levels that report a fallback or a failure |
| `SR_LOG_FATAL` | Logs after every queued record and flushes; the caller then ends the process |
| `SR_LOGGER_SET_SYNC_MODE(mode)` | Forwards to `set_log_synchronous` |
| `log_write(level, line)` | Hands a finished line to the sink, whatever the level |

`log_gate.hpp`:

| Name | What it does |
| --- | --- |
| `K_LOG_LEVEL_FLOOR` | The least severe level a translation unit compiles calls for |
| `Detail::log_level_slot`, `Detail::is_log_enabled` | What a macro reads before it formats a record |

`LogLevel`, `LogCategory`, `LogSink` and the calls that set them are declared by the `log` module.

Everything in `Detail` belongs to the macros.

## Categories

Each module that logs declares one category, named after its folder, in
`<module>_log_category.hpp`: `K_DISTRIBUTED_LOG_CATEGORY` is `make_log_category("distributed")`. A
call names it first: `SR_LOG_WARN(K_DISTRIBUTED_LOG_CATEGORY, "peer {} lost", id)`. A module logs
only under its own category, and `tools/logs/check_logging.py` reports a call without one.

## Level

Every category is at `Warn` unless something sets it. A record passes when its level is at or
above the level of its category, so `Warn` passes warnings, errors and fatal records and drops the
`Info` lines that `Runtime::create()` logs for each device.

`SR_LOG_LEVEL` in the environment sets the levels at the first log or level call. It takes a level
for every category, then any number of `category=level` pairs, in any case:
`SR_LOG_LEVEL=warn,distributed=debug`. The levels are `trace`, `debug`, `info`, `warn`, `error`,
`fatal` and `off`. A value the logger cannot read is reported once on stderr, and every level stays
`Warn`. In code, `set_log_level(level)` sets every category and `set_log_level(category, level)`
sets one; a category set that way keeps its level when `set_log_level(level)` runs later. A call
made before the first log call wins over the environment.

A log call goes through two gates.

1. `K_LOG_LEVEL_FLOOR` is fixed per translation unit: `Error` when `SR_ENABLE_LOGGING` is defined
   as 0 before the header is included, `Debug` under `NDEBUG`, `Trace` otherwise. A call below it
   compiles to nothing; `ERROR` and `FATAL` are never compiled out.
2. Each category's level is one `std::atomic<int>` in a table the library holds, 64 categories at
   most; past that, every new category shares one level and the logger says so on stderr. A call
   site keeps the address of its category's atomic in a static on its first pass, so each later
   call costs one relaxed load and a branch, and its arguments are not evaluated when it fails.

## Format

`Detail::sr_format` replaces each `{}` with the next argument, written through `operator<<`, and
writes `{{` and `}}` as single braces. It takes no format specification; a call site that needs a
width or hex formats the value first. Extra arguments and extra placeholders are ignored.

The format string travels inside `__VA_ARGS__`, so the macros take `(...)` and need neither
`__VA_OPT__` nor `##__VA_ARGS__`, which C++17 does not have.

## Record and sink

A record is one line: `[HH:MM:SS.mmm] [LEVEL] [category] [tN] [file:line] message`, in local time,
with no line break. `tN` is a small number the logger gives each thread on its first record, and
the file is its name without its folder. The sink receives the level and that line as a `std::string_view` that is valid
during the call.

With no sink set, every record goes to stderr, whatever its level, in one `fprintf` followed by a
flush. Stdout is left to the program. `set_log_sink` with an empty sink (`nullptr` or `{}`) restores
this default.

A `LogSink` is a `Delegate` with 48 bytes of inline storage, so a capture larger than that
does not compile.

### Which thread calls the sink

| Mode | Thread |
| --- | --- |
| Asynchronous (the default) | The logger's own thread, never a pool worker |
| Synchronous | The thread that made the log call, before the call returns |
| After the library's static destruction | The thread that made the log call; no sink is called when its mutex was abandoned, and the line goes to stderr |

One mutex is held across every sink call, so a sink is never entered twice at once and needs no
lock of its own.

### Replacing the sink

`set_log_sink` takes that same mutex. It returns once no call into the old sink is running, and the
old sink is never called again, so the caller may destroy what the old sink captured as soon as
`set_log_sink` returns. Records queued before the call and not yet delivered go to the new sink.

A sink must not log, call `set_log_sink` or call `set_log_synchronous`. Each of them can end up
waiting for the sink call that made it. A sink that throws does not lose its record; the logger
reports the exception on stderr and writes the record there.

### The queue

In asynchronous mode the records wait in a queue of 4096. A record that finds it full is dropped
and counted, and the next record delivered is preceded by a `WARN` in the category `logging` that
names how many were dropped. Synchronous mode queues nothing and drops nothing.

### FATAL

A `FATAL` record is not queued. The call waits until every queued record has been delivered, then
delivers its own on the calling thread, then flushes every output stream, so the reason the process
ends is the last line written. The caller ends the process.

### Synchronous mode

`set_log_synchronous(true)` first waits until every queued record has been delivered, then makes
each log call deliver its own record. Tests use it to read records in order: after it returns,
nothing logged earlier is still in flight. `set_log_synchronous(false)` goes back to the queue.

## Lifetime

The logger is created by the first call that needs it and is never destroyed, so a log call is valid
during static initialisation and at any point of process exit. Its thread starts with the first
asynchronous record. If the thread cannot start, the logger says so on stderr and stays synchronous.

When the library's statics are destroyed the logger stops its thread and joins it. On Linux the
thread delivers what is queued before it exits. On Windows the operating system has already ended
the thread by then, so the exiting thread delivers what the queue still holds. From that point each
log call delivers its own record on the calling thread, unless the sink's mutex was abandoned as
described below; then the record goes to stderr and the sink is not called.

Two cases lose records at exit. A process that ends without static destruction (`std::abort`,
`TerminateProcess`, `_exit`) loses whatever was queued. On Windows, a thread ended inside a sink
call leaves the sink's mutex held; the exiting thread sees that and drops the queued records
without waiting. That mutex is then recorded as abandoned and never taken again, so `set_log_sink`
returns without replacing the sink.

A sink that is still installed at exit is called during static destruction, so it must not use
objects that are destroyed before the library's statics.

## Tests

`tests/foundation/logging/` builds `sr_logging_tests`; its `README.md` describes it.
`tests/surface/api/runtime/test_logging.cpp` checks the level and the sink around
`Runtime::create()`.
