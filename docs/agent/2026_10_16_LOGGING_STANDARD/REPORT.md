# Logging standard: report

The work of `PLAN.md`, run inline on `feature/logging_standard`, plus the one interface every Sushi
logger offers, fixed in SushiSkills `skills/logging/INTERFACE.md` (8be3591, with the two
amendments of 001f907).

## Done

| Commit | What it holds |
| --- | --- |
| `bd25623` | `LogLevel` renumbered Trace 0 to Off 6; a category per module with a level of its own |
| `8b3dde5` | Every record of nine modules logged under its module's category |
| `2d1a1dc` | `LogProject`, a `LogRecord` sink, `log_forward_sink()`, the C sink, `SR_LOG_FRONT_AT` |

The interface in `2d1a1dc`:

- A category is a project and a module; a record shows `[sushiruntime.memory]`.
- Each project's variable sets only its own categories: `SR_LOG_LEVEL` for the runtime.
- `set_log_level` and `log_level` take a project or a category. The overloads without a project
  are gone.
- The sink receives a `LogRecord`: level, project, module, file, line, thread, `time_ns`,
  message and the whole line.
- `log_forward_sink()` takes another Sushi logger's records into this queue. They pass no level
  here, keep their time and call site, and take this logger's thread number and line.
- `log/log_c_sink.h`: `SushiLogRecord` with its size first, `sr_set_log_sink`,
  `sr_set_log_synchronous`.
- `log/log_front.hpp`: `SR_LOG_FRONT_AT(floor, level, category, ...)` and
  `log_level_floor(enabled)`, a `static constexpr` function so each translation unit reads its
  own `NDEBUG`.

Two points of the first draft of `INTERFACE.md` changed after SushiSkills accepted them (001f907):

- A sender always fills `thread` and `text`, and the receiver ignores both.
- The compile-time floor is a `static constexpr` function, never an inline one.

## Verification

- `sr build --distributed --jobs 3`: passed.
- `sr test --distributed --jobs 1`: 1050 tests, 0 failed. Three were skipped by their own
  conditions on this machine: `DiamondAllKernelNodes`,
  `ImportOnAQueueWithoutTheExtensionReturnsUnsupported` and
  `ProfiledReuseTicksDoNotAllocatePerTick`.
- `tools/check_all.py`: clean on 9 checkers.

## Consumers

The break touches the names that change: the sink signature, the project-less
`set_log_level(level)` and `log_level()`, and `make_log_category(name)`.

| Repository | Uses today | Needs |
| --- | --- | --- |
| SushiBLAS | `set_log_synchronous(true)` in its test runner, which is unchanged; its own `SushiBLAS/core/logger.hpp` | Its logger rebuilt as a front: `K_SUSHIBLAS_LOG_PROJECT`, `SB_LOG_*` on `SR_LOG_FRONT_AT` |
| SushiAI | `set_log_synchronous(true)` in its engine fixture, which is unchanged | Its `SA_LOG_*` front on `SR_LOG_FRONT_AT` |
| SushiEngine | Its own logging module and categories, not SushiRuntime's | Nothing from this change; a front when its owner chooses |

Each consumer moves its SushiRuntime pin to the commit that holds `2d1a1dc` when it builds its
front.

## Not done

- No consumer was changed here. SushiAI and SushiEngine belong to their own sessions.
- The SushiBLAS front is its own task.
