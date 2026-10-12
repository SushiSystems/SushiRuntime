# distributed tests: end to end

A master and its workers together, with and without faults on the link.
The tests are part of `sr_distributed_tests`; [the page of the program](../README.md) says
how to run it.

## test_distributed_e2e.cpp

The file runs offload end to end on a real SYCL device, with a master and its workers in one
process. Most tests use a real `WorkerAgent` on the in-process fabric; the tests that need a worker
to misbehave use `ScriptedWorker`. Every test skips when SYCL reports no device.

Float buffers hold whole numbers below 2^24, which float addition keeps exact, so `K_TOLERANCE` is
0 and a comparison is an equality.

### `USMFloats`, `LocalWorker`, `BackgroundRun`

`USMFloats` owns one shared USM allocation and frees it. `LocalWorker` owns a worker's
`Runtime` and its `WorkerAgent`. `BackgroundRun` calls `Graph::run()` on its own thread; a
test waits for it at most `K_WAIT` (10 s), so a run that never returns fails that test and the
suite goes on. A run that did not return is detached, not joined.

### `vec_add_kernel`, `add_vec_add`

The kernel takes `span<const BufferView>` and reads the element count from the output's
`size_bytes`. Each registry counts its kernel's runs in an atomic the test owns, which is how a
test tells a remote run from the local fallback. `add_vec_add` adds the op-tagged task with a
fallback lambda that computes the same sum.

### `ScriptedWorker`

A worker endpoint that implements `TransportEvents` itself. It sends `HELLO`, answers `CHALLENGE`
with a `PROOF` under the empty secret, and answers `PING` with `PONG`, so the master counts it
alive. It records each `TASK_DISPATCH` and wakes the test through a condition variable. What it
does next is one of three scripts: `Accept` sends `TASK_ACCEPTED` and holds the task, `Fail` also
sends `TASK_RESULT` with `KernelFailed`, `FallSilent` sends nothing from then on. Its transport
does not reconnect. `send_stream` hands the master a `ScriptedFrames` source, because a frame that
carries the last-chunk flag can only travel in a stream.

### `StubTransport`

An `Transport` that carries nothing: `exposure()` and `start()` return what the test set, and
`start_calls()` counts the starts.

### `eventually`

The master's `stale_writebacks` counter has no event to wait on. `eventually` re-reads a condition,
yielding between reads, until it holds or `K_WAIT` passes. No test sleeps.

### `OffloadVectorAddRoundTrip`, `FanOutAcrossTwoWorkers`, `OffloadOverTCPLoopback`

One task, or two on two workers, goes through the whole chain: `prepare`, dispatch, the buffer
pull, the kernel on the worker, the output chunks and the completion. The fan-out test asserts the
total of the two workers' run counters, not a split: which worker gets which task depends on
timing. The TCP test binds `TCPMasterOptions` defaults (127.0.0.1, port 0) and reads the port from
`local_endpoint()` after `attach`.

### `AutoVersionBumpAcrossLocalWriteThenOffloadedRead`

A local task with no operation writes a shared buffer and an offloaded task reads it. The
buffer's version must rise, and the worker must compute from the values the local task wrote.
The worker's kernel runs once, so the writer ran on the master.

### `WorkerDeathFallsBackToLocal`

A `FallSilent` worker takes the dispatch and stops answering. With a liveness timeout of 300 ms
the master closes it and resubmits the task, which finishes locally. Until the dispatch the worker
answers pings, so it cannot be closed early on a slow machine.

### `StaleWritebackIsFenced`

An `Accept` worker holds the dispatched task. The test first streams an output chunk at the
dispatched version minus one, filled with 999, and waits for `stale_writebacks` to reach 1; the
output must still hold 7. It then streams a chunk at the dispatched version, filled with 42,
followed by a succeeded `TASK_RESULT`. The run must return with 42 in the output, one chunk
received, one refused and nothing resubmitted.

### `LostBufferPushTimesOutInsteadOfHanging`

The fault policy drops every `BUFFER_CHUNK` from master to worker, so no input arrives. With a
pull idle timeout of 200 ms the worker reports `InputUnavailable`; the master resubmits, and after
the attempt cap the task runs locally. The kernel must not have run on the worker.

