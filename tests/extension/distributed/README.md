# distributed tests

The tests of the `distributed` module, mirroring `modules/extension/distributed/`.
`tests/extension/distributed/CMakeLists.txt` builds them into `sr_distributed_tests` and
`sr_distributed_benchmarks`, built under `SR_BUILD_BENCHMARKS`. `sr test` runs them with the rest;
`sr run sr_distributed_tests -- --gtest_filter='<pattern>'` runs one program alone.

A benchmark prints its figures and asserts correctness and forward progress; it asserts a
speed only where its section says so.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## Helpers

The headers below are shared by tests of this module only, so they sit in this folder.

| Header | What it does |
| --- | --- |
| `tcp_fault_proxy.hpp` | A loopback proxy that stalls, cuts, throttles or truncates a link; see its section. |

## benchmark_cluster.cpp

The benchmark measures the round trip of an offloaded task: dispatch, the worker's pull of the
input, the kernel, the output chunks written back into the master's buffer, and the result. It
is built only with `SR_ENABLE_DISTRIBUTED`. Master and workers run in one process, each worker a
`WorkerAgent` on a runtime of its own. The kernel writes each input element plus one, and every
task is recorded with `record_registered_task`, so the local fallback is the same kernel. The policy
is `LeastLoadedOffloadPolicy(0)`, which offloads a task whatever its size.

Each case runs once untimed, which pays the first touch of the buffers and the worker's cache
allocation, then five timed runs, of which the median is reported. Before a run the inputs are
rewritten and bumped, so a worker pulls them again instead of reading its cache. The timed
region is the `run()` call alone: the refill, the graph build and the check of the outputs stay
outside it.

`OffloadRoundTripThroughput` runs 256 independent tasks of 256 floats each on two workers over
the in-process fabric. It prints milliseconds per run, microseconds per task, tasks per second,
the dispatches offloaded and taken back in the timed runs, and the kernels each worker has run
since it started, which is the split the policy made. It asserts that every output is right and
that at least one dispatch was offloaded.

`RoundTripByBufferSize` runs one task with one input and one output of 64 KiB, 4 MiB and 64 MiB,
first over the in-process fabric and then over TCP on a loopback port the OS chooses, with one
worker. It prints milliseconds per round trip and MiB per second, counting the input going out
and the output coming back. It asserts that every output is right, that each of the five timed
runs was offloaded and that no task was taken back.

Neither test asserts a speed.

## test_distributed.cpp

The file runs offload end to end on a real SYCL device, with a master and its workers in one
process. Most tests use a real `WorkerAgent` on the in-process fabric; the tests that need a worker
to misbehave use `ScriptedWorker`. Every test skips when SYCL reports no device.

Float buffers hold whole numbers below 2^24, which float addition keeps exact, so `K_TOLERANCE` is
0 and a comparison is an equality.

| Test | What it proves |
| --- | --- |
| `USMFloats`, `LocalWorker`, `BackgroundRun` | `USMFloats` owns one shared USM allocation and frees it. `LocalWorker` owns a worker's `Runtime` and its `WorkerAgent`. `BackgroundRun` calls `Graph::run()` on its own thread; a test waits for it at most `K_WAIT` (10 s), so a run that never returns fails that test and the suite goes on. A run that did not return is detached, not joined. |
| `vec_add_kernel`, `add_vec_add` | The kernel takes `span<const BufferView>` and reads the element count from the output's `size_bytes`. Each registry counts its kernel's runs in an atomic the test owns, which is how a test tells a remote run from the local fallback. `add_vec_add` adds the op-tagged task with a fallback lambda that computes the same sum. |

### `ScriptedWorker`

A worker endpoint that implements `TransportEvents` itself. It sends `HELLO`, answers `CHALLENGE`
with a `PROOF` under the empty secret, and answers `PING` with `PONG`, so the master counts it
alive. It records each `TASK_DISPATCH` and wakes the test through a condition variable. What it
does next is one of three scripts: `Accept` sends `TASK_ACCEPTED` and holds the task, `Fail` also
sends `TASK_RESULT` with `KernelFailed`, `FallSilent` sends nothing from then on. Its transport
does not reconnect. `report(facts)` sets the name, system, labels and devices its `HELLO` carries;
without it the name is `scripted-worker` and the rest is empty. `send_stream` hands the master a `ScriptedFrames` source, because a frame that
carries the last-chunk flag can only travel in a stream.

| Test | What it proves |
| --- | --- |
| `StubTransport` | An `Transport` that carries nothing: `exposure()` and `start()` return what the test set, and `start_calls()` counts the starts. |
| `eventually` | The master's `stale_writebacks` counter has no event to wait on. `eventually` re-reads a condition, yielding between reads, until it holds or `K_WAIT` passes. No test sleeps. |

### `OffloadVectorAddRoundTrip`, `FanOutAcrossTwoWorkers`, `OffloadOverTCPLoopback`

