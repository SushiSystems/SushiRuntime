# Profiling

**Status:** Open — P1 to P4 written and not yet compiled or run
**About:** How SushiRuntime reports its own time to SushiProfiler and to any observer, and which timing leaves its public report.
**Modules:** modules/foundation/profile, modules/foundation/report, modules/platform/observability, modules/surface/profiling

## Problem

The runtime measured time in six places and handed it to nobody. A run's device times, its host-node times,
a per-node table, the workers' busy, stealing, polling and idle time, the run's wall time and its plan
compile time, and its transfer bytes all ended in `RunReport`, which no program of the repository read.
SushiProfiler is the stack's one profiler; a second timing path in the report is a second profiler to keep
true.

What the profiler needs from the runtime, it could not get:

- The device each dispatch ran on and the thread that submitted it, so a span has a track and sits under the zone that launched it.
- One clock pairing per device, since a CPU and a GPU device do not share an event clock.
- A node table with the name, operation and stated cost of each node, so a span can be joined to what the caller built.
- The runtime's own stages as zones: the plan build, a run's submit and wait, a dispatch, an allocation, a contended lock.
- A way for SushiAI, SushiBLAS and SushiEngine to read the capture without a profiler session.

Dispatches also vanished without a trace in four places, and the runtime logged nothing where it recovered or
dropped a status.

## Decision

Timing leaves the public report. The runtime keeps only the capture nobody else can do: the SYCL event
timestamps, one writer of a dispatch record, the per-device pairing and the counters only it sees. They reach
the outside through a `RunObserver` and through the profiler's zones, values and counters, and nowhere else.

- One foundation module, `profile`, is the only code below the surface tier that includes SushiProfiler. A surface module, `profiling`, holds the device source, the environment contributor and the registration function.
- SushiProfiler is linked as its shared library by the runtime and by every host, so a process has one collector.
- A runtime fans a run out to any number of observers. Device timing is chosen per run: a run is timed while at least one observer is added, and no plan is rebuilt when one is added or removed.
- An observer receives a `RunObservation`: the layer, the shared node table, the records, one pairing per device and the drop counts. A device record stays on its device's event clock and a host record on the steady clock.
- No period is read in the runtime. The source places each span by its converted start against the period the session hands it, which makes the per-device pairing load-bearing.
- A run hands its observation over when its device work completes, so a caller that waits late loses nothing.
- Zones sit in exported sources only, never in header templates a consumer compiles. The level is compiled: `SR_PROFILE_LEVEL` 2 by default, with the spin and steal sites at 3.
- The removal is breaking and made at once, before 1.0, with no deprecation release. A changelog entry names each removed member and its replacement.
- Where the runtime recovers, falls back or drops a status it logs, under its own category per module, once and with the values. A counter that SushiLog does not yet offer is local to the runtime.

## Design

### Capture

A node dispatch writes one `NodeEventRecord`, 48 bytes: the node's registration index, the step, the launched
and live widths, the submit, start and end times, the device index and the profiler's index of the submitting
thread. One ledger function fills the whole record; the poller, the in-order chain and the host path call it.
A body that submits several kernels hands each event over with `note_dispatch_event()`, and the record then
runs from the earliest start to the latest end.

The first kernel to reach a device in a run claims that device's pairing and times its own `submit` call, so the
pairing's uncertainty is the length of that call. A dispatch that leaves no record is counted under one of five
causes and logged once per run.

### Observation

The plan builds one `NodeTable` and shares it with every observation of its runs. A row holds the index, the
label, the operation, the optional `NodeCost`, whether the node is host-native and how many internal nodes share
the index. The cost is absent unless the caller stated it; a stated zero is a cost.

A blocking run notifies at its end. A step started with `run_async()` notifies from its sentinel, once every leaf
has been reaped and before the latch signals.

### The device source

`sushiruntime.device` queues observations on the runs' threads and turns them into spans in `collect()`. A span
carries the track of its device, the node's name, the thread that submitted it and the pairing's uncertainty. Its
attributes are SushiProfiler's `DeviceSpanKeys` plus the name attribute `layer`. The track name comes from
`device_track_name()`, which a library that states a fact keyed by the track uses too. Older spans are dropped as
late and counted; the queue and a run's own drops are counted with them.

### Zones, values and counters

The groups and names are in `docs/reference/PROFILER_KEYS.md`. The stage zones are the plan build, a run's steps,
submit and wait, the chain's submit and wait, a region commit and the runtime's wait; the fine zones are one
dispatch, one submission, an allocation and a queue wait; the search for work to steal is the detail zone. A
contended acquisition of the scheduler's pending-submission mutex writes a lock-wait zone. Every run writes the
workers' busy, stealing, polling and idle time as values and its task, steal and transfer counts as counters.

### Alternatives set aside

- Keeping `RunReport::node_events` beside the observer: a second path to keep true.
- One pairing per run: it misplaces every span of the device whose clock differs.
- Reading the followed period at submit: the runtime holds no handle to it, and a step waited for late has no period to read.
- A single observer slot: SushiEngine reads records without a session while the profiler parts read them for the timeline.
- A host pull, where the host hands each report to the source: every host would have to remember it.

### Logging

The runtime logs a recovery, a fallback or a dropped status where it happens, under the category of its own module.
The new categories are `api`, `process_environment`, `network_socket` and `profiling`. A returned status is the
caller's to log; only a status dropped on the floor, such as an unwaited step that failed, is logged. A failed
`SR_VERIFY` or `SR_FATAL` is logged at `FATAL` by a handler `Runtime::create()` installs. The per-node and
per-dispatch lines are at `TRACE`.

## Phases

| Id | Phase | State |
| --- | --- | --- |
| P1 | Capture and observer: the report types, the ledger's one writer and per-device pairing, the node table, the observer fan-out and device timing by attachment, and the removal of timing from `RunReport`. Done when: a test adds an observer to a runtime, runs a graph of three named nodes and reads a table with their names and records that name the device each ran on; the removed members are gone from the tree. | building |
| P2 | Device source and registration: the `profiling` module, the source, the contributor, `register_profiler_parts()` and the track name. Done when: a session started with the registered parts records device spans joined to the node names, in the period that holds their start, with the device and driver in the record's environment. | building |
| P3 | The runtime's zones, values and counters, the lock waits and the overhead benchmark. Done when: a session records the plan build, the run and the dispatch zones, and the benchmark prints the cost of a run of empty nodes at each level with the gate closed and open. | building |
| P4 | Logging adoption: the silent places that now log, the counted drops and the throttles. Done when: each place named in the work's analysis logs the line it names, once, under its module's category, and `check_logging.py` passes. | building |

<!-- work:begin -->
- [2026_10_26_PROFILER_AND_LOG_ADOPTION](../../agent/2026_10_26_PROFILER_AND_LOG_ADOPTION/SPEC.md): P1, P2, P3, P4
<!-- work:end -->

## Open questions

- The cost of a run with the gate closed at level 2 is the profiler's budget until the benchmark has run; the default level drops to 1 if it misses (RW-69).
- The distributed layer has no zones or flows; its timing is its own phase (RW-70).
