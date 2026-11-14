# observability

The module holds the instruments a run is measured with: the profiling ledger, the event
timestamps and the transfer counters. It is header-only.
The module is internal: its headers are for the other modules of the library and are not installed.

It depends on the C++ standard library, the SYCL headers and `report`.

## Public entry points

| Header | Holds |
| --- | --- |
| `profiling_ledger.hpp` | `ProfilingLedger`, the per-plan store of node event records, `DispatchFacts` and `DispatchTimes` |
| `offset_bounds.hpp` | `DispatchWindow`, `OffsetBounds` and the functions that bound a device's clock offset and turn the bounds into a `ClockPairing` |
| `steady_time.hpp` | `steady_ns()` and `steady_now_ns()`, the steady readings a record and a pairing are made of |
| `event_timestamps.hpp` | `EventTimestamps`, the function that reads them from a SYCL event, and the ones that merge the events of one dispatch |
| `transfer_counters.hpp` | `TransferCounters`, the byte counters of explicit host and device transfers |

Everything is in `SushiRuntime::Internal::Observability`. `ProfilingLedger::collect` appends to the
`events` of a `RunObservation`, which the `report` module defines, and `collect_pairings` appends the
device clocks.

## event_timestamps.hpp

### read_event_timestamps

A backend with no start/end reading leaves the dispatch unrecorded. `command_submit` is not read:
DPC++ estimates it on the host from a device-host base it takes once and never refreshes, so it runs
about 20 ms early on the device clock (KI-31). A dispatch's submit time is the steady reading the
submitting thread took before the submit call.

### read_dispatch_timestamps

A task body that submits several kernels hands every event over with `note_dispatch_event()`.
read_dispatch_timestamps() reads the timestamps of the dispatch across them: the earliest start and
the latest end, through merge_timestamps(). When the body handed over none, the event it returned
is read alone. One event that cannot be read fails the whole dispatch, which the ledger counts as an
unreadable event.

## profiling_ledger.hpp

### ProfilingLedger

A ProfilingLedger is owned by CompiledGraph and reused across runs. Each row is sized to its own
node's expected dispatch count times how many internal nodes share its registration_index.
reserve() grows it outside the poller, before a run's dispatch loop starts, so claim_slot() and
write() never allocate. The ledger also carries the clock offset bounds of each device.

### record

record() is the one writer of a node event record. The poller, the in-order chain and the host path
each pass the node, the step, the widths, the device, the submitting thread and the three
timestamps, and the ledger fills every field of the record itself, so a new field is one edit. The
submit time is a steady reading on every path. The host path passes `K_HOST_DEVICE_INDEX` and
steady-clock start and end; a device path passes its device's index and event-clock start and end.

### Clock pairings

Every dispatch whose start and end were read narrows its device's pairing, through narrow_pairing().
Its kernel cannot start before the host began to submit it, and it ended before the reaper saw it
complete, so the offset that takes the device's clock to the steady clock lies in
`[submit_steady_ns - start_ns, reap_steady_ns - end_ns]` (`bounds_of()`, `offset_bounds.hpp`). Each dispatch's
range holds on its own, so the device keeps the larger low and the smaller high across the run's
dispatches, folded with compare-exchanges as the reapers reach them. The scheduler's poller takes the
reap reading when it sees the dispatch complete; the chain run takes one after its single wait.

`pairing_from()` turns the bounds into a `ClockPairing`: its offset is their midpoint and its
uncertainty half their width, rounded up, plus the drift the two clocks can make across the device's
dispatches, so the true steady time of any converted reading lies within the uncertainty of it.

The two clocks drift apart: on the RTX 3080 Ti of the owner's machine, CUDA's event clock gained
33.5 ppm on the host's steady clock, about 2 ms a minute, and DPC++ never takes a new base. Each
dispatch's bounds hold for the offset at its own time, so bounds kept across minutes would exclude
the offset of now. The ledger therefore keeps bounds for one run only, and `reserve()` widens them
again for the next. Within a run, the offset moves by at most `K_MAX_CLOCK_DRIFT_PPM` (100 ppm, three
times the measured rate) of the span from the device's first submit to its last reap; `drift_over()`
adds that to the uncertainty, which covers the crossing the drift makes in the bounds of a long run. A cold first dispatch alone gives a wide range, and its own span still lies
inside it; any later dispatch of the run can only narrow it. Bounds that crossed, which only a
quantised timestamp makes (KI-32), keep the midpoint and half the gap. The pairing's event reading is
the earliest start the device's dispatches read.

The bounds are per device, since two devices may not share an event clock. collect_pairings() appends a
`DeviceClock` for each device a dispatch narrowed; a device that ran no device submission, or whose events
could not be read, has none, and its spans cannot be placed on the steady clock.

### Dropped dispatches

A dispatch that leaves no record is counted under one of five causes (`DropCause`): an unreadable
event, a full row, a node past the table, a kernel with no event, or a dispatch that found no device
queue. The first node that lost one is kept. drops() reads the counts, reserve() clears them, and the
behaviour is the same in debug and release builds: a full row is counted, never asserted.

### reserve

reserve() sizes row i to expected_invocations * multiplicities[i], or to the row's own last observed
demand if that was larger. An overflowing row therefore grows on the very next call, and a shared
registration_index gets a row big enough for every node behind it. It also sizes the offset bounds
to the device count, widens them again and clears the drop counts.

## Tests

`tests/platform/observability/` builds `sr_observability_test_objects`, linked into `sr_platform_tests`.

## Designs

<!-- designs:begin -->
- [PROFILING](../../../docs/design/PROFILING/README.md): How SushiRuntime reports its own time to SushiProfiler and to any observer, and which timing leaves its public report.
<!-- designs:end -->