One task, or two on two workers, goes through the whole chain: `prepare`, dispatch, the buffer
pull, the kernel on the worker, the output chunks and the completion. The fan-out test asserts the
total of the two workers' run counters, not a split: which worker gets which task depends on
timing. The TCP test binds `TCPMasterOptions` defaults (127.0.0.1, port 0) and reads the port from
`local_endpoint()` after `attach`.

| Test | What it proves |
| --- | --- |
| `AutoVersionBumpAcrossLocalWriteThenOffloadedRead` | A local task with no operation writes a shared buffer and an offloaded task reads it. The buffer's version must rise, and the worker must compute from the values the local task wrote. The worker's kernel runs once, so the writer ran on the master. |
| `WorkerPullsTheNewVersionAfterALocalWrite` | One offloaded read puts version 1 of a shared buffer in the worker's cache. A local task with no operation then rewrites the buffer and a second offloaded read follows. The version rises, `chunks_sent` grows, so the worker pulled again, and the output holds the new bytes. This is the optimizer-then-replica step of a training run. |
| `TwoLocalWritesBeforeADispatchCostOnePull` | Two local writes of one buffer, in order, before one offloaded read. The version rises by two, one per write, and `chunks_sent` doubles, so the worker pulled once for the two. |
| `WorkerDeathFallsBackToLocal` | A `FallSilent` worker takes the dispatch and stops answering. With a liveness timeout of 300 ms the master closes it and resubmits the task, which finishes locally. Until the dispatch the worker answers pings, so it cannot be closed early on a slow machine. |

### `PolicySeesWorkerFactsAcrossReconnect`

A `FactsRecordingPolicy` picks the first worker it is offered and keeps a copy of that load's
`facts`. A `ScriptedWorker` named `rack-7-gpu` registers with the labels `zone=a` and `tier=fast`,
one GPU and one CPU, and runs one offloaded task to its result. It leaves; the test waits for
`alive_workers()` to reach 0. A second `ScriptedWorker` registers under the same name with
`zone=b` and runs a second task. The policy must have been called twice, each time with facts
whose name, system, labels and devices (kind, name, memory) are the ones that worker sent, the
second with `zone=b`.

### Placement rules

The tests set one rule named `pin` that matches the class `test.pinned` with the key 7, and record
one vec-add node of that class and key. The workers are `ScriptedWorker`s whose facts list a CPU,
then a GPU. A wait is observed through `ClusterStatistics::held_nodes`, never through a sleep.

| Test | What it proves |
| --- | --- |
| `Medium_DistributedPlacementTest.RulePinsANodeToTheWorkerItNames` | With a decoy worker registered first, a rule for `machine_name` `rack-7-gpu` and `device_kind` GPU sends the node to that worker with `device_index` 1, and the decoy gets nothing. |
| `Medium_DistributedPlacementTest.RulePinsANodeToTheWorkerItsLabelNames` | The same with `machine_label` `zone=b` and `device_index` 1. |
| `Medium_DistributedPlacementTest.WaitHoldsTheNodeUntilTheWorkerRegisters` | Under `Wait`, with no worker, the node is held; the named worker registers and gets the node on device 0. The bound is `K_WAIT`, so only a hang reaches it. |
| `Medium_DistributedPlacementTest.WaitRunsTheNodeLocallyOnceTheBoundPasses` | Under `Wait` with a 30 ms bound and no worker, the run returns no sooner than the bound, the master computed the sum, and nothing was offloaded. |
| `Medium_DistributedPlacementTest.FailEndsTheRunNamingTheRuleAndTheTarget` | Under `Fail`, with no worker, `run()` returns `InvalidState` whose message names the rule `"pin"` and `machine "rack-7-gpu"`; the output stays unwritten and nothing is offloaded. |
| `Medium_DistributedPlacementTest.RuleDeadlineTakesASilentTaskBackSooner` | With the liveness timeout and `task_deadline` at 60 s, a rule deadline of 100 ms takes a `FallSilent` worker's task back, counted as `tasks_reaped_deadline`, and the master computes the sum within `K_WAIT`. |
| `Medium_DistributedPlacementTest.BusyTargetRunsBothNodesOneAfterTheOther` | A worker whose `max_jobs` is 1 gets the first of two pinned nodes; the second is held, then dispatched to it once the first result frees the slot. Both outputs hold what the worker wrote and nothing was redispatched, so neither ran on the master. `ScriptedWorker::limit_jobs()` sets the limit `PROOF` reports. |
| `Medium_DistributedPlacementTest.UnmatchedNodeIsPlacedByThePolicy` | A rule for another key leaves the node to the policy, which is called once, and the dispatch carries device 0. |
| `Medium_DistributedPlacementTest.Regression_ADeviceOnlyRuleTheMasterLacksOffloads` | A master built on CPU devices only, under a rule with `device_kind` GPU and no machine part, sends the node to the worker with a GPU, on its `device_index` 1. Skipped when SYCL has no CPU device. |
| `Medium_DistributedPlacementTest.Regression_ARegionsNodeInAScopeReachesTheNamedWorker` | A task recorded in region 0 of a `DynamicGraph`, inside the scope a rule matches, reaches the worker the rule names, on device 1. |

