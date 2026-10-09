# logging

The module holds the runtime's logger: one header, `logger.hpp`.

It depends on the C++ standard library only, and no other module of the repository.

## Public entry points

| Name | What it does |
| --- | --- |
| `SR_LOG_ERROR`, `SR_LOG_WARN`, `SR_LOG_INFO`, `SR_LOG_DEBUG` | Log one message; each `{}` in the format takes the next argument |
| `SR_THROW_IF(condition, ...)` | Logs the message and throws when the condition holds |
| `SR_LOGGER_SET_SYNC_MODE(mode)` | Switches between the asynchronous writer and the synchronous one |
| `LogType` | The level of a message |

A message goes to a worker thread by default, so a log call on a pool worker does not wait on
the stream. The synchronous mode writes under a mutex before the call returns; tests use it to
read what was logged.

`SR_ENABLE_LOGGING` set to 0 before the header is included turns the four macros into no-ops.

## Tests

`tests/test_logger.cpp` builds `sr_logging_tests`.
