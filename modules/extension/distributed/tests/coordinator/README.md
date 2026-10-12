# distributed tests: coordinator

The master's side: the worker directory, the pending tasks and the offload dispatcher.
The tests are part of `sr_distributed_tests`; [the page of the program](../README.md) says
how to run it.

## test_pending_tasks.cpp

The file tests `PendingTasks` of `modules/extension/distributed/source/pending_tasks.hpp`, the table
that owns an offloaded node from `insert` until one removal returns it. It needs no hardware, no
transport and no clock: every time is a fixed point passed in, and a node is a bare `Internal::Graph::Node`
from `make_sushi` that the table never reads.

### `InsertAfterCloseRefused`, `DrainReturnsEveryNode`

After `close()` an insert returns false and the caller keeps its node; an id already in the table is
refused too. `drain` then hands back each node that was inserted, once, whether or not the task was
acknowledged, and a second `drain` finds nothing. Together they pin findings D1 and D2: a stopping
coordinator takes no new node and returns the ones it holds.

### `TakeIsExactlyOnce`, `ForeignWorkerCannotTake`

`take` returns the node, the worker and the output handles as dispatched, and the same call a second
time returns nothing, as do the three other removals. A worker that does not own the task can
neither take it, acknowledge it nor pass the output fence for it.

### `LateResultLeavesInflightAlone`

The regression test of finding DIST-20. A result for an id that was never dispatched, or for a task
`take_for_worker` already returned, gets nothing back, so the caller has no task to lower a count
for.

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

### `ChunkRestartsTheDeadline`, `TakeForWorkerForgetsItsChunkTime`

The task deadline runs from the later of the dispatch and the worker's last chunk, per worker.
A worker's chunk time is dropped with its tasks, so a task dispatched to it afterwards is measured
from its own dispatch.

### `TwoRemovalsOfOneTaskHaveOneWinner`

In each of 200 rounds one thread calls `take` and another `take_for_worker` for the same task. Both
wait at a `StartGate`, which spins until the two have arrived, so the calls overlap as closely as
two threads can. Whichever order the scheduler picks, exactly one of them gets the node.

## test_worker_directory.cpp

The file tests `WorkerDirectory` of `modules/extension/distributed/source/worker_directory.hpp`. It
needs no hardware and no transport: the test plays the worker's side of the handshake with fixed
nonces and a secret of the bytes 0 to 31, computes proofs with `compute_proof` of `handshake.hpp`,
and passes every time in as a fixed point.

### `RecordingPolicy`

`RecordingPolicy` is the offload policy of the fixture. It records the workers of each call and
returns the first, a fixed id, or nothing, as the test sets it.

### `UnregisteredPeerMessagesDropped`

The regression test of finding DIST-03. The test lists the message types `admits` passes for a peer
at each step: none for an unknown peer, `HELLO` and `GOODBYE` after the connect, those and `PROOF`
after `HELLO`, and every type but the two handshake frames once the proof holds. A `BUFFER_REQUEST`
or `BUFFER_CHUNK` from a peer that has not proved the secret is therefore dropped unread.

### `HandshakeDeadlineClosesPeer`

Finding DIST-04. A peer that has not registered is listed one millisecond after the limit, counted
from its connect, and once. Sending `HELLO` again does not move the limit, and a registered worker
is never listed.

### `WaitForWorkersWakesOnRegistration`

Finding DIST-10. A thread waits for one worker with a limit of 24 hours while the test registers
one, and a second thread waits for two while the test closes the directory. The limit is never
reached, so a registration or a `close()` that wakes nobody hangs the test until the program's
timeout; no outcome depends on which thread runs first.

### `WrongProofIsRejected`

A proof made under another secret, the master's own proof sent back, and a proof made under an
empty key are each refused with `AuthenticationFailed`, and the peer is not a worker afterwards. A
refused proof spends its nonces: the right proof for them is then `OutOfOrder`.

### `SecondHelloRestartsTheHandshake`

A second `HELLO` from a peer that owes `PROOF` gets a proof for the new nonces, and only a `PROOF`
for the new nonces registers it. `HELLO` and `PROOF` from a registered worker are `OutOfOrder` and
change nothing.

### `SilentWorkerIsTakenOnce`

A worker silent for longer than the liveness limit is listed once and stops counting at that
moment: the alive count drops, its operations stop counting toward `supports`, and `pick` offers
only the other worker. The `remove_peer` that follows does not lower the count a second time, and
a late frame does not bring the worker back.

### `PickSkipsWorkersAtTheirJobLimit`

Two workers send limits of 1 and 2 in `PROOF`. The policy sees both, then only the second, then
nobody, and at that point it is not called at all. One `release_inflight` frees one slot. An answer
that names a worker the policy was not offered counts as no pick.

