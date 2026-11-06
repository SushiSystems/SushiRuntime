# pending_tasks tests

The tests of the `pending_tasks` module, mirroring `modules/extension/pending_tasks/`.
`tests/extension/pending_tasks/CMakeLists.txt` builds them into `sr_pending_tasks_test_objects`, linked into `sr_extension_tests`. `sr test`
runs them with the rest; `sr run sr_extension_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_pending_tasks.cpp

The file tests `PendingTasks`, the table that owns an offloaded node from `insert` until one removal
returns it. It needs no hardware, no transport and no clock: every time is a fixed point passed in, and a
node is a bare `Internal::Graph::Node` from `make_sushi` that the table never reads. Every test is small
except the removal race, which starts two threads.

| Test | What it proves |
| --- | --- |
| `TaskIDsRiseFromOne` | Ids start at 1 and rise by one. |
| `ADuplicateIDIsRefused` | An id already in the table is refused and the table keeps its size. |
| `InsertAfterCloseIsRefused` | After `close()` an insert returns false and the caller keeps its node. Together with the drain tests it pins findings D1 and D2: a stopping coordinator takes no new node and returns the ones it holds. |
| `TakeReturnsTheTaskWithItsOutputs` | `take` returns the node, the worker and the output handles as dispatched. |
| `ATakenTaskLeavesThroughNoSecondRemoval` | After a `take`, a second `take`, `take_for_worker`, `take_overdue` and `drain` find nothing. |
| `DrainReturnsEveryNodeOfEveryWorker` | `drain` hands back each node that was inserted, once, whether or not the task was acknowledged, and empties the table. |
| `ASecondDrainFindsNothing` | A second `drain` returns nothing. |
| `AnIDThatWasNeverDispatchedTakesNothing` | The regression test of finding DIST-20, first half: a result for an id that was never dispatched gets nothing back. |
| `AResultForATaskAlreadyReturnedIsRefused` | Finding DIST-20, second half: a result for a task `take_for_worker` already returned gets nothing back, so the caller has no task to lower a count for. |
| `ALostTaskLeavesAnotherWorkersTaskInflight` | Returning the tasks of one worker leaves the task of another worker in the table. |
| `ForeignWorkerCannotTake` | A worker that does not own the task can neither take it, acknowledge it, pass the output fence for it nor have it returned by `take_for_worker`. |
| `ExpectsOutputOnlyAtTheDispatchedVersion` | `expects_output` is true only for the dispatched buffer at the dispatched version, and false once the task is taken. |
| `AnUnacknowledgedTaskIsReapedOneMillisecondAfterTheLimit` | Finding D6, the lost dispatch. With a task deadline of 0, a task with no `TASK_ACCEPTED` is reaped one millisecond after the acknowledgement limit of 5000 ms with the cause `Unacknowledged`, and not at the limit itself. |
| `ADeadlineOfZeroBoundsNoAcceptedTask` | An acknowledged task is never reaped with a task deadline of 0. |
| `TwoMissesInARowReapATaskAsLostResult` | Finding D6, the lost result. A heartbeat whose oldest held id is above an acknowledged task counts one miss; the task is reaped as `LostResult` at the second miss in a row. |
| `AHeartbeatThatListsTheTaskAgainRestartsItsMisses` | A heartbeat that lists the task again clears the count: a listed task needs two more misses to be reaped. |
| `AWorkerThatSentNoHeartbeatLosesNothing` | A task of a worker that sent no heartbeat is not reaped. |
| `AHeartbeatLeavesAnUnacknowledgedTaskToItsOwnLimit` | Heartbeats do not count against a task the worker has not accepted. |
| `ChunkRestartsTheDeadline` | The task deadline runs from the later of the dispatch and the worker's last chunk, per worker. |
| `TakeForWorkerForgetsItsChunkTime` | A worker's chunk time is dropped with its tasks, so a task dispatched to it afterwards is measured from its own dispatch. |
| `OwnDeadlineReplacesTheClusterDeadline` | A task inserted with its own deadline is reaped by it, shorter or longer than the cluster's; a task with none keeps the cluster's. |
| `EarliestDeadlinePublishesOnlyOwnDeadlines` | `earliest_deadline()` is the maximum time point until a task with its own deadline is inserted, and then that deadline plus one tick. |
| `EarliestDeadlineIsAtOrBeforeTheMovedDeadlineAfterAChunk` | After a chunk moves a deadline on, the published bound is at or before the moved deadline. |
| `ATakeLeavesNoEarliestDeadline` | After the only task with a deadline is taken and the next pass runs, no deadline is published. |
| `Medium_PendingTasksRace.TwoRemovalsOfOneTaskHaveOneWinner` | One round per row, 200 rows. In each one thread calls `take` and another `take_for_worker` for the same task. Both wait at a `StartGate`, which spins until the two have arrived, so the calls overlap as closely as two threads can. Whichever order the scheduler picks, exactly one of them gets the node. |
