# logging

The `SR_LOG_*` macros, SushiRuntime's front over SushiLog. Every record goes to SushiLog, the one
logger of the process, under the runtime's project, `sushiruntime`. The module is internal and
header-only: its headers are for the other modules of the library, and they are not installed.

It depends on the C++ standard library and on `log`, which brings SushiLog.

## Public entry points

`logger.hpp`, which includes `log_gate.hpp` and the headers of `log`:

| Name | What it does |
| --- | --- |
| `SR_LOG_TRACE`, `SR_LOG_DEBUG`, `SR_LOG_INFO` | Log one record in a category; each `{}` takes the next argument |
| `SR_LOG_WARN`, `SR_LOG_ERROR` | The same, at the two levels that report a fallback or a failure |
| `SR_LOG_FATAL` | Logs after every queued record and flushes; the caller then ends the process |
| `SR_LOGGER_SET_SYNC_MODE(mode)` | Forwards to `set_log_synchronous` |

`log_gate.hpp`:

| Name | What it does |
| --- | --- |
| `K_LOG_LEVEL_FLOOR` | `SushiLog::log_level_ceiling(SR_ENABLE_LOGGING != 0)`, the least severe level a translation unit compiles calls for |
| `Detail::is_log_enabled` | Whether a level passes a category's run-time gate |

The `SR_LOG_*` macros are SushiLog's `SL_LOG_FRONT_AT` with `K_LOG_LEVEL_FLOOR`. Everything in
`Detail` belongs to the macros.

## Categories

Each module that logs declares one category, named after its folder, in
`<module>_log_category.hpp`: `K_DISTRIBUTED_LOG_CATEGORY` is
`make_log_category(K_SUSHIRUNTIME_LOG_PROJECT, "distributed")`. A call names it first:
`SR_LOG_WARN(K_DISTRIBUTED_LOG_CATEGORY, "peer {} lost", id)`. A module logs only under its own
category, and `tools/logs/check_logging.py` reports a call without one.

## Level

Every category is at `Warn` unless something sets it, so `Runtime::create()` writes nothing at
the default and logs a line per device at `Info`. `SR_LOG_LEVEL` sets the runtime's levels and no
other project's: a level for every module, then any number of `module=level` pairs, such as
`SR_LOG_LEVEL=warn,distributed=debug`. In code, `set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, level)`
sets every module and `set_log_level(category, level)` sets one.

A call goes through two gates.

1. `K_LOG_LEVEL_FLOOR` is fixed per translation unit: `Error` when `SR_ENABLE_LOGGING` is defined
   as 0 before the header is included, `Debug` under `NDEBUG`, `Trace` otherwise. A call below it
   compiles to nothing; `ERROR` and `FATAL` are never compiled out.
2. The category's level in SushiLog. A call below it costs one relaxed load and a branch, and its
   arguments are not evaluated.

## Format

`Detail::sr_format`, in `log/log_format.hpp`, replaces each `{}` with the next argument, written
through `operator<<`, and writes `{{` and `}}` as single braces, as SushiLog formats a record. A
call site that needs a width or hex formats the value first.

## Records, sinks and threads

The record, its line, the sink, the queue, `FATAL`, synchronous mode and what happens at exit are
SushiLog's, and the logging guide of SushiLog's repository describes them. A runtime record's line
reads `[12:04:31.207] [WARN] [sushiruntime.memory] [t3] [usm_allocator.cpp:88] message`.

## Tests

`tests/foundation/logging/` builds `sr_logging_tests`; its `README.md` describes it.
`tests/surface/api/runtime/test_logging.cpp` checks the level and the sink around
`Runtime::create()`.
