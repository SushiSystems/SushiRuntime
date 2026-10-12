# distributed tests: worker

The worker's side: its bricks, its options and its process.
The tests are part of `sr_distributed_tests`; [the page of the program](../README.md) says
how to run it.

## test_worker_bricks.cpp

The tests in this file open no socket. A fake transport records every frame a brick sends and every
peer it closes, and either pulls an output stream dry on the calling thread or keeps it until the
test releases it. A fake allocator gives the cache host memory, counts it and can be told to refuse.
The handshake tests use a fixed nonce and a fixed secret. Every wait is on a condition, bounded at
30 s so that a broken brick fails a test. The executor tests build a default `sycl::queue`; their
kernels write through the views and return a finished event. The file includes the private headers
of the four bricks by quoted name.

### `ReleaseEvicts`

`BUFFER_RELEASE` frees an unpinned buffer at once and counts an eviction. A pinned buffer keeps its
memory until its last unpin.

### `BudgetEvictsLeastRecentlyUsedUnpinned`

Three buffers of 100 bytes fill a 300-byte budget and the first is read again. A fourth buffer makes
the cache free exactly the second, which is the one used longest ago.

### `PinnedEntryIsNotEvicted`

With the oldest buffer pinned, the budget evicts the newer unpinned one. With every buffer pinned,
`pin_output` answers `AllocationFailed` and frees nothing.

### `GapInAPulledBufferDiscardsIt`

A chunk at offset 8 after a chunk that ended at 4 ends the transfer: `wait_input` answers `Aborted`
and the memory is returned. The same buffer is then pulled again in three chunks and arrives whole.

### `DuplicateDispatchRunsOnce`

The same dispatch delivered twice through `DispatchDeduplicator` reaches the executor once: one
kernel call, one `TASK_ACCEPTED`, one `TASK_RESULT`. A deduplicator of capacity 2 forgets its oldest
id when a third arrives.

### `OutputAllocationFailureFailsTheTask`

With the allocator refusing, a task with one output ends in a `TASK_RESULT` with
`OutputAllocationFailed`, its kernel never runs and no chunk is sent. The next task, with memory
available, succeeds on the same exec thread.

### `KernelSeesBufferSizes`

The kernel reads `size_bytes` of its input and its output views: 12 and 20. The input comes from a
`BUFFER_REQUEST` the test answers with two chunks, and the output leaves in chunks of 8 bytes.

### `GenericLambdaRegistersAsAKernel`

`register_kernel` has one overload, so a lambda whose four parameters are all `auto` registers
without a cast. It fills its output with 0x5a, and the four bytes come back in one chunk.

### `KernelThatThrowsFailsTheTaskAndTheWorkerLives`

A kernel that throws produces `KernelFailed` with no outputs and no chunk. The next dispatch runs.

### `OutputStreamEndsWithTheResult`

The transport keeps the stream. Until it is pulled no `TASK_RESULT` exists, the task is still held
for `HEARTBEAT`, and a `BUFFER_RELEASE` cannot free the output. Pulled, the stream gives chunks of
4, 4 and 2 bytes, the last one flagged, then `TASK_RESULT`; its destructor returns the pin.

### `DisconnectEndsAnOpenInputPull`

The executor runs with no pull idle timeout, so a pull ends only when something ends it. A task
asks for its input and `discard_queued()` is called before any chunk arrives. The task ends as
`InputUnavailable`, its kernel never runs and the cache holds no byte of the abandoned pull. A
task of the next connection then runs on the same exec thread.

### `HandshakeChecksTheMasterBeforeProving`

`HELLO` carries the nonce, the name and the build id. No `PROOF` leaves before a `CHALLENGE` whose
proof verifies; the `PROOF` then carries the worker's proof, its operations and its capacity, and
`WELCOME` registers the link.

### `WrongMasterProofEndsTheLink`

A `CHALLENGE` proved with another secret ends the link with `RejectedAuthentication`, sends no
`PROOF` and closes the peer. The next connection is closed without a `HELLO`.

### `EndedLinkStopsItsTransport`

A link that ends as `RejectedAuthentication` stops its transport from its heartbeat thread, once,
so the connector opens no further connection. A link that runs stops nothing.

### `PingGetsNoPongBeforeRegistration`

Between a good `CHALLENGE` and its `WELCOME` the link answers no `PING`. After `WELCOME` the same
`PING` gets one `PONG` with its token.

### `LateChallengeForAForgottenHelloIsIgnored`

The link remembers the nonces of its last four `HELLO`s. After a fifth `HELLO`, a `CHALLENGE`
proved against the first nonce reports `LinkChange::None`: no `PROOF` leaves and the peer stays
open, because the proof may answer a `HELLO` the link has forgotten.

### `IdleHeartbeatCarriesTheNextTaskID`

After task 41 finished and nothing is held, the heartbeat thread sends `oldest_held_task_id` 42.
Before the first task the executor reports 0.

### `SilentMasterIsLost`

