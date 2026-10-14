# probe_tasks

The module holds the small tasks the tests submit to watch the scheduler: one that bumps a counter,
one that records its priority, and a host task that records its ID. It is an internal module built
only with the tests: its headers are never installed and the library does not link it.

It depends on `core` and `task`.

## Public entry points

| Header | Holds |
| --- | --- |
| `atomic_counter_task.hpp` | `AtomicCounterTask` |
| `priority_tracking_task.hpp` | `PriorityTrackingTask` |
| `order_recorder.hpp` | `make_order_recorder()` |

Every name is declared in `SushiRuntime::Test`.

## order_recorder.hpp

`make_order_recorder` takes an optional delay. Sleeping before the task records its ID widens
the window in which an ordering bug can surface.
