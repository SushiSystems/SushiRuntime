# observability

The module holds the instruments a run is measured with: the profiling ledger, the event
timestamps and the transfer counters. It is header-only.
The module is internal: its headers are for the other modules of the library and are not installed.

It depends on the C++ standard library, the SYCL headers and `report`.

## Public entry points

| Header | Holds |
| --- | --- |
| `profiling_ledger.hpp` | `ProfilingLedger`, the per-plan store of node event records, `DispatchFacts` and `DispatchTimes` |
| `event_timestamps.hpp` | `EventTimestamps`, the function that reads them from a SYCL event, and the ones that merge the events of one dispatch |
| `transfer_counters.hpp` | `TransferCounters`, the byte counters of explicit host and device transfers |

Everything is in `SushiRuntime::Internal::Observability`. `ProfilingLedger::collect` appends to the
`events` of a `RunObservation`, which the `report` module defines, and `collect_pairings` appends the
device clocks.

## event_timestamps.hpp

### read_event_timestamps

A backend with no start/end reading leaves the dispatch unrecorded; one with no submit reading
degrades to submit_ns == start_ns.

### read_dispatch_timestamps

A task body that submits several kernels hands every event over with `note_dispatch_event()`.
read_dispatch_timestamps() reads the timestamps of the dispatch across them: the earliest start, the
latest end and the earliest submit, through merge_timestamps(); the submit reading counts as the
backend's own only when every event has one. When the body handed over none, the event it returned
is read alone. One event that cannot be read fails the whole dispatch, which the ledger counts as an
unreadable event.

## profiling_ledger.hpp

### ProfilingLedger

A ProfilingLedger is owned by CompiledGraph and reused across runs. Each row is sized to its own
node's expected dispatch count times how many internal nodes share its registration_index.
reserve() grows it outside the poller, before a run's dispatch loop starts, so claim_slot() and
write() never allocate. The ledger also carries one clock pairing claim per device.

### record

record() is the one writer of a node event record. The poller, the in-order chain and the host path
each pass the node, the step, the widths, the device, the submitting thread and the three
timestamps, and the ledger fills every field of the record itself, so a new field is one edit. The
host path passes `K_HOST_DEVICE_INDEX` and steady-clock times; a device path passes its device's
index and event-clock times.

### Clock pairings

The first device submission to reach a device in a run, a kernel or a task body, claims that
device's pairing with a compare-exchange. release_pairing_owner() gives a claim back when the
submission turned out to submit nothing. `PairingClaim` (`pairing_claim.hpp`) is the one way a
dispatch claims: it claims, times the submit call with `steady_ns()`, and on `settle()` records that
window or gives the claim back. The scheduler and the chain run both use it.
The claim is per device, since two devices may not share an event clock. collect_pairings() appends a
`DeviceClock` for each device whose owner's event timestamps were read; a device that ran no device submission,
or whose owner's event could not be read, has none, and its spans cannot be placed on the steady
clock.

### Dropped dispatches

A dispatch that leaves no record is counted under one of five causes (`DropCause`): an unreadable
event, a full row, a node past the table, a kernel with no event, or a dispatch that found no device
queue. The first node that lost one is kept. drops() reads the counts, reserve() clears them, and the
behaviour is the same in debug and release builds: a full row is counted, never asserted.

### reserve

reserve() sizes row i to expected_invocations * multiplicities[i], or to the row's own last observed
demand if that was larger. An overflowing row therefore grows on the very next call, and a shared
registration_index gets a row big enough for every node behind it. It also sizes the pairing slots
to the device count and clears the pairings and the drop counts.

## Tests

`tests/platform/observability/` builds `sr_observability_tests`.

## Designs

<!-- designs:begin -->
- [PROFILING](../../../docs/design/PROFILING/README.md): How SushiRuntime reports its own time to SushiProfiler and to any observer, and which timing leaves its public report.
<!-- designs:end -->
