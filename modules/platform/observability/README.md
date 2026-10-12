# observability

The module holds the instruments a run is measured with: the profiling ledger, the event
timestamps and the transfer counters. It is header-only.
The module is internal: its headers are for the other modules of the library and are not installed.

It depends on the C++ standard library, the SYCL headers and `report`.

## Public entry points

| Header | Holds |
| --- | --- |
| `profiling_ledger.hpp` | `ProfilingLedger`, the per-plan store of node event records |
| `event_timestamps.hpp` | `EventTimestamps` and the function that reads them from a SYCL event |
| `transfer_counters.hpp` | `TransferCounters`, the byte counters of explicit host and device transfers |

Everything is in `SushiRuntime::Internal::Observability`. `ProfilingLedger::collect` fills a
`RunReport`, which the `report` module defines.

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

## Tests

`tests/test_profiling_ledger.cpp` and `tests/test_transfer_counters.cpp` build
`sr_observability_tests`.
