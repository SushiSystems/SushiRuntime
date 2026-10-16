# log

The controls of the runtime's logger that a program calls: the categories, the level of each one,
and the sink that receives each record. The module is header-only and its headers are installed.

It depends on the C++ standard library and on `core` (`export.hpp`, `delegate.hpp`).

## Public entry points

`log_level.hpp`:

| Name | What it does |
| --- | --- |
| `LogLevel` | `Trace`, `Debug`, `Info`, `Warn`, `Error`, `Fatal`, least severe first, then `Off` |
| `set_log_level(level)`, `log_level()` | Set the level of every category, and read the level a category takes when nothing sets its own |

`log_category.hpp`:

| Name | What it does |
| --- | --- |
| `LogCategory`, `make_log_category(name)` | Name the module a record comes from |
| `set_log_level(category, level)`, `log_level(category)` | Set and read the level of one category |

`log_sink.hpp`:

| Name | What it does |
| --- | --- |
| `LogSink` | The callable that receives each record |
| `set_log_sink(sink)` | Replaces the sink of the process |
| `set_log_synchronous(bool)` | Chooses which thread delivers a record to the sink |

Every name is declared in `SushiRuntime`. The module declares the functions and `logging` defines
them, in `modules/foundation/logging/source/logger.cpp`, beside the state they change.
`modules/foundation/logging/README.md` describes the levels, the record format, the sink, its queue
and its threads.

## Tests

`tests/foundation/log/` tests the order of the levels and the level each category keeps;
`tests/foundation/logging/test_logger.cpp` covers the functions with the logger they drive.
