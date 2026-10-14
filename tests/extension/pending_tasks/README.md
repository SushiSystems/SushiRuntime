# pending_tasks tests

The tests of the `pending_tasks` module, mirroring `modules/extension/pending_tasks/`.
`tests/extension/pending_tasks/CMakeLists.txt` builds them into `sr_pending_tasks_tests`. `sr test`
runs them with the rest; `sr run sr_pending_tasks_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_pending_tasks.cpp

The file tests `PendingTasks`, the table
that owns an offloaded node from `insert` until one removal returns it. It needs no hardware, no
transport and no clock: every time is a fixed point passed in, and a node is a bare `Internal::Graph::Node`
from `make_sushi` that the table never reads.

### `InsertAfterCloseRefused`, `DrainReturnsEveryNode`

After `close()` an insert returns false and the caller keeps its node; an id already in the table is
refused too. `drain` then hands back each node that was inserted, once, whether or not the task was
acknowledged, and a second `drain` finds nothing. Together they pin findings D1 and D2: a stopping
coordinator takes no new node and returns the ones it holds.

| Test | What it proves |
| --- | --- |
| `TakeIsExactlyOnce`, `ForeignWorkerCannotTake` | `take` returns the node, the worker and the output handles as dispatched, and the same call a second time returns nothing, as do the three other removals. A worker that does not own the task can neither take it, acknowledge it nor pass the output fence for it. |
| `LateResultLeavesInflightAlone` | The regression test of finding DIST-20. A result for an id that was never dispatched, or for a task `take_for_worker` already returned, gets nothing back, so the caller has no task to lower a count for. |

### `UnacknowledgedTaskReapedWithDeadlineZero`

Finding D6, the lost dispatch. With a task deadline of 0, a task with no `TASK_ACCEPTED` is reaped
one millisecond after the acknowledgement limit of 5000 ms, with the cause `Unacknowledged`, and not
at the limit itself. The acknowledged task beside it is never reaped, because a deadline of 0 puts
no bound on the kernel. `HeartbeatLeavesAnUnacknowledgedTaskToItsOwnLimit` shows that heartbeats do
not count against a task the worker has not accepted.

### `AcknowledgedTaskBelowOldestHeldReaped`

Finding D6, the lost result. A heartbeat whose oldest held id is above an acknowledged task counts
one miss; the task is reaped as `LostResult` at the second miss in a row. A heartbeat that lists the
task again clears the count, which the test shows with task 2: it misses, is listed, and then needs
two more misses. A task of another worker is untouched.

| Test | What it proves |
| --- | --- |
| `ChunkRestartsTheDeadline`, `TakeForWorkerForgetsItsChunkTime` | The task deadline runs from the later of the dispatch and the worker's last chunk, per worker. A worker's chunk time is dropped with its tasks, so a task dispatched to it afterwards is measured from its own dispatch. |

### `TwoRemovalsOfOneTaskHaveOneWinner`

In each of 200 rounds one thread calls `take` and another `take_for_worker` for the same task. Both
wait at a `StartGate`, which spins until the two have arrived, so the calls overlap as closely as
two threads can. Whichever order the scheduler picks, exactly one of them gets the node.
