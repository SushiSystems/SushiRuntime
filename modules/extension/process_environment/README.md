# process_environment

The module reads what a worker process receives from outside: its environment variables, and the
stop signals that end it in order. It is an internal module: its headers reach the modules that name
it and the tests, and are never installed. It is built with the distributed layer, under
`SR_ENABLE_DISTRIBUTED`, and every name it declares is in `SushiRuntime::Distributed`.

It depends on `core`.

## Public entry points

| Header | Holds |
| --- | --- |
| `environment.hpp` | `read_environment()` |
| `stop_signal.hpp` | `StopSignal` |