### `SnapshotReportsWhatTheWorkerSent`

One registered worker and one peer that only connected. After a `PONG`, a `HEARTBEAT`, a frame
and a pick, the snapshot holds one entry, the worker's, with its id, name, endpoint, an inflight
count of 1 and the round trip, free memory, queue depth and cache size it sent. Its
`ms_since_last_frame` is 600, the time from its last frame to the snapshot.

### `IdentityCarriesTheBuildOfTheLastHello`

`WorkerDirectory::identity()` returns nothing for a stranger and for a peer that still owes
`PROOF`. A peer that said `HELLO` twice and then proved itself has the name, the endpoint and the
build id of the second `HELLO`. A third `HELLO`, sent after registration, changes none of them,
and `remove_peer` ends the identity.

### `HelloFromARegisteredWorkerRegistersNothingTwice`

A registered worker with one task out says `HELLO` again, as a worker does when its `WELCOME` was
lost. The answer is `AlreadyRegistered`. The alive count, the kernels, the inflight count and the
build id stay as they were, and the directory still refuses a `PROOF` from that peer.

### `CloseRetiresEveryWorker`

Finding QC-36. Two workers are registered and `close()` is called. The alive count must be 0, no
operation supported, `list_workers` and `snapshot` empty, and `wait_for_workers` false for a
count of 2 and for a count of 0. A third peer that then sends a valid `HELLO` and `PROOF` gets
`OutOfOrder`. `remove_peer` on a worker still reports it as registered, since the coordinator
counts the loss from that answer, and neither it nor a second `close()` lowers the count again.

## test_offload_dispatcher.cpp

The file tests `OffloadDispatcher` of `modules/extension/distributed/source/offload_dispatcher.hpp`
over a real `BufferRegistry`, `WorkerDirectory` and `PendingTasks`. The transport is a fake that
records each `send()` and answers with a status the test sets; the policy is a fake that picks the
first worker offered and counts its calls. The one worker registers through `accept_hello` and
`accept_proof` under the empty secret. A buffer is a `std::array<float, 16>` whose address is
shared; no byte of it is read, so the file needs no device and no thread.

### `AttemptCapDeclinesWithoutClearingOffloadable`

With `attempts` at `K_MAXIMUM_OFFLOAD_ATTEMPTS` the dispatcher returns false before it asks the
policy, and `offloadable` and the count stay as they were. `complete_local` sets the count to 0 and
the next `try_offload` dispatches. This pins finding N3: the cap holds for one run, not for good.

### `RefusedSendUndoesThePendingEntry`

The transport answers `QueueFull`. The frame was offered once, the pending table is empty, the
worker's inflight count is 0, `try_offload` returned false and `send_queue_full` and `declined` are
1. The output keeps the version the frame named, one above the version before the call.

### `StoppedCoordinatorDeclines`

`DistributedCoordinator::stop()` closes the pending table, then the directory. After the first
close the policy is asked once, `insert` refuses and the worker's inflight count goes back to 0.
After the second the policy is not asked. No frame reaches the transport in either state and
`offloadable` stays set.

### `DynamicNodeResolvesItsLiveLists`

A node with `live_reads` and `live_writes` gets no payload from `prepare`. Its dispatch names the
buffers of the live lists; after the two lists swap, the next dispatch names them the other way
round, though `io_reads_` and `io_writes_` did not change. `complete_local` then raises the
version of the buffer in `live_writes` and leaves the other alone, which pins finding D3.

### `PrepareWarnsOnceForLocalOnlyOps`

One `prepare` over an offloadable node, a node whose operation no worker has, a node with an
unshared output, an untagged node and a null slot logs one WARN, captured through
`set_log_sink`. Its text is `2 of 3 op-tagged nodes will run locally: 1 no live worker for
the op, 1 buffer not shared, 0 buffer written twice (first: op N)`, where N is the id of the
operation no worker has. A second `prepare` over the offloadable node alone logs nothing.

### `LocalNodeBumpsOnlyItsSharedWrites`

A node that stays local and writes a shared buffer keeps that buffer's handle, and
`complete_local` raises its version. A node that writes no shared buffer gets no `OffloadState`,
so its completion returns after one null check and takes no lock.

### `NodeThatWritesOneBufferTwiceStaysLocal`

A fixed-buffer node and a double-buffered node each list one buffer twice among their writes.
Both stay local, and the one WARN counts them under the third reason, `2 buffer written twice`. A
node that reads and writes the same buffer names it once as an output and stays offloadable. The
local run of the first node still raises the version of the buffer it wrote.