### `FluentClusterOffload`, `FluentDistributedStateParity`, `evolve_distributed_state`

The fluent surface builds the same offload. A `State` swaps its two allocations every step, so
its handles are resolved at each dispatch; `evolve_distributed_state` runs 4 and 5 steps, which
leave the result in different allocations, and both must match `seed[i] + steps * (i % 7)`.

### `ClusterDetachAndDisplacement`, `ClusterHandleOutlivesRuntime`

`detach()` leaves `offload()` null and a second call does nothing. A later `attach()` displaces
the installed master, and the displaced handle's `detach()` leaves the new one installed. A handle
that outlives its context still answers `alive_workers()` and `offloaded()`.

### `StopResubmitsPendingNodes`

An `Accept` worker holds the only task while `execute()` waits. The test calls `stop()` on the
installed coordinator. The run must return with the local result, `redispatched` must be 1 and
`pending_tasks` 0.

### `TryOffloadAfterStopRunsLocally`

`compile()` builds the plan while a real worker is registered, so its node is offloadable. The
coordinator is stopped before the run. The worker's kernel must not run, `offloaded` stays 0,
`declined` rises and the result is the local one.

### `DynamicStateBumpsTheLiveBufferAfterSwap`

A distributed `State` runs two steps with no worker attached, so both steps complete locally: the
first writes one allocation, the second the other. Both versions must rise, and by the same
amount. A master that bumped the list recorded at compile time would raise one version twice and
leave the other.

### `AttachStartFailureLeavesNothingInstalled`

`attach` on a `StubTransport` whose `start()` returns `BindFailed` must return an error
carrying the transport's message, call `start()` once and leave `offload()`
null. A second `attach` on the same context must then succeed.

### `AttemptCapDoesNotOutliveTheRun`

A `Fail` worker fails every dispatch. One graph is compiled once and runs twice. In each run the worker must receive `K_MAXIMUM_OFFLOAD_ATTEMPTS` dispatches before the task
runs locally, so the second run proves the cap of the first did not pin the node. `declined`
rises by one per run and `redispatched` by the cap.

### `RefusesEmptySecretOnNetworkExposure`

On a `StubTransport` that reports `Network`, `attach` with an empty secret must return an error
before `start()` is called. With a 32-byte secret the same transport attaches.

### `RegisteredTaskRunsWithAndWithoutAWorker`, `run_registered_sum`

`record_registered_task`, called through `Graph::extend()`, records the vector-add with no lambda. Run with no cluster, the master's own
registry runs the kernel once. Run with a worker, the worker's registry runs it once and the
master's not at all. Both outputs must be equal.

### `AttachOnAStartedTransportLeavesTheFirstMasterServing`

Finding QC-35. One master is attached on an in-process transport, then a second context attaches
on the same transport object. The transport answers `AlreadyStarted`, so the second `attach` must
return an error and leave its context without a coordinator. A worker started after that must still
register with the first master within 10 s, which it cannot when the failed attach stopped the
transport.

### `DetachedClusterReportsNoWorkers`

Finding QC-36. Two real workers register, and the master supports the vector-add. After
`detach()`, `wait_for_workers(2, 100 ms)` must be false, `alive_workers()` 0 and `supports()`
false, and a copy of the handle must read both gauges as 0 and an empty worker list. The 100 ms
is the limit of a wait that must fail, so the test does not depend on how long it takes.

### `AttachIsRefusedAndDetachDeferredDuringARun`

Finding QC-84. An `Accept` worker holds the only task while `execute()` waits on another thread,
so a run is in flight. `attach` on that context must return `InvalidState` before it calls
`start()` on the new transport, and the installed coordinator, its worker and its pending task
must be as they were. `detach()` must then stop the master, which returns the task and ends the
run with the local result, and leave the coordinator installed. A second `detach()` after the run
must remove it.

### `MovedFromHandleStillNamesTheMaster`

Finding QC-120. A handle is move-constructed and move-assigned from. The sources must still answer
`alive_workers()` and `offloaded()`. `detach()` through the first source must leave `offload()`
null and close `wait_for_workers(0, 0 ms)` on the other two handles, which shows that all three
name one master.