### `StaleWritebackIsFenced`

An `Accept` worker holds the dispatched task. The test first streams an output chunk at the
dispatched version minus one, filled with 999, and waits for `stale_writebacks` to reach 1; the
output must still hold 7. It then streams a chunk at the dispatched version, filled with 42,
followed by a succeeded `TASK_RESULT`. The run must return with 42 in the output, one chunk
received, one refused and nothing resubmitted.

| Test | What it proves |
| --- | --- |
| `LostBufferPushTimesOutInsteadOfHanging` | The fault policy drops every `BUFFER_CHUNK` from master to worker, so no input arrives. With a pull idle timeout of 200 ms the worker reports `InputUnavailable`; the master resubmits, and after the attempt cap the task runs locally. The kernel must not have run on the worker. |
| `FluentClusterOffload`, `FluentDistributedStateParity`, `evolve_distributed_state` | The fluent surface builds the same offload. A `State` swaps its two allocations every step, so its handles are resolved at each dispatch; `evolve_distributed_state` runs 4 and 5 steps, which leave the result in different allocations, and both must match `seed[i] + steps * (i % 7)`. |
| `ClusterDetachAndDisplacement`, `ClusterHandleOutlivesRuntime` | `detach()` leaves `offload()` null and a second call does nothing. A later `attach()` displaces the installed master, and the displaced handle's `detach()` leaves the new one installed. A handle that outlives its context still answers `alive_workers()` and `offloaded()`. |
| `StopResubmitsPendingNodes` | An `Accept` worker holds the only task while `execute()` waits. The test calls `stop()` on the installed coordinator. The run must return with the local result, `redispatched` must be 1 and `pending_tasks` 0. |
| `TryOffloadAfterStopRunsLocally` | `compile()` builds the plan while a real worker is registered, so its node is offloadable. The coordinator is stopped before the run. The worker's kernel must not run, `offloaded` stays 0, `declined` rises and the result is the local one. |

### `DynamicStateBumpsTheLiveBufferAfterSwap`

A distributed `State` runs two steps with no worker attached, so both steps complete locally: the
first writes one allocation, the second the other. Both versions must rise, and by the same
amount. A master that bumped the list recorded at compile time would raise one version twice and
leave the other.

| Test | What it proves |
| --- | --- |
| `AttachStartFailureLeavesNothingInstalled` | `attach` on a `StubTransport` whose `start()` returns `BindFailed` must return an error carrying the transport's message, call `start()` once and leave `offload()` null. A second `attach` on the same context must then succeed. |
| `AttemptCapDoesNotOutliveTheRun` | A `Fail` worker fails every dispatch. One graph is compiled once and runs twice. In each run the worker must receive `K_MAXIMUM_OFFLOAD_ATTEMPTS` dispatches before the task runs locally, so the second run proves the cap of the first did not pin the node. `declined` rises by one per run and `redispatched` by the cap. |
| `RefusesEmptySecretOnNetworkExposure` | On a `StubTransport` that reports `Network`, `attach` with an empty secret must return an error before `start()` is called. With a 32-byte secret the same transport attaches. |
| `RegisteredTaskRunsWithAndWithoutAWorker`, `run_registered_sum` | `record_registered_task`, called through `Graph::extend()`, records the vector-add with no lambda. Run with no cluster, the master's own registry runs the kernel once. Run with a worker, the worker's registry runs it once and the master's not at all. Both outputs must be equal. |

### `AttachOnAStartedTransportLeavesTheFirstMasterServing`

Finding QC-35. One master is attached on an in-process transport, then a second context attaches
on the same transport object. The transport answers `AlreadyStarted`, so the second `attach` must
return an error and leave its context without a coordinator. A worker started after that must still
register with the first master within 10 s, which it cannot when the failed attach stopped the
transport.

| Test | What it proves |
| --- | --- |
| `DetachedClusterReportsNoWorkers` | Finding QC-36. Two real workers register, and the master supports the vector-add. After `detach()`, `wait_for_workers(2, 100 ms)` must be false, `alive_workers()` 0 and `supports()` false, and a copy of the handle must read both gauges as 0 and an empty worker list. The 100 ms is the limit of a wait that must fail, so the test does not depend on how long it takes. |

### `AttachIsRefusedAndDetachDeferredDuringARun`

Finding QC-84. An `Accept` worker holds the only task while `execute()` waits on another thread,
so a run is in flight. `attach` on that context must return `InvalidState` before it calls
`start()` on the new transport, and the installed coordinator, its worker and its pending task
must be as they were. `detach()` must then stop the master, which returns the task and ends the
run with the local result, and leave the coordinator installed. A second `detach()` after the run
must remove it.

| Test | What it proves |
| --- | --- |
| `MovedFromHandleStillNamesTheMaster` | Finding QC-120. A handle is move-constructed and move-assigned from. The sources must still answer `alive_workers()` and `offloaded()`. `detach()` through the first source must leave `offload()` null and close `wait_for_workers(0, 0 ms)` on the other two handles, which shows that all three name one master. |

