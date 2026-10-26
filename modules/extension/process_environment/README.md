# process_environment

The module reads what a worker process receives from outside: its environment variables, and the
stop signals that end it in order. It is an internal module: its headers reach the modules that name
it and the tests, and are never installed. It is built with the distributed layer, under
`SR_ENABLE_DISTRIBUTED`, and every name it declares is in `SushiRuntime::Distributed`.

It depends on `core` and `log`.

## Public entry points

| Header | Holds |
| --- | --- |
| `environment.hpp` | `read_environment()` |
| `stop_signal.hpp` | `StopSignal` |
| `process_environment_log_category.hpp` | `K_PROCESS_ENVIRONMENT_LOG_CATEGORY`, the module's category, `sushiruntime.process_environment` |

## What the module logs

`StopSignal` installs its handler for each stop signal through `signal()`. When the C runtime
answers `SIG_ERR`, the constructor logs one `WARN` with the signal number and `errno`, and the
worker has no handler for that signal, so it will not stop on it. A second constructor takes the
installing function, so a test makes the refusal happen. The destructor restores only the
handlers that were installed.