`WELCOME` carries a liveness timeout of 40 ms and no frame follows. The link closes the peer, and
the loss that follows reports `Disconnected` and leaves the link running.

### `NewSessionClearsTheCache`

A reconnect to the same `session_id` reports `Registered` and the cache and the seen ids stay. A
reconnect to another session reports `RegisteredNewSession`, on which the test clears both, as the
agent does.

### `LossAfterGoodbyeIsTheWorkersOwn`

A registered link says goodbye. The loss that follows and an abandoned connect both report
`LinkChange::None`, the end stays `Running`, and a connection that opens afterwards is closed
without a `HELLO`. `stop()` then ends the link as `Stopped`.

### `SealedRegistryStopsARegistration`, `EmptyKernelStopsTheRegistration`

Finding QC-37. `Unit_KernelRegistryDeathTest` needs no queue: its kernel returns a default event
and is never called. After `seal()`, called twice, `register_kernel` must stop the process for a
new operation and for one already registered, and the registry must hold what it held. A
default-constructed `Kernel` must stop it on an open registry and on a sealed one, with the
message that names the empty kernel.

## test_worker_main.cpp

The parser tests pass an `EnvironmentReader` over a table, so no test changes the environment of
the test process. The five exit-code tests of `Integration_WorkerMainTest` call `serve_worker()` of
`source/worker_session.hpp` over an in-process fabric, through `ServedWorker`, which serves on a
thread of its own with an empty secret and reconnecting off. `FakeMaster` is a `TransportEvents`
on the master endpoint that plays the frames of one script: reject for authentication, reject for
version, register, or register and close. A sixth test runs `run_worker()` itself over TCP
loopback. Each of the six builds a runtime, and each wait for an exit code is bounded at 60 s. No
test raises a signal.

### `MasterAloneGivesTheDefaults`, `EveryFlagIsRead`

`--master` alone leaves every other setting at the default of the flag table. The second test gives
every flag, half as `--flag value` and half as `--flag=value`, and reads each field back.

### `EnvironmentFillsWhatNoFlagGives`, `FlagBeatsEnvironmentBeatsDefault`, `EmptyVariableCountsAsUnset`

With no flag, each of the eight variables sets its field. With `--max-jobs 3` and
`SR_WORKER_MAX_JOBS` set to text that is not a number, the flag wins and the variable is never
parsed. A variable set to nothing leaves the default.

### `HelpListsEveryFlagAndVariable`, `VersionNeedsNoMaster`

`--help` and `--version` are answered without `--master`. The usage text names all twelve flags and
all eight variables.

### `EachRefusalNamesWhatWasWrong`, `NameAbove255BytesIsRefused`

Nineteen command lines the parser refuses: no master, a flag without its value, an endpoint
without a port, port 0, an unbracketed IPv6 address in `SR_MASTER`, an unknown flag, a stray word,
a value given to `--no-reconnect`, and a number that is zero where one is needed, too large,
negative, fractional, empty or not a number. Each message is one line, and a refused value's line
starts with the flag or the variable it came from.

### `ReadsASetVariableAndReportsAnUnsetOne`

`read_environment("PATH")` returns text; a name nobody sets returns nothing.

### `RequestSetsTheFlagAndANewObjectClearsIt`

A `StopSignal` starts clear, `request()` sets the flag, and the next `StopSignal` starts clear
again.

### `UnreadableSecretFileExitsOne`, `ShortSecretFileExitsOne`, `UnknownLogLevelExitsOne`, `BadFlagExitsOne`

Exit code 1. Each test captures the error stream and finds the file name, `31 bytes` and
`at least 32`, the five level names, or the parser's line. All four return before a runtime is
built.

### `VersionPrintsTheProtocolAndExitsZero`

`--version` prints `protocol v3` and the build, and returns 0.

### `SignalStopsWithExitZero`

Exit code 0. The fake master welcomes the worker and waits for its first `HEARTBEAT`, which a
worker sends only once it is registered. The test then calls `StopSignal::request()`, which sets
the flag the first signal of a worker process sets. `serve_worker()` returns `StoppedBySignal`
and the master receives `GOODBYE`.

### `AuthenticationRejectExitsThree`, `VersionRejectExitsFour`

The fake master answers `HELLO` with `REJECT(authentication)` or `REJECT(version)`. The worker
returns 3 or 4 and does not reconnect.

### `MasterNotReachedExitsTwo`

Exit code 2. The fabric has no master endpoint and the worker's connect timeout is 50 ms.

### `MasterLostWithoutReconnectExitsFive`

Exit code 5. The fake master sends `WELCOME` and closes the connection; the worker runs with
reconnecting off.

### `RunWorkerOverTCPReturnsTheCodeOfItsEnd`

The one test of `run_worker(options, kernels)` with a connection. A fake master listens with
`TCPTransport::make_master()` on a port the OS chooses, welcomes the worker and closes. The worker,
on a second thread with reconnecting off, returns 5.
