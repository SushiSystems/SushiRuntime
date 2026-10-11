# logging

The module holds the runtime's logger. One logger exists per process: its level, its sink and its
thread live in `source/logger.cpp` inside the shared library, so a program and the library it loads
log through the same state.

It depends on the C++ standard library and on `core` (`export.hpp`, `error.hpp`, `delegate.hpp`).

## Public entry points

`logger.hpp`, which includes the other two headers:

| Name | What it does |
| --- | --- |
| `SR_LOG_ERROR`, `SR_LOG_WARN` | Log one record; each `{}` takes the next argument |
| `SR_LOG_INFO`, `SR_LOG_DEBUG` | The same, at the two lower levels |
| `SR_LOGGER_SET_SYNC_MODE(mode)` | Forwards to `set_log_synchronous` |
| `set_log_synchronous(bool)` | Chooses which thread delivers a record |
| `log_write(level, line)` | Hands a finished line to the sink, whatever the level |

`log_level.hpp`:

| Name | What it does |
| --- | --- |
| `LogLevel` | `Off`, `Error`, `Warn`, `Info`, `Debug` |
| `set_log_level(level)`, `log_level()` | Set and read the level of the process |
| `K_LOG_LEVEL_CEILING` | The highest level a translation unit compiles calls for |

`log_sink.hpp`:

| Name | What it does |
| --- | --- |
| `LogSink` | The callable that receives each record |
| `set_log_sink(sink)` | Replaces the sink of the process |

Everything in `Detail` belongs to the macros.

## Level

The level of the process is `Warn` unless something sets it. A record passes when its level is at
or below the process level, so `Warn` passes errors and warnings and drops the `Info` lines that
`Runtime::create()` logs for each device.

`SR_LOG_LEVEL` in the environment sets the initial level. It takes `off`, `error`, `warn`, `info`
or `debug` in any case. Any other value is reported once on stderr and the level stays `Warn`. The
variable is read at the first log call that passes the gate or the first `log_level()` call, and
it is not read at all when `set_log_level()` runs before either.

A log call goes through two gates.

1. `K_LOG_LEVEL_CEILING` is fixed per translation unit: `Off` when `SR_ENABLE_LOGGING` is defined
   as 0 before the header is included, `Info` under `NDEBUG`, `Debug` otherwise. A call above it
   compiles to nothing.
2. The process level is one `std::atomic<int>` exported from the library. A call above it costs one
   relaxed load and a branch, and its arguments are not evaluated.

The atomic starts at a value above every level, which marks the environment as unread. A call that
passes the load then asks `Detail::is_log_enabled`, which reads the variable once and answers for
the real level. That second question is asked only on calls that are about to format a record.

## Format

`Detail::sr_format` replaces each `{}` with the next argument, written through `operator<<`, and
writes `{{` and `}}` as single braces. It takes no format specification; a call site that needs a
width or hex formats the value first. Extra arguments and extra placeholders are ignored.

The format string travels inside `__VA_ARGS__`, so the macros take `(...)` and need neither
`__VA_OPT__` nor `##__VA_ARGS__`, which C++17 does not have.

## Record and sink

A record is one line: `[HH:MM:SS.mmm] [LEVEL] [file:line] [function] message`, in local time, with
no line break. The sink receives the level and that line as a `std::string_view` that is valid
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

`tests/test_logger.cpp` builds `sr_logging_tests`; `tests/README.md` describes it.
`tests/integration/test_logging_defaults.cpp` checks the level and the sink around
`Runtime::create()`.
