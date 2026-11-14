# submit_funnel

**Describes:** `modules/engine/scheduler/source/submit_funnel.hpp`, `modules/engine/scheduler/source/submit_funnel.cpp`, `modules/engine/scheduler/source/submit_funnels.hpp`, `modules/engine/scheduler/source/submit_funnels.cpp`

The submit funnel lets one thread per device enter the device's submission locks in place of every
worker. It is off unless `WorkerPoolSettings::submit_funnel` is set, which
`Internal::Execution::worker_pool_settings()` copies from `RuntimeSettings::submit_funnel`, itself
copied from `RuntimeConfig::submit_funnel`. The measurements and the rulings behind it are on
[the design page](../../../../docs/design/GRAPH_REPLAY/SUBMIT_FUNNEL.md).

## SubmitFunnels

`SubmitFunnels` holds one `SubmitFunnel` per device of the `DeviceQueueSet`, or none when the
setting is off; `TaskScheduler::Parts` builds it between `RunState` and `WorkerRounds`, so it
outlives the rounds that push into it. `TaskScheduler` starts the funnel threads in `start()`,
before the workers, arms and disarms them with the run, and stops them in `stop()` after the
workers have ended and an attached executor's rounds have drained. `funnelled_count()` sums the
records every funnel took; `TaskScheduler::funnelled_task_count()` returns it, and the engine
reports it as `ExecutionMetrics::tasks_funnelled`.

## SubmitFunnel

### The record ring

`WorkerRounds::execute_task()` builds a `FunnelRecord` at the submit site of a graph node that is
not a host node, once the node is placed and its queue chosen, and pushes it into the funnel of the
queue's device through `MPSCRing::try_push()`. The push takes no lock. A worker whose push finds
the ring full, or whose queue matched no device, submits the node itself as it does with the funnel
off.

### The thread

The thread, named `sr-funnel-<device>` so the profiler records its zones, pops records in order and
calls `Internal::Graph::submit_task()` with the record's queue and the node's `sycl_deps`, inside a
`dispatch.submit` zone. Under profiling it reads the steady clock just before the call, and
`dispatched_event()` builds the event the worker would have built, with the funnel thread as the
submitting thread. A submission that throws is named with `name_failure()`, logged, recorded in the
run's `RunOutcome` and landed as `skipped_dispatch_event()`, as `execute_task()` does on a worker.

While a run is armed the thread yields between empty polls. Once the run is disarmed it yields 100
times and then parks on a condition variable, at most 100 ms at a time. `arm()` wakes it; it takes
the park mutex only when the funnel goes from disarmed to armed, so the re-arm the completion
sentinel makes from a worker takes no lock.

### The landing rings

Each worker has an `SPSCRing` of 256 events in each funnel. The funnel thread is its only producer
and the worker its only consumer. When a worker's ring is full the thread keeps the event in an
overflow of its own and moves it in later, before it pops the next record.

`WorkerRounds` counts, per worker, the nodes that are in a funnel and have not landed. The worker
raises the count before its push and lowers it when it lands events at the top of each round,
before it polls. `in_flight_empty()` reads it with the event list, so a worker with a node in a
funnel neither parks nor exits, and `has_work()` keeps an executor posting its rounds. The count
stays at zero with the funnel off.