### `RefusesATimeoutBelowOneMillisecond`

Finding QC-38. `attach` with `liveness_timeout` of 0 and with `handshake_timeout` of 0 must each
return a message that names the option and its value, before `start()` is called. `attach` with a
`task_deadline` of 0 and both timeouts at 1 ms must succeed. `Cluster::liveness_timeout(0 ms)` must
return `InvalidState`, and 1 ms must succeed.

### `ShareStopsOnADeviceResidentHandle`

`Cluster::share` on a device-resident handle stops the process for both overloads of Buffer and
for State. The line names `Cluster::share(buffer)` or `Cluster::share(state)`, and the residency.

### `ShareRegistersAHostAccessibleHandle`

`Cluster::share` returns its argument for a named Buffer and a named State, and the moved buffer
for a temporary. Each shared allocation, both fields of the State among them, then has a handle
in `buffer_handle()`. A moved-from handle that owns nothing is skipped, and a buffer nobody
shared has no handle.

### `WorkerAgentSealsItsRegistry`

Finding QC-37. A registry is open until a `WorkerAgent` is built with it and sealed from then on,
before `start()`. `register_kernel` must then stop the process, which the death test checks.

### The per-run offloaded count

No test of this file reads `RunReport`. That `tasks_offloaded_to_network` counts the dispatches
of one run is pinned by `OffloadedCountIsOfThisRun` in
`modules/surface/api/tests/graph/running/test_run_driver.cpp`, with a coordinator that only counts.

## test_distributed_jepsen.cpp

The file is a soak test in the style of Jepsen. It runs an offload graph under a seeded fault
policy and a nemesis thread, which between them drop, repeat, park, reorder and partition frames.
It asserts determinism under faults: whatever the schedule, every output must end equal to a
fault-free run (`out == 2 * in`). It also asserts liveness: `execute()` must return.

The runtime is an offload accelerator with one authoritative writer, the master. For that model
the determinism check stands in for a linearizability check and costs far less.

### `run_one_scenario`

Two workers register before any fault is injected, so the scenario exercises recovery at task
time. The master runs with a liveness timeout of 250 ms and a task deadline of 300 ms, the workers
with a heartbeat of 40 ms and a pull idle timeout of 120 ms.

The graph runs on a `BackgroundRun` with a limit of 30 s. When the limit passes, the scenario
stops the coordinator, which returns every node it holds, and reports the hang.

Before the outputs are read, the nemesis heals the fabric, both workers stop and the cluster
detaches. From then on no thread writes an output.

### `seeded_faults`, `Nemesis`

`seeded_faults` drops 8 frames in 100, repeats 4 and parks 8. The fabric calls a policy from
several threads with no lock held, so the policy locks its own random source. `Nemesis` installs
the policy and runs a thread that flushes parked frames in a shuffled order every 25 ms and, one
pass in eight, cuts one worker's link for 60 ms. The two intervals pace the faults; the thread
waits on a condition variable, so `heal()` ends it at once.

### `RegistrationSurvivesMessageLoss`, `drop_first_of`

`drop_first_of` drops the first frame of each listed type. The test loses the first `HELLO`, the
first `CHALLENGE` and the first `PROOF`. The worker sends `HELLO` again with a fresh nonce after
each heartbeat interval, so it must register; the policy must have dropped exactly three frames,
and the task that follows must run on the worker.

### `RegistrationSurvivesALostWelcome`

The same run with only the first `WELCOME` lost. The master has registered the worker by then and
the worker has not heard it. The test asserts that the task still runs on the worker.

## test_distributed_tcp_faults.cpp

The file runs a master and a worker in one process over TCP on `127.0.0.1` and breaks the link
between them. The master is a `Cluster` on a `Runtime` and binds port 0; the test reads the
port back after `attach`. The worker is a `WorkerAgent` on a second runtime with one kernel, which
writes each input byte combined with 0x5a to the output. Every test needs a SYCL device and is
reported as skipped without one. Every wait is on a condition and bounded at 60 s: a socket read,
`wait_for_workers`, `wait_for_end`, a future, or `eventually`, which reads a statistics counter
every 2 ms. The file includes the private headers `socket.hpp`, `wire_frame.hpp` and
`wire_messages.hpp` by quoted name.