### `RefusesATimeoutBelowOneMillisecond`

Finding QC-38. `attach` with `liveness_timeout` of 0 and with `handshake_timeout` of 0 must each
return a message that names the option and its value, before `start()` is called. `attach` with a
`task_deadline` of 0 and both timeouts at 1 ms must succeed. `Cluster::liveness_timeout(0 ms)` must
return `InvalidState`, and 1 ms must succeed.

| Test | What it proves |
| --- | --- |
| `ShareStopsOnADeviceResidentHandle` | `Cluster::share` on a device-resident handle stops the process for both overloads of Buffer and for State. The line names `Cluster::share(buffer)` or `Cluster::share(state)`, and the residency. |

### `ShareRegistersAHostAccessibleHandle`

`Cluster::share` returns its argument for a named Buffer and a named State, and the moved buffer
for a temporary. Each shared allocation, both fields of the State among them, then has a handle
in `buffer_handle()`. A moved-from handle that owns nothing is skipped, and a buffer nobody
shared has no handle.

| Test | What it proves |
| --- | --- |
| `WorkerAgentSealsItsRegistry` | Finding QC-37. A registry is open until a `WorkerAgent` is built with it and sealed from then on, before `start()`. `register_kernel` must then stop the process, which the death test checks. |

### The per-run offloaded count

No test of this file reads `RunReport`. That `tasks_offloaded_to_network` counts the dispatches
of one run is pinned by `OffloadedCountIsOfThisRun` in
`tests/surface/api/graph/plumbing/test_step_driver.cpp`, with a coordinator that only counts.

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

| Test | What it proves |
| --- | --- |
| `RegistrationSurvivesMessageLoss`, `drop_first_of` | `drop_first_of` drops the first frame of each listed type. The test loses the first `HELLO`, the first `CHALLENGE` and the first `PROOF`. The worker sends `HELLO` again with a fresh nonce after each heartbeat interval, so it must register; the policy must have dropped exactly three frames, and the task that follows must run on the worker. |

### `RegistrationSurvivesALostWelcome`

The same run with only the first `WELCOME` lost. The master has registered the worker by then and
the worker has not heard it. The test asserts that the task still runs on the worker.

The file runs a master and a worker in one process over TCP on `127.0.0.1` and breaks the link
between them. The master is a `Cluster` on a `Runtime` and binds port 0; the test reads the
port back after `attach`. The worker is a `WorkerAgent` on a second runtime with one kernel, which
writes each input byte combined with 0x5a to the output. Every test needs a SYCL device and is
reported as skipped without one. Every wait is on a condition and bounded at 60 s: a socket read,
`wait_for_workers`, `wait_for_end`, a future, or `eventually`, which reads a statistics counter
every 2 ms. The file includes `socket.hpp` of `network_socket` and `wire_frame.hpp` and
`wire_messages.hpp` of `wire`.

### `tcp_fault_proxy.hpp`

`TCPFaultProxy` listens on a free loopback port and forwards each accepted connection to the
master, with one pump thread per direction. `set_fault` gives a direction one of five faults:
`Pass`, `Stall` (hold every byte and the end of the stream), `Cut` (close every connection and
refuse new ones), `Throttle` (a number of bytes per second) and `Truncate` (forward a number of
bytes, close, then act as `Cut`). Each fault is one rule in `K_FAULT_RULES`, and the pump asks the
table. The proxy uses `Socket` and `Listener` of the module, so it holds no platform check.
`retarget` sends later connections to another master.

| Test | What it proves |
| --- | --- |
| `StrayConnectionTakesNoSlot` | Finding DIST-04. A connection that never sends HELLO stays open while a worker registers. The master closes it after the 500 ms handshake timeout without sending it a byte, and it never counts as a worker. |

### `GarbageBytesClosedMasterLives`, `V1FrameRejected`, `OversizedLengthClosesPeerNotProcess`

Findings DIST-15 and DIST-16. A raw socket writes 12 bytes: the text `GET / HTTP/1`, a version 1
frame (a 32-bit length and a type byte), or a version 4 header that announces 1 MiB plus one byte.
The master closes the connection with no reply and `peers_closed_malformed` reaches 1. A worker
then registers, and in the first and the third test a task runs on it.

| Test | What it proves |
| --- | --- |
| `WrongSecretRejected` | Finding DIST-03. A worker with another secret ends with `RejectedAuthentication`: it checks the master's proof first and sends no PROOF, so the master counts nothing for it. A raw peer then answers CHALLENGE with a proof of 0x44 bytes, and `handshake_rejected_auth` is 1. |
| `VersionMismatchLoggedOncePerConnection` | Finding DIST-06. Two connections each send two headers of version 2. A sink installed with `set_log_sink` counts the ERROR lines that hold `protocol v2`: two after the master is detached, one for each connection, and `handshake_rejected_version` is 2. |
| `MasterKilledWorkerSurvivesAndReconnects` | Findings DIST-01 and DIST-07. The proxy cuts the link and the master is detached. A second master attaches on another port and the proxy is retargeted. The worker registers with it, counts one reconnect, and a task runs on it. |

