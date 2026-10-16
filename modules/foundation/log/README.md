# log

The controls of the runtime's logger that a program calls, and all a project needs to log
through it with macros of its own: the projects and their categories, the level of each, the
record a sink receives, the sink, and a C sink for a host outside C++. The module is
header-only and its headers are installed.

It depends on the C++ standard library and on `core` (`export.hpp`, `delegate.hpp`).

## Public entry points

`log_level.hpp`:

| Name | What it does |
| --- | --- |
| `LogLevel` | `Trace` = 0 to `Fatal` = 5, then `Off` = 6, in one byte; the numbers are part of the C interface and never change |

`log_project.hpp` and `sushiruntime_log_project.hpp`:

| Name | What it does |
| --- | --- |
| `LogProject` | A project's name and the environment variable that sets its levels |
| `set_log_level(project, level)`, `log_level(project)` | Set and read the level of every category of one project |
| `K_SUSHIRUNTIME_LOG_PROJECT` | `{"sushiruntime", "SR_LOG_LEVEL"}`, the project of every runtime module |

`log_category.hpp`:

| Name | What it does |
| --- | --- |
| `LogCategory`, `make_log_category(project, module)` | Name the project and module a record comes from |
| `set_log_level(category, level)`, `log_level(category)` | Set and read the level of one category |

`log_record.hpp` and `log_sink.hpp`:

| Name | What it does |
| --- | --- |
| `LogRecord` | Level, project, module, file, line, thread, `time_ns`, message and the whole line, each apart |
| `LogSink` | `Delegate<void(const LogRecord&), 48>`, the callable that receives each record |
| `set_log_sink(sink)` | Replaces the sink of the process |
| `set_log_synchronous(bool)` | Chooses which thread delivers a record to the sink |
| `log_forward_sink()` | Returns a sink that enters another logger's records into this one's queue |

`log_front.hpp` and `log_format.hpp`:

| Name | What it does |
| --- | --- |
| `SR_LOG_FRONT_AT(floor, level, category, ...)` | Logs one record past a front's compile-time floor and its category's level |
| `log_level_floor(enabled)` | `Error` when `enabled` is false, `Debug` under `NDEBUG`, `Trace` otherwise, read in the caller's translation unit |
| `Detail::sr_format` | Replaces each `{}` with the next argument |

`log_c_sink.h`, which a C compiler reads too:

| Name | What it does |
| --- | --- |
| `SushiLogRecord` | The record in C: its `size` first, each string as a pointer and a length |
| `SushiLogSinkFunction` | `void (*)(const SushiLogRecord*, void* user)` |
| `sr_set_log_sink(function, user)` | Replaces the sink; a null function restores stderr |
| `sr_set_log_synchronous(int)` | `set_log_synchronous` for C |

Every C++ name is declared in `SushiRuntime`. The module declares the functions and `logging`
defines them, in `modules/foundation/logging/source/logger.cpp`, beside the state they change.
`modules/foundation/logging/README.md` describes the levels, the record format, the sink, its
queue and its threads.

## A project that logs through SushiRuntime

A project built on SushiRuntime declares its project once, a category per module, and its
macros on `SR_LOG_FRONT_AT`. It adds no queue, thread or sink.

```cpp
inline constexpr SushiRuntime::LogProject K_SUSHIBLAS_LOG_PROJECT{"sushiblas", "SB_LOG_LEVEL"};
inline constexpr SushiRuntime::LogCategory K_BLAS_LOG_CATEGORY =
    SushiRuntime::make_log_category(K_SUSHIBLAS_LOG_PROJECT, "blas");

#define SB_LOG_FLOOR SushiRuntime::log_level_floor(SB_ENABLE_LOGGING != 0)
#define SB_LOG_WARN(category, ...) \
    SR_LOG_FRONT_AT(SB_LOG_FLOOR, SushiRuntime::LogLevel::Warn, category, __VA_ARGS__)
```

Source: not compiled

`tests/foundation/log/test_log_front.cpp` builds a front this way.

## Another logger in the process

A second Sushi logger, such as SushiDSP's, joins at start-up by forwarding into this one, so the
process keeps one queue, one thread and one sink:

```cpp
SushiDSP::set_log_synchronous(true);
SushiDSP::set_log_sink(SushiRuntime::log_forward_sink());
```

Source: not compiled

A forwarded record passes none of this logger's levels, because the sender has applied its own.
It keeps its level, project, module, file, line and time; it takes this logger's number for the
calling thread, and this logger writes its line.

## The C sink

A `SushiLogRecord` is valid only during the call, and no string in it ends with a NUL. A later
version only adds fields at the end, so a host copies `min(record->size, sizeof its own struct)`
bytes and reads the fields both cover. The function must not throw or unwind into the logger.

## Tests

`tests/foundation/log/` tests the numbers of the levels, projects, categories, the record a sink
receives and a front's macro; `tests/foundation/logging/` covers the functions with the logger
they drive, forwarding and the C sink.
