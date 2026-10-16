# log

SushiRuntime's names for SushiLog, the logger every Sushi library links. The module holds no
logger of its own: each header names a SushiLog type or call in `SushiRuntime`, so a program
writes `SushiRuntime::set_log_sink` and reaches the one logger of the process. The module is
header-only and its headers are installed.

It depends on the C++ standard library and on SushiLog (`SushiLog::SushiLog`), which its public
headers include, so every consumer of the library links it too.
`cli/sushistack.deps.toml` pins the SushiLog release; the CLI hands its checkout to CMake as
`SUSHILOG_DIR`.

## Public entry points

| Header | Names in `SushiRuntime` |
| --- | --- |
| `log_level.hpp` | `LogLevel`, `Trace` = 0 to `Fatal` = 5, then `Off` = 6 |
| `log_project.hpp` | `LogProject`, `set_log_level(project, level)`, `log_level(project)` |
| `sushiruntime_log_project.hpp` | `K_SUSHIRUNTIME_LOG_PROJECT`, `{"sushiruntime", "SR_LOG_LEVEL"}`, the project of every runtime module |
| `log_category.hpp` | `LogCategory`, `make_log_category(project, module)`, `set_log_level(category, level)`, `log_level(category)` |
| `log_record.hpp` | `LogRecord`: level, project, module, file, line, thread, `time_ns`, message and the whole line |
| `log_sink.hpp` | `LogSink`, `set_log_sink(sink)`, `set_log_synchronous(bool)`, `log_forward_sink()` |
| `log_format.hpp` | `Detail::sr_format`, which formats a message as every `SR_LOG_*` call does |
| `log_c_sink.h` | `sr_set_log_sink(function, user)` and `sr_set_log_synchronous(int)`, which call `sl_set_log_sink` and `sl_set_log_synchronous` |

Each type is SushiLog's, under a `using` declaration, and each call is SushiLog's function. What
they do, the record format, the queue, the threads and the C record are described in the logging guide of SushiLog's
repository.

A project built on SushiRuntime logs through SushiLog's own front, `SL_LOG_FRONT_AT`, and links
SushiLog directly.

## Tests

`tests/foundation/log/` checks that each name is SushiLog's, that the runtime's project reads
`SR_LOG_LEVEL`, that `sr_format` formats as the log calls do, and that the `sr_` C calls install
a C sink.
