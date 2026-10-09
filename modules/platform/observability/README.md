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

The first four were `core/` headers in v0.3; their old include paths forward here until
v0.5.0 (`cmake/SushiRuntimeMovedHeaders.cmake`).

## Tests

`tests/test_node_event_record.cpp` and `tests/test_transfer_counters.cpp` build
`sr_observability_tests`.
