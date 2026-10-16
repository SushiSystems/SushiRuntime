# Logging standard: plan

**Status:** planned on 2026-10-16; in progress, inline.

The reference is SushiSkills `skills/logging/SKILL.md` at `d457c23`, not the current logger.
`tools/logs/check_logging.py` reports, on this tree: 182 calls with no category, 9 modules that
log and have no category header, and 1 empty `catch`.

## Acceptance for the whole

`python tools/logs/check_logging.py .` reports nothing, and the logging tests cover the level of
each category, the drop report of the bounded queue and the order of a `FATAL`.

## Where the logger stands

- `foundation/log` holds the public `LogLevel` (`Off`, `Error`, `Warn`, `Info`, `Debug`, where a
  larger value is more verbose) and the sink type. `foundation/logging` holds `logger.hpp`,
  `log_gate.hpp` and `logger.cpp`, 728 lines in all.
- `SR_LOG_<LEVEL>(format, args...)` takes no category, and the record carries `__func__`.
- One level for the whole process; the queue in `logger.cpp` is a `std::deque` with no bound.

## Tasks, in hierarchy order

Each task has one acceptance criterion and one build.

1. **Levels.** `LogLevel` gains `Trace` and `Fatal`. `FATAL` and `ERROR` are never compiled out;
   the ceiling is `DEBUG` under `NDEBUG` and `TRACE` otherwise.
   *Accepted when* the `log_level` and `log_gate` tests cover the seven levels and both ceilings.
2. **Categories and their levels.** A `LogCategory` type and `make_log_category(name)`; one atomic
   level per category, `WARN` by default; `set_log_level(level)` and
   `set_log_level(category, level)`; `SR_LOG_LEVEL=warn,distributed=debug` read at the first call,
   a value it cannot read reported once on stderr.
   *Accepted when* tests set and read the level of two categories apart, by call and by the
   environment, and an unreadable value leaves both at `WARN`.
3. **Record.** `[time] [LEVEL] [category] [tN] [file:line] message`: the category and a small
   thread number join, the function name leaves.
   *Accepted when* a test reads every field of one record from a capturing sink.
4. **Bounded queue and FATAL.** The queue holds a fixed number of records; a record that finds it
   full is dropped and counted, and the next record delivered is preceded by a `WARN` with the
   count. `FATAL` drains the queue, delivers on the calling thread and flushes before the process
   ends.
   *Accepted when* a test fills the queue behind a blocked sink and reads the drop `WARN`, and a
   death test reads the queued records before the `FATAL` one.
5. **Calls.** `SR_LOG_<LEVEL>(category, format, args...)`. Each of the nine modules that logs gets
   `<module>_log_category.hpp` with `K_<MODULE>_LOG_CATEGORY`, and all 182 calls take their
   module's category. The macro change and the calls land together, because the old form stops
   compiling.
   *Accepted when* `check_logging.py --rule rule_uncategorized_call --rule
   rule_missing_category_header` reports nothing and the suite passes.
6. **The empty catch** in `platform/topology/source/hw_discovery.cpp` logs what it caught and what
   it does next.
   *Accepted when* `check_logging.py` reports nothing.
7. **Documentation.** The logging guide, the module READMEs of `log` and `logging`, the changelog.

The shared tools from SushiSkills `d457c23` and the settings they need (`K_LOG_PREFIX`,
`K_CONSOLE_PATHS`, `K_PRIVATE_DEPENDS_ALSO`) land with task 1, on this work's branch, since
`check_all` stays red until task 6.

## Owner decisions, 2026-10-16

- The plan runs inline.
- `LogLevel` is renumbered in the standard's order, so a larger value is more severe: `Trace`,
  `Debug`, `Info`, `Warn`, `Error`, `Fatal`, then `Off`. This breaks a host that stores or
  compares the numbers, and lands as a breaking change.
- A build runs only when the owner says so; the code of several tasks may wait for one build.
