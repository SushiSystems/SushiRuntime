# report

The module holds what a run reports about itself: the `RunReport` a run verb returns and the
records it carries. It is header-only, and its headers are installed.

It depends on the C++ standard library and on `core`, for `OperationID`.

## Public entry points

| Header | Holds |
| --- | --- |
| `run_report.hpp` | `RunReport`, `NodeTiming`, `WorkerTiming`, `NodeEventRecord`, `ClockPairing`, `ExecutionLayer`, `NUMANodeStatistics` and `DistributedStatistics` |
| `dispatch_width.hpp` | `K_WIDTH_UNKNOWN`, the marker of a dispatch with no element count |

Every name is declared in `SushiRuntime`. The engine fills these types and a consumer reads them,
so the module sits in the foundation tier, below both.

## run_report.hpp

### NodeEventRecord

There is one NodeEventRecord per node dispatch, never one per SYCL command group a host-work node
submits internally. `invocation` is the step index within `run(steps)`, zero for a single run.
`node_index` is the node's public registration index, so several internal nodes one `add()` call
created share it and the pair may repeat within a run.

### reset

reset() is what makes a report reusable. Assigning a fresh RunReport over an old one would free
every vector's storage and the next run would allocate it again; clearing keeps the buffers, so a
report handed to `run(RunReport&)` every tick stops allocating after the first run.

The retained capacity covers the rows, not what a row owns: clearing node_timings destroys each
row's name string. That is why the zero-allocation claim is stated for a steady state with profiling
off. With profiling on, the node labels are rebuilt each run by design.

## Tests

`tests/foundation/report/` builds `sr_report_tests`.