### `WorkerCutMidChunkTaskFinishesLocally`

The proxy closes the link 100 000 bytes into a 1 MiB input sent in 64 KiB chunks, and the worker
does not reconnect. The kernel never runs, the master takes the task back and the local fallback
writes the output. `peers_lost` and `redispatched` are both 1 as soon as the run returns, because
the master raises them before it hands the node back.

| Test | What it proves |
| --- | --- |
| `StalledLinkReapedThenWorkerRejoins` | Finding DIST-05. With a 300 ms liveness timeout the proxy holds every byte until the master has reaped the worker. The test raises the timeout to 60 s and lets the bytes pass; the worker registers again on a new connection and runs a task. |

### `LinkStalledMidTransferLostAtLivenessClose`

A 32 MiB input is throttled to 8 MiB/s and the proxy stalls the link once 1 MiB has passed. The
master's write idle timeout is 10 minutes, its liveness timeout 2 s and its task deadline 0, so
only the liveness close can end the wait. By then the writer has waited in one socket write for
longer than the connection's 1 s linger, so `close()` shuts the socket: the peer is lost, the
task is resubmitted and the local fallback writes the output.

| Test | What it proves |
| --- | --- |
| `ThrottledTransferKeepsWorkerAlive` | Finding DIST-05. An 8 MiB input crosses at 4 MiB/s under a 500 ms liveness timeout, which takes about 2 s; the test checks that it took at least 1.5 s. No worker is lost and no task is reaped, because every chunk and every heartbeat counts as a sign of life. |
| `ManyChunkBufferCrossesIntact` | Finding DIST-12. 1 MiB goes to the worker and 1 MiB comes back at `chunk_bytes` 4 KiB: 256 chunks each way, no abort and no stale writeback. |
| `DispatchReturnsWhilePeerStalled` | Finding DIST-14. While the proxy holds every byte, a graph with one offloadable node and one native node is started. The native node runs, `offloaded` reaches 1 and the task stays pending; the run ends on the worker once the bytes pass. |

## test_in_process_transport.cpp

The file needs no hardware. `InProcessTransport` also runs the conformance suite of
`test_tcp_transport.cpp` as its reference.

### `InProcessTransportDeliversBothDirections`

A master endpoint and a worker endpoint each start with a recording `TransportEvents`. The test
waits on the `on_peer_connected` event of both sides, sends one control frame each way and checks
that each side sees the frame under the `PeerID` its own `on_peer_connected` named, with the type,
flags 0 and the payload it was sent.

## test_kernel_registry.cpp

### `SealedRegistryStopsARegistration`, `EmptyKernelStopsTheRegistration`

Finding QC-37. `Medium_KernelRegistryDeathTest` needs no queue: its kernel returns a default event
and is never called. After `seal()`, called twice, `register_kernel` must stop the process for a
new operation and for one already registered, and the registry must hold what it held. A
default-constructed `Kernel` must stop it on an open registry and on a sealed one, with the
message that names the empty kernel.

## test_registered_task/test_nested_run_probe.cpp

The file probes whether a registered kernel may run a compiled graph from inside its own body, as
SushiAI's replica does with a whole compiled step (R11 of `docs/design/LAYERED_EXECUTION/README.md`).
The kernel runs a one-task graph, `out = 2 * in`, and records how its `Graph::run()` ended. A worker
runs each dispatch as a task of its runtime's run, and the master runs a local fallback as a task
of its own run, so in both places the kernel's body is inside a run. Every master run goes through
`BoundedRun`, which fails a test after 10 s instead of hanging it. Every test skips when SYCL
reports no device.

| Test | What it proves |
| --- | --- |
| `WorkerKernelRunningAGraphOfItsOwnRuntimeIsRefused` | On a worker, the nested `run()` on the worker's own runtime returns `InvalidState`, the inner output stays unwritten, the dispatch still succeeds, and the same graph runs once the dispatch is over. |
| `WorkerKernelRunsAGraphOfASecondRuntime` | On a worker, a graph built on a second `Runtime` of the process runs to its end and writes the right output. |
| `MasterFallbackRunningAGraphOfItsOwnRuntimeIsRefused` | On the master, the same refusal, with the task's body run locally; the runtime is usable after the run. |
| `MasterFallbackRunsAGraphOfASecondRuntime` | On the master, a graph of a second `Runtime` runs to its end and writes the right output. |

## test_shared_secret.cpp

The tests need no hardware and no network. They cover `SharedSecret`; the hash, the HMAC and the
handshake proofs it feeds are tested in `tests/extension/link_security/`.

| Test | What it proves |
| --- | --- |
| `ScratchFile` | `ScratchFile` writes a file into the working directory of the test program, which is inside the build tree, and removes it when the test ends. The `SharedSecret` tests read their secrets from such files: 31 bytes must be refused with the file name and the length in the message, 32 bytes must be accepted byte for byte, and a missing file must be refused with its path in the message. |

