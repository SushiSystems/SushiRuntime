# pending_tasks

The module keeps the master's table of the tasks out on a worker: what each task waits for, when it
is overdue, and which tasks a lost worker leaves behind. It is an internal module: its headers reach
the modules that name it and the tests, and are never installed. It is built with the distributed
layer, under `SR_ENABLE_DISTRIBUTED`, and every name it declares is in `SushiRuntime::Distributed`.

It depends on `core` and `transport`, and its source on `task`.

## Public entry points

| Header | Holds |
| --- | --- |
| `pending_tasks.hpp` | `PendingTasks`, `PendingTask` |

## pending_tasks.hpp

### Who owns an offloaded node

`PendingTasks::insert()` takes the scheduling reference `try_offload()` was handed. From then on
the node leaves through exactly one of `take()`, `take_for_worker()`, `take_overdue()` and
`drain()`, once, and whoever receives the `PendingTask` owns the node and passes it to the sink:
`complete_remote()` after a result, `resubmit()` otherwise. The entry carries the worker and the
output handles as dispatched, so nothing reads the node's lists after dispatch.

Before a resubmit the owner bumps the task's outputs in the registry. `take()` has already made
`expects_output()` false for the task, and a chunk that passed that check a moment earlier either
wrote before the bump, and is overwritten by the local run, or meets the bumped version in
`BufferRegistry::write()` and is refused. `resubmit_tasks()` is the one place this sequence lives.

### A send that fails after insert

`try_offload()` inserts before it sends. When `send()` returns anything but `Queued` it calls
`take(task_id, worker)`. If that returns the task, the dispatcher lowers the worker's inflight
count and returns false, and the pool runs the node. If it returns nothing, a reaper already took
the node and will resubmit it, so `try_offload()` returns true.

### Lost results

A result is lost when a worker's heartbeats stop listing a task it accepted.
`HEARTBEAT.oldest_held_task_id` is the smallest id the worker still holds, or, when it holds none,
the largest id whose result it has handed to its writer plus one. A heartbeat is a control frame
and can overtake the stream that ends with a task's result, and a heartbeat built just before a
dispatch arrived does not list it. One heartbeat is therefore not proof: `take_overdue()` reaps an
accepted task as `LostResult` only after `K_LOST_RESULT_HEARTBEATS` (2) heartbeats in a row
reported an oldest held id above it.

### A task's own deadline

`insert()` takes an optional deadline. When set, `take_overdue()` measures the task against it in
place of `OverdueLimits::task_deadline`, from the later of its dispatch and its worker's last chunk,
as for the cluster's deadline; a deadline of 0 or less sets no bound. The dispatcher passes the
`deadline` of the placement rule that sent the task, so a long task a rule pins is not taken back
at the cluster's limit. `Unacknowledged` and `LostResult` do not read it.
