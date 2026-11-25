# submit_release

**Describes:** `modules/engine/scheduler/include/SushiRuntime/scheduler/submit_release.hpp`, `modules/engine/scheduler/source/worker_rounds.cpp`

A worker that submits a kernel node releases its successors at once instead of waiting for the
node's event to complete. The successor's submit then waits for that event on the device. The
rulings behind it are in [the design topic](../../../../docs/design/LAYERED_EXECUTION/README.md#release-at-submit).

## release_successors_at_submit

After a successful submit, `WorkerRounds::release_at_submit()` calls
`release_successors_at_submit()` with the node and the event of its submit. For each successor that
`may_release_at_submit()` accepts, `Node::release_at_submit()` claims it with a compare-and-swap of
`in_degree` from 1 to 0, so only a successor whose last outstanding predecessor is this node is
claimed. The winner stores the event as `released_after()`, takes a scheduling reference, adds
itself to the quiescence latch and is pushed to the worker's own deque. Only the owner pushes to
that deque and reaps its events, so the release cannot race the reap.

When the successor is dispatched, `submit_dispatch()` passes `released_after()` to `submit_task()`
as `after`, and the command group calls `depends_on` on it. The device orders the edge, so the
launch does not wait for the host to see the completion.

## may_release_at_submit

Both nodes must be kernel nodes without a `placed_split`, with a checked locality and the same
`cached_queue_index_`. The successor must be neither the sentinel nor offloadable. A host body
would run on the worker and could read data its predecessor has not written. A split node submits
several command groups, so one event does not cover it. An offloadable node may move to a device
whose queue does not share the event's context.

A worker holding `K_SUBMIT_RELEASE_IN_FLIGHT` (16) events releases nothing; the value is a first
choice and is not measured. A dispatch handed to a submit funnel, a submit that failed and a run
that is aborting release nothing at submit.

## Completion and skipped nodes

`Node::on_complete()` drops `released_after()`, then takes each successor's `in_degree` down by
one. A successor already released at this node's submit is at 0 and goes to -1, so it is not
reported a second time. A released node that is skipped (an aborting run, a disabled late-bound
node) or whose submit failed gets the event `skipped_dispatch_event()` builds: not ready, polling
`released_after()`, so its own successors are freed only after its predecessor ends.

A kernel that fails after its successor was released leaves that successor on the device. The run
records the failure and aborts as before.