## test_tcp_transport.cpp

The file pins the `Transport` contract as `transport.hpp` writes it and needs no hardware. The
suite `Medium_TransportConformanceTest` is typed over two rigs, so each test runs once over TCP on
`127.0.0.1` with port 0 and once on an `InProcessFabric`, the reference that `TCPTransport` must
match. The tests wait on recorded events with a 10 s limit and never on a sleep.

| Test | What it proves |
| --- | --- |
| `EventRecorder` | `EventRecorder` implements `TransportEvents`, copies every event into a list and lets a test wait until the list satisfies a condition. A test may set a reaction, which runs inside the event on the transport's thread, outside the recorder's lock. |
| `DeliversInOrderPerPeer` | Each end queues 500 control frames that carry a counter. Each receiver must see the counters in order, and the byte counters must cover 16 bytes for each frame. |

### `PeerLostOnceAfterLastFrame`

The worker queues 100 frames and calls `close_peer()`. The master must record the connection, the
100 frames in order and one `ClosedByPeer` loss, 102 events in all, counted after both transports
have stopped. The worker must record one `ClosedLocally` loss and then one `on_connect_abandoned()`,
since its reconnect is off. A send to the closed peer returns `UnknownPeer` on both ends.

| Test | What it proves |
| --- | --- |
| `NoCallbackAfterStop` | A thread floods the master with frames while the test stops the master. The number of events the master recorded when `stop()` returned must not grow afterwards, and `stop()` must report no loss. |
| `SendFromCallbackAllowed` | The master sends from `on_peer_connected()` and echoes every frame from inside `on_frame()`; the worker calls `close_peer()` from inside the event of the last echo. Every send must return `Queued` and the echoes must arrive in order, which fails if a transport holds a lock across an event. |

### `SendAfterStopReturnsStopped`

`send()` returns `Stopped` before `start()` and after `stop()`, and `UnknownPeer` in between for an
id that names nobody. `send_stream()` on a stopped transport destroys its source before it returns.
A second `start()` returns `AlreadyStarted`, also after `stop()`, and a second `stop()` does
nothing.

| Test | What it proves |
| --- | --- |
| `ControlFrameOvertakesStream` | A stream of 8 chunks holds its second chunk until the test has queued a control frame. The control frame must arrive after at most 2 chunks, and the chunks must arrive in order with the last-chunk flag on the eighth only. |
| `GatedSource` | `GatedSource` is the stream of that test. Its wait stands in for a writer that is busy with one chunk; a production `FrameSource` never waits. |

## test_worker_agent.cpp

The tests drive one `WorkerAgent` through its transport and open no socket. `AgentRig` builds the
agent on a runtime of its own over a fake transport, which records every frame the agent sends and
every peer it closes, hands it the master's frames, and either pulls an output stream dry on the
calling thread or keeps it until the test releases it. `register_master` plays a master holding the
same 32-byte secret: it answers the agent's `HELLO` with a `CHALLENGE` proved for the nonce that
`HELLO` carried, waits for `PROOF` and sends `WELCOME`. The tests read the agent's counters from
`statistics()`. Every wait is on a condition, bounded at 30 s.

The cache tests set `cache_bytes` and fill the cache with the outputs of tasks; a buffer is known to
be cached when a task that reads it sends no `BUFFER_REQUEST`. The executor tests dispatch tasks
and read their `TASK_RESULT`s. The link tests play the handshake and the liveness of the master.

| Test | What it proves |
| --- | --- |
| `ReleaseEvicts` | `BUFFER_RELEASE` frees an unpinned output at once and counts an eviction. An output whose stream waits to be pulled is freed at once too, because the stream holds a copy; the stream then delivers its result. |
| `BudgetEvictsLeastRecentlyUsedUnpinned` | Three outputs of 100 bytes fill a 300-byte budget and the first is read again. A fourth output makes the cache free the second, the one used longest ago: a task that reads it pulls it, and one that reads the first does not. |
| `PinnedEntryIsNotEvicted` | A 300-byte cache holds one unpinned output. Task 2 pins an output and an input whose pull stays open while tasks 3 and 4 queue, so all three join one batch. Task 3's output evicts the unpinned buffer; task 4 finds every byte pinned and ends as `OutputAllocationFailed`, and nothing pinned was evicted. |
| `GapInAPulledBufferDiscardsIt` | A chunk at offset 8 after a chunk that ended at 4 ends the pull: the task ends as `InputUnavailable` and the memory is returned. The same buffer is then pulled in three chunks, and the kernel reads every byte the master sent. |
| `OutputAllocationFailureFailsTheTask` | With a 32-byte cache, a task with a 64-byte output ends in a `TASK_RESULT` with `OutputAllocationFailed`, its kernel never runs and no chunk is sent. The next task, whose output fits, succeeds on the same exec thread. |
| `KernelSeesBufferSizes` | The kernel reads `size_bytes` of its input and its output views: 12 and 20. The input comes from a `BUFFER_REQUEST` the test answers with two chunks, and the output leaves in chunks of 8 bytes. |
| `GenericLambdaRegistersAsAKernel` | `register_kernel` has one overload, so a lambda whose four parameters are all `auto` registers without a cast. It fills its output with 0x5a, and the four bytes come back in one chunk. |
| `KernelThatThrowsFailsTheTaskAndTheWorkerLives` | A kernel that throws produces `KernelFailed` with no outputs and no chunk. The next dispatch runs. |
| `OutputStreamEndsWithTheResult` | The transport keeps the stream. Until it is pulled no `TASK_RESULT` exists, and a `BUFFER_RELEASE` frees the cached output at once. Pulled, the stream gives chunks of 4, 4 and 2 bytes of its own copy, the last one flagged, then `TASK_RESULT`. |
| `DispatchRunsOnTheDeviceItNames` | Two dispatches name devices 0 and 1, and the kernel records the device of the queue it is handed: both succeed, on two devices. Skipped on a worker with fewer than two devices. |
| `DispatchNamingAMissingDeviceFailsTheTask` | A dispatch for a device index the worker lacks ends in `KernelFailed` and its kernel never runs. The next dispatch, on device 0, succeeds. |

