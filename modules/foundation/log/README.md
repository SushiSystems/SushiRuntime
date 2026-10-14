# log

The controls of the runtime's logger that a program calls: the level of the process and the sink
that receives each record. The module is header-only and its headers are installed.

It depends on the C++ standard library and on `core` (`export.hpp`, `delegate.hpp`).

## Public entry points

`log_level.hpp`:

| Name | What it does |
| --- | --- |
| `LogLevel` | `Off`, `Error`, `Warn`, `Info`, `Debug` |
| `set_log_level(level)`, `log_level()` | Set and read the level of the process |

`log_sink.hpp`:

| Name | What it does |
| --- | --- |
| `LogSink` | The callable that receives each record |
| `set_log_sink(sink)` | Replaces the sink of the process |
| `set_log_synchronous(bool)` | Chooses which thread delivers a record to the sink |

Every name is declared in `SushiRuntime`. The module declares the functions and `logging` defines
them, in `modules/foundation/logging/source/logger.cpp`, beside the state they change.
`modules/foundation/logging/README.md` describes the level, the record format, the sink and its
threads.

## Tests

`tests/foundation/logging/test_logger.cpp` covers the four functions with the logger they
drive.
