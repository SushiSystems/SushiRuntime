# observability

The module holds what a run reports about itself. It is header-only.

It depends on the C++ standard library and the SYCL headers, and on no other module of the
repository.

## Public entry points

| Header | Holds |
| --- | --- |
| `run_report.hpp` | `RunReport` and the statistics records it carries |
| `profiling_ledger.hpp` | `ProfilingLedger`, the per-plan store of node event records |
| `event_timestamps.hpp` | `EventTimestamps` and the function that reads them from a SYCL event |
| `dispatch_width.hpp` | `WIDTH_UNKNOWN`, the marker of a dispatch with no element count |
| `transfer_counters.hpp` | `TransferCounters`, the byte counters of explicit host and device transfers |

Everything is in `SushiRuntime::Internal::Observability`. `run_report.hpp` also promotes `RunReport` into
`SushiRuntime`.

## event_timestamps.hpp

### read_event_timestamps

A backend with no start/end reading leaves the dispatch unrecorded; one with no submit reading
degrades to submit_ns == start_ns.

## profiling_ledger.hpp

### ProfilingLedger

A ProfilingLedger is owned by CompiledGraph and reused across runs. Each row is sized to its own
node's expected dispatch count times how many internal nodes share its registration_index.
reserve() grows it outside the poller, before a run's dispatch loop starts, so claim_slot() and
write() never allocate. The ledger also carries the run's clock pairing claim.

### reserve

reserve() sizes row i to expected_invocations * multiplicities[i], or to the row's own last observed
demand if that was larger. An overflowing row therefore grows on the very next call, and a shared
registration_index gets a row big enough for every node behind it.

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

`tests/test_node_event_record.cpp` and `tests/test_transfer_counters.cpp` build
`sr_observability_tests`.
