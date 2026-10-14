# measurement

The module holds the measurement the benchmarks share: a repeated timed run reduced to its median,
minimum and maximum, the report line that carries the result, and the signal that ends a timed
region. It is an internal module built only with the tests: its headers are never installed and the
library does not link it.

It depends on the C++ standard library and on no other module of the repository.

## Public entry points

| Header | Holds |
| --- | --- |
| `measurement.hpp` | `RunStatistics`, `MeasurementLabel`, `summarize()`, `measure()`, `format_measurement()` and `print_measurement()` |
| `completion_signal.hpp` | `CompletionSignal` |

Every name is declared in `SushiRuntime::Test`.

## measurement.hpp

`measure` calls a callable that times its own region and returns one figure, five times by
default, and returns the median, the minimum and the maximum. `print_measurement` writes one
line per case that starts with `SR_MEASUREMENT` and holds `key=value` pairs: benchmark, case,
unit, median, min, max, runs, build, workers and size. A before-and-after comparison greps
those lines out of the benchmark output.

## completion_signal.hpp

`CompletionSignal` closes a timed region: the task that finishes the work raises it and the
test waits on it with a budget, so no polling sleep sits inside a measurement. A region that
ends on the signal includes one thread wake-up.
