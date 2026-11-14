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

`WorkerRounds::execute_task()` builds a `FunnelRecord` at the submit site of every dispatch that
reaches a queue: a kernel node, a HOST node that submits device work, and a task that is not a
graph node. It does so once the node is placed and its queue chosen, and `SubmitFunnels::hand()`
pushes the record into the funnel of the queue's device through `MPSCRing::try_push()`. The push
takes no lock. With the funnel on, `select_dispatch_queue()` looks up the device of a plain task's
queue even without profiling, so that task finds its funnel too.

A record whose push finds the ring full stays in the worker's backlog, and `SubmitFunnels::land()`
pushes it again at the top of each round; a worker never submits it itself. Only a dispatch whose
queue matched no device, which the queue set's own queues never produce, is submitted by the worker
as with the funnel off.

### The thread

The thread, named `sr-funnel-<device>` so the profiler records its zones, pops records in order and
calls `submit_dispatch()` (`dispatch_record.hpp`), the same submit sequence a worker runs, which calls
`Internal::Graph::submit_task()` with the record's queue and the node's `sycl_deps` inside a
`dispatch.submit` zone. For a HOST node that call runs the body on the funnel thread, and under
profiling it collects the events the body hands over, as the worker does. Under profiling the thread
reads the steady clock just before the call, and `dispatched_event()` builds the event the worker
would have built, with the funnel thread as the submitting thread. A submission that throws is named with `name_failure()`, logged, recorded in the
run's `RunOutcome` and landed as `skipped_dispatch_event()`, as `execute_task()` does on a worker.

While a run is armed the thread yields between empty polls. Once the run is disarmed it yields 100
times and then parks on a condition variable, at most 100 ms at a time. `arm()` wakes it; it takes
the park mutex only when the funnel goes from disarmed to armed, so the re-arm the completion
sentinel makes from a worker takes no lock.

### The landing rings

Each worker has an `SPSCRing` of 256 events in each funnel. The funnel thread is its only producer
and the worker its only consumer. When a worker's ring is full the thread keeps the event in an
overflow of its own and moves it in later, before it pops the next record.

`SubmitFunnels` counts, per worker, the records it was handed that have not landed, the backlog
included. `hand()` raises the count before the push and `land()` lowers it at the top of each round,
before the worker polls. `WorkerRounds::in_flight_empty()` reads it through `is_idle()` with the
event list, so a worker with a record in a funnel or in its backlog neither parks nor exits, and
`has_work()` keeps an executor posting its rounds. With the funnel off there is no worker side and
`is_idle()` is always true.