### Batches

`deliver_as_one_batch` makes two dispatches share one run: the first asks for an input, the second
is delivered and accepted while that pull is open, and then the input's one chunk arrives.
`Rendezvous` lets a kernel enter, wait for a partner up to a bound, and leave, and keeps the most
kernels inside at once. `first_output_of` returns the first output chunk a task sent.

| Test | What it proves |
| --- | --- |
| `DispatchesOverDisjointBuffersAreInFlightTogether` | Two dispatches with disjoint outputs, on devices 0 and 1 when the worker has two and both on device 0 otherwise, have both kernels inside the rendezvous at once; with two devices they ran on two. |
| `DispatchesWritingOneBufferRunOneAfterTheOther` | Two dispatches that write buffer 6 never overlap: the first waits 250 ms for a partner that does not come. They run in dispatch order, and each sends the bytes it wrote, so the second writer did not change the first one's output. |
| `OneRunPullsAnInputTwoDispatchesReadOnce` | Two dispatches of one batch that read one input version send one `BUFFER_REQUEST`, and both kernels run. |
| `ADispatchReadingAQueuedOutputRunsAfterIt` | A dispatch that reads the output of a dispatch queued before it opens the next batch, finds that output in the cache, sends no `BUFFER_REQUEST` for it and copies its bytes. |

### `DisconnectEndsAnOpenInputPull`

The agent runs with no pull idle timeout, so a pull ends only when something ends it. A task asks
for its input and the master's connection is lost before any chunk arrives. The task ends as
`InputUnavailable`, its kernel never runs and the cache holds no byte of the abandoned pull. A task
of the next connection then runs on the same exec thread.

| Test | What it proves |
| --- | --- |
| `HandshakeChecksTheMasterBeforeProving` | `HELLO` carries the name and a build id. No `PROOF` leaves before a `CHALLENGE` whose proof verifies; the `PROOF` then carries the worker's proof, its two operations and its capacity, and `WELCOME` registers the agent. |
| `WrongMasterProofEndsTheLink` | A `CHALLENGE` proved with another secret ends the agent with `RejectedAuthentication`, sends no `PROOF` and closes the peer. The next connection is closed without a `HELLO`. |
| `EndedLinkStopsItsTransport` | An agent that ends as `RejectedAuthentication` stops its transport, so the connector opens no further connection. |
| `PingGetsNoPongBeforeRegistration` | Between a good `CHALLENGE` and its `WELCOME` the link answers no `PING`. After `WELCOME` the same `PING` gets one `PONG` with its token. |
| `LateChallengeForAForgottenHelloIsIgnored` | With a heartbeat of 1 ms the agent sends a `HELLO` per beat and remembers the nonces of its last four. A `CHALLENGE` proved against the first nonce sends no `PROOF`, leaves the peer open and does not end the agent, because the proof may answer a `HELLO` it has forgotten. |
| `IdleHeartbeatCarriesTheNextTaskID` | After task 41 finished and nothing is held, the next heartbeat carries `oldest_held_task_id` 42 and a queue depth of 0. |
| `SilentMasterIsLost` | `WELCOME` carries a liveness timeout of 40 ms and no frame follows. The agent closes the peer, and the loss that follows leaves it running. |
| `NewSessionClearsTheCache` | A reconnect to the same `session_id` keeps the cache and the ids seen, so a dispatch repeated there runs once. A reconnect to another session clears both, and `reconnects` counts 2. |
| `LossAfterGoodbyeIsTheWorkersOwn` | A registered agent stops and says `GOODBYE` once. The loss that follows leaves its end `Stopped`, not `MasterLost`. |

## test_worker_main.cpp

The five exit-code tests of `Medium_WorkerMainTest` call `run_worker()` against a master
listening with `TCPTransport::make_master()` on a port the OS chooses, through `ServedWorker`, which
runs the worker on a thread of its own with an empty secret and reconnecting off. `FakeMaster` is a
`TransportEvents` on the master endpoint that plays the frames of one script: reject for
authentication, reject for version, register, or register and close. Each wait for an exit code is
bounded at 60 s.

