# Profiling guide

How a program sees where a run spent its time. The runtime reports time in two ways: to a
`RunObserver` the program adds, and to SushiProfiler, the stack's profiler, through zones, values,
counters and a device source. Both read the same capture.

## What a run reports

A `RunReport` holds the outcome of a run: success, cancellation, the task count, the execution layer and
the NUMA and distributed counts. It holds no timing. Time reaches a program in these ways:

| What | How it reaches you |
| --- | --- |
| The device time of each dispatch | A `RunObservation` handed to an observer, or a span of the source `sushiruntime.device` |
| The stages of a run: plan build, submit, wait, a chain's submit and wait | Zones in the group `Runtime` |
| One dispatch's submission, a host node's body, an allocation, a queue wait | Fine zones |
| A contended lock on the scheduler's submission mutex | The zone `lock.pending`, written only when the lock was contended |
| The workers' busy, stealing, polling and idle time of a run | The values `worker.busy`, `worker.stealing`, `worker.polling` and `worker.idle` |
| Task, steal and transfer counts of a run | The counters `run.tasks`, `run.steals`, `transfer.h2d_bytes` and `transfer.d2h_bytes` |

[The profiler keys](../reference/PROFILER_KEYS.md) list every name, level and unit.

## An observer without a profiler

`Runtime::add_run_observer()` adds an observer and returns a handle that removes it when it is destroyed or
reset. Any number of observers may be added and each receives every finished run, in the order added.

```cpp
class Tally final : public SushiRuntime::RunObserver
{
    public:
        void on_run(const SushiRuntime::RunObservation& observation) noexcept override
        {
            dispatches += observation.events.size();
        }

        std::size_t dispatches = 0;
};

auto tally = std::make_shared<Tally>();
SushiRuntime::RunObserverHandle handle = runtime.add_run_observer(tally);
```

Source: not compiled

A `RunObservation` holds:

- `layer`: the path that ran the plan, the scheduler or the chain.
- `table`: the plan's `NodeTable`, one row per registration index with the node's name, origin, operation,
  optional cost and host-native flag, and the graph's label and id. Every run of a plan shares it.
- `graph_id`: the id of the graph that owns the plan, the value `Graph::id()` returns.
- `events`: one `NodeEventRecord` per dispatch, with the node's index, the step, the widths, the device, the
  submitting thread and the submit, start and end times.
- `device_clocks`: one `ClockPairing` per device that ran a kernel of the run.
- `drops`: how many dispatches the run lost, by cause.

A device record is on its device's event clock. Its `ClockPairing` places it on the steady clock: the
distance from the record's time to the pairing's event reading is the distance from the result to its steady
reading. A host-node record is already on the steady clock, with the device index `K_HOST_DEVICE_INDEX`. The
pairing's `uncertainty_ns` is the length of the submit call that produced it, which bounds the error of the
conversion.

`on_run()` runs on the thread that learned the run finished, must not throw, must not start a run and must not
add or remove an observer. A blocking run notifies when its last step ends. A step started with `run_async()`
notifies when its device work completes, not when the caller waits for it, so a caller that waits late loses
nothing. A failed run notifies nothing.

## Putting the runtime on a SushiProfiler timeline

A host that links SushiProfiler registers the runtime and starts a session:

```cpp
SushiProfiler::Registry registry;
SushiProfiler::register_builtin_writers(registry, std::cout);
SushiRuntime::RuntimeProfilerParts parts = SushiRuntime::register_profiler_parts(registry, runtime);
```

Source: not compiled

The registry then knows the source `sushiruntime.device` and the runtime's environment facts. A session
started with it adds an observer to the runtime when it begins and removes it when it ends. The host keeps
`parts` alive until every session started with the registry has stopped. A second runtime joins the same
source with `parts.attach(other)` before a session starts, and its dispatches land on tracks named by its own
node.

The runtime and the host must link the same SushiProfiler library, the shared one. A runtime linked to the
static library would record its zones into a collector the host's session never drains. The host's own zones
and the runtime's land in the same record because they reach one collector.

### What the record holds

Each dispatch is a span on the track `<node>/<device index>/<device name>`, or `<node>/host` for a node that ran
on a worker thread. A span carries the node's name, the thread that submitted it and its attributes: `node`,
`invocation`, `operation`, `launched_width`, `live_width` and `layer`, `graph` for a graph that has a label
(`Graph::set_label()`), `origin` for a node whose caller named its own operation (`NodeOptions::origin`), and
`flops`, `bytes` and `dtype` for a node whose caller stated a cost. `device_track_name()` returns the track name for a runtime and a device index, so a
library that states a fact keyed by a track builds no string of its own.

A span goes in the period whose span holds its start. The runtime reads no period, so a span whose device has no
clock pairing has no start and is counted as dropped. So is a span that arrives after its period closed, and a
dispatch the run lost. The record's dropped spans count all of them, so a profile with no drops is complete.

## What it costs

The numbers below are the profiler's budgets and arithmetic on what the runtime does, not measurements of this
runtime; the overhead benchmark has not run yet (RW-69 in [the backlog](../design/REMAINING_WORK.md)).

| Case | Cost |
| --- | --- |
| Built with `SR_ENABLE_PROFILER` off, or a zone above `SR_PROFILE_LEVEL` | Nothing: the macro compiles out |
| A zone compiled in, no session recording | One relaxed load and a branch, under 2 ns |
| A zone compiled in, a session recording | Under 25 ns |
| Device timing | A queue that records timestamps, three timestamp reads per dispatch, a snapshot of the node's widths and a record per dispatch; two steady clock reads per host node |
| An untimed run | One load and a branch |
| Idle memory | The plan's node table, one row per registration index with its name, and after a timed run the ledger at its high-water size, 48 bytes per recorded dispatch |

Device timing is chosen per run and is on while at least one observer is added. No plan is rebuilt when an
observer is added or removed: the run that starts after `add_run_observer()` is timed and carries records, and
a run already in flight when the observer is added reaches it with no records. The only thing that differs in
a timed run is the queue, the device's profiling-enabled flavour, built on first use. The observer also pays
for the copy it makes of an observation, which it decides how to keep.

The first run after an observer is added builds the device's profiling queue, and the first submit of a process may
include driver compilation. Neither is recorded, and their size is not measured.

## Build options

| Option | Default | Effect |
| --- | --- | --- |
| `SR_ENABLE_PROFILER` | `ON` | Builds the `profiling` module and compiles the zones in; `OFF` removes SushiProfiler from the build |
| `SR_PROFILE_LEVEL` | `2` | The deepest zone level: 0 none, 1 stages, 2 fine zones, 3 per-element zones |

## Limits

- A run on the in-order chain reports one device, the chain's queue, and its worker values are zero.
- The distributed layer writes no zones; its timing is its own phase.
- Two devices that share an event clock still get two pairings.
- A task body that submits several kernels hands each event to `note_dispatch_event()`; otherwise its dispatch is
  timed by the one event it returns, which covers only the last kernel.
