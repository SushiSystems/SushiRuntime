# report

The module holds what a run reports about itself: the `RunReport` a run verb returns and the
records it carries. It is header-only, and its headers are installed.

It depends on the C++ standard library and on `core`, for `OperationID` and `NodeCost`.

## Public entry points

| Header | Holds |
| --- | --- |
| `run_report.hpp` | `RunReport`, `ExecutionLayer`, `NUMANodeStatistics` and `DistributedStatistics` |
| `node_event_record.hpp` | `NodeEventRecord` and `K_HOST_DEVICE_INDEX` |
| `clock_pairing.hpp` | `ClockPairing` and `DeviceClock`, which place a device's event clock on the steady clock |
| `node_table.hpp` | `NodeTable` and `NodeTableRow`, the names, operations and costs of a plan's nodes by registration index |
| `drop_counts.hpp` | `DropCause` and `DropCounts`, how many dispatches a run left out of its timing |
| `run_observer.hpp` | `RunObserver`, the interface that receives a run's timing, `RunObservation`, what it receives, and `RunObserverHandle`, which removes an observer |
| `dispatch_width.hpp` | `K_WIDTH_UNKNOWN`, the marker of a dispatch with no element count |

Every name is declared in `SushiRuntime`. The engine fills these types and a consumer reads them,
so the module sits in the foundation tier, below both.

## run_report.hpp

`RunReport` holds the outcome of a run: success, cancellation, the task count, the execution layer,
the NUMA and distributed counts. It holds no timing. A run's timing leaves the runtime through a
`RunObserver`, and the profiler's zones, values and counters carry the rest.

### reset

reset() is what makes a report reusable. Assigning a fresh RunReport over an old one would free
every vector's storage and the next run would allocate it again; clearing keeps the buffers, so a
report handed to `run(RunReport&)` every tick stops allocating after the first run.

## run_observer.hpp

A runtime keeps any number of `RunObserver`s, added with `Runtime::add_run_observer()`. A runtime records
device timing while it has at least one. `on_run()` receives one `RunObservation` for every run that finished
without a fault while the observer is added, on the thread that learned the run finished: the thread that
ended a blocking run, or the worker that reaped the last node of an asynchronous step. A run of a plan built
before the observer was added reports with no records. `on_run()` must not throw, start a run, or add or remove an
observer.

`RunObserverHandle` is move-only and removes the observer it stands for when it is destroyed or `reset()`. It
holds the runtime weakly, so a handle that outlives its runtime removes nothing.

### RunObservation

| Member | Meaning |
| --- | --- |
| `layer` | Path that submitted the run's device work |
| `table` | The plan's `NodeTable`, shared by every run of the plan |
| `events` | One `NodeEventRecord` per dispatch |
| `device_clocks` | One `ClockPairing` per device that ran a kernel of the run |
| `drops` | The `DropCounts` of the run |
| `graph_id` | The id of the graph that owns the run's plan; 0 when none is known |

The observation is valid for the duration of `on_run()`; an observer that keeps a part copies it.

### NodeEventRecord

There is one NodeEventRecord per node dispatch, never one per SYCL command group a host-work node
submits internally; a body that submits several kernels hands each event over, and the record then spans them
all. `invocation` is the step index within `run(steps)`, zero for a single run.
`node_index` is the node's public registration index, so several internal nodes one `add()` call
created share it and the pair may repeat within a run. `device_index` names the device whose queue
ran the dispatch, or `K_HOST_DEVICE_INDEX` for a node that ran on a worker thread with no queue.
`submit_thread` is the profiler's index of the submitting thread, 0 when unknown. A record of a
device is on that device's event clock, which its `DeviceClock` pairs with the steady clock; a host
record is on the steady clock. The record is 48 bytes and its first seven fields keep their order, since
SushiEngine mirrors the layout.

### NodeTable

One row per registration index: name, `origin` (the caller's own operation the node belongs to, empty when
none), operation, optional `NodeCost`, whether the node is host-native and how many internal nodes share the
index. The table also holds the `label` the graph had when the plan was built and the graph's `graph_id()`,
so spans of two graphs of one runtime can be told apart. The name and the operation are the first node's of that index. A plan
builds the table once and every observation of its runs shares it.

### DropCounts

A dispatch that leaves no record is counted under one cause: an unreadable event, a full ledger row, a node
past the table, a kernel with no event, or no device queue. `first_node` is the registration index of the first
one lost. A run that lost none has all counts zero.

## Tests

`tests/foundation/report/` builds `sr_report_tests`.

## Designs

<!-- designs:begin -->
- [PROFILING](../../../docs/design/PROFILING/README.md): How SushiRuntime reports its own time to SushiProfiler and to any observer, and which timing leaves its public report.
<!-- designs:end -->