| Test | What it proves |
| --- | --- |
| `UnreadableSecretFileExitsOne`, `ShortSecretFileExitsOne`, `UnknownLogLevelExitsOne`, `BadFlagExitsOne` | Exit code 1. Each test captures the error stream and finds the file name, `31 bytes` and `at least 32`, the five level names, or the parser's line. All four return before a runtime is built. |
| `VersionPrintsTheProtocolAndExitsZero` | `--version` prints `protocol v4` and the build, and returns 0. |

### `SignalStopsWithExitZero`

Exit code 0. The fake master welcomes the worker and waits for its first `HEARTBEAT`, which a
worker sends only once it is registered. The test then raises `SIGINT`, the first signal a worker
process serves; `run_worker()` installed its handler and is still running, so the handler, not the
default, receives it. The worker returns 0 and the master receives `GOODBYE`.

| Test | What it proves |
| --- | --- |
| `AuthenticationRejectExitsThree`, `VersionRejectExitsFour` | The fake master answers `HELLO` with `REJECT(authentication)` or `REJECT(version)`. The worker returns 3 or 4 and does not reconnect. |
| `MasterNotReachedExitsTwo` | Exit code 2. Nobody listens on `127.0.0.1:1` and the worker's connect timeout is 1 s. |
| `MasterLostWithoutReconnectExitsFive` | Exit code 5. The fake master sends `WELCOME` and closes the connection; the worker runs with reconnecting off. |
| `RunWorkerOverTCPReturnsTheCodeOfItsEnd` | The one test of `run_worker(options, kernels)` with a connection. A fake master listens with `TCPTransport::make_master()` on a port the OS chooses, welcomes the worker and closes. The worker, on a second thread with reconnecting off, returns 5. |

## test_worker_options_parser.cpp

The parser tests pass an `EnvironmentReader` over a table, so no test changes the environment of
the test process.

| Test | What it proves |
| --- | --- |
| `MasterAloneGivesTheDefaults`, `EveryFlagIsRead` | `--master` alone leaves every other setting at the default of the flag table. The second test gives every flag, half as `--flag value` and half as `--flag=value`, and reads each field back. |
| `EnvironmentFillsWhatNoFlagGives`, `FlagBeatsEnvironmentBeatsDefault`, `EmptyVariableCountsAsUnset` | With no flag, each of the eight variables sets its field. With `--max-jobs 3` and `SR_WORKER_MAX_JOBS` set to text that is not a number, the flag wins and the variable is never parsed. A variable set to nothing leaves the default. |
| `HelpListsEveryFlagAndVariable`, `VersionNeedsNoMaster` | `--help` and `--version` are answered without `--master`. The usage text names all thirteen flags and all nine variables. |
| `LabelsComeFromTheVariableAndRepeatedFlags`, `TheVariableHoldsPairsSeparatedByCommas`, `NoLabelGivesAnEmptyList` | `SR_WORKER_LABELS` and two `--label` flags give three labels, the variable's first. The variable splits at each comma and a label's value may hold '='. An empty variable counts as unset. |
| `ALabelThatBreaksARuleIsRefused`, `ALabelAbove255BytesIsRefused` | An empty key, a label with no '=' and a repeated key are each refused, from a flag and from the variable, and so is a repeat across the two. A key or a value over 255 bytes is refused. Each message is one line that starts with `--label` or `SR_WORKER_LABELS`. |

### `EachRefusalNamesWhatWasWrong`, `NameAbove255BytesIsRefused`

Nineteen command lines the parser refuses: no master, a flag without its value, an endpoint
without a port, port 0, an unbracketed IPv6 address in `SR_MASTER`, an unknown flag, a stray word,
a value given to `--no-reconnect`, and a number that is zero where one is needed, too large,
negative, fractional, empty or not a number. Each message is one line, and a refused value's line
starts with the flag or the variable it came from.

## test_buffer_view.cpp

The file tests BufferView: the bytes it names, read as an element type.

| Test | What it proves |
| --- | --- |
| `AsTypesTheAddress` | As() returns the address typed, and a default view names nothing. |

## test_cluster.cpp

The file tests Cluster without a worker: attach, shared buffers, versions and the option checks.

| Test | What it proves |
| --- | --- |
| `AMasterWithNoWorkerReportsNone` | A master with no worker reports none and offloads nothing. |
| `ASharedAddressGetsAHandle` | A shared address gets a valid handle whose version bump_buffer() raises. |
| `ALivenessTimeoutBelowTheMinimumIsRefused` | A liveness timeout under one millisecond is refused and changes nothing. |

## test_registered_task/test_registered_task.cpp

The file tests record_registered_task(): the recorded node runs the registered kernel on this
machine.

| Test | What it proves |
| --- | --- |
| `TheKernelRunsLocally` | A run with no cluster calls the registered kernel with the task's views. |