### `tcp_fault_proxy.hpp`

`TCPFaultProxy` listens on a free loopback port and forwards each accepted connection to the
master, with one pump thread per direction. `set_fault` gives a direction one of five faults:
`Pass`, `Stall` (hold every byte and the end of the stream), `Cut` (close every connection and
refuse new ones), `Throttle` (a number of bytes per second) and `Truncate` (forward a number of
bytes, close, then act as `Cut`). Each fault is one rule in `K_FAULT_RULES`, and the pump asks the
table. The proxy uses `Socket` and `Listener` of the module, so it holds no platform check.
`retarget` sends later connections to another master.

### `StrayConnectionTakesNoSlot`

Finding DIST-04. A connection that never sends HELLO stays open while a worker registers. The
master closes it after the 500 ms handshake timeout without sending it a byte, and it never counts
as a worker.

### `GarbageBytesClosedMasterLives`, `V1FrameRejected`, `OversizedLengthClosesPeerNotProcess`

Findings DIST-15 and DIST-16. A raw socket writes 12 bytes: the text `GET / HTTP/1`, a version 1
frame (a 32-bit length and a type byte), or a version 3 header that announces 1 MiB plus one byte.
The master closes the connection with no reply and `peers_closed_malformed` reaches 1. A worker
then registers, and in the first and the third test a task runs on it.

### `WrongSecretRejected`

Finding DIST-03. A worker with another secret ends with `RejectedAuthentication`: it checks the
master's proof first and sends no PROOF, so the master counts nothing for it. A raw peer then
answers CHALLENGE with a proof of 0x44 bytes, and `handshake_rejected_auth` is 1.

### `VersionMismatchLoggedOncePerConnection`

Finding DIST-06. Two connections each send two headers of version 3. A sink installed with
`set_log_sink` counts the ERROR lines that hold `protocol v3`: two after the master is
detached, one for each connection, and `handshake_rejected_version` is 2.

### `MasterKilledWorkerSurvivesAndReconnects`

Findings DIST-01 and DIST-07. The proxy cuts the link and the master is detached. A second master
attaches on another port and the proxy is retargeted. The worker registers with it, counts one
reconnect, and a task runs on it.

### `WorkerCutMidChunkTaskFinishesLocally`

The proxy closes the link 100 000 bytes into a 1 MiB input sent in 64 KiB chunks, and the worker
does not reconnect. The kernel never runs, the master takes the task back and the local fallback
writes the output. `peers_lost` and `redispatched` are both 1 as soon as the run returns, because
the master raises them before it hands the node back.

### `StalledLinkReapedThenWorkerRejoins`

Finding DIST-05. With a 300 ms liveness timeout the proxy holds every byte until the master has
reaped the worker. The test raises the timeout to 60 s and lets the bytes pass; the worker
registers again on a new connection and runs a task.

### `LinkStalledMidTransferLostAtLivenessClose`

A 32 MiB input is throttled to 8 MiB/s and the proxy stalls the link once 1 MiB has passed. The
master's write idle timeout is 10 minutes, its liveness timeout 2 s and its task deadline 0, so
only the liveness close can end the wait. By then the writer has waited in one socket write for
longer than the connection's 1 s linger, so `close()` shuts the socket: the peer is lost, the
task is resubmitted and the local fallback writes the output.

### `ThrottledTransferKeepsWorkerAlive`

Finding DIST-05. An 8 MiB input crosses at 4 MiB/s under a 500 ms liveness timeout, which takes
about 2 s; the test checks that it took at least 1.5 s. No worker is lost and no task is reaped,
because every chunk and every heartbeat counts as a sign of life.

### `ManyChunkBufferCrossesIntact`

Finding DIST-12. 1 MiB goes to the worker and 1 MiB comes back at `chunk_bytes` 4 KiB: 256 chunks
each way, no abort and no stale writeback.

### `DispatchReturnsWhilePeerStalled`

Finding DIST-14. While the proxy holds every byte, a graph with one offloadable node and one
native node is started. The native node runs, `offloaded` reaches 1 and the task stays pending;
the run ends on the worker once the bytes pass.
