# api tests: runtime

The tests of `Runtime`, its configuration, its errors and its logging controls. They are part of `sr_api_tests`;
[the page of the module's tests](../README.md) says how to run them.

## test_advanced_controls.cpp

The file covers the rebalancer control: the rebalancer is off unless requested, execution is correct
in both states, and the engine reports the flag faithfully.

The default is the assertion that matters. The background thermal rebalancer migrates tasks between
workers on a 5 ms heartbeat. That changes which worker folds which tile and puts a periodic wakeup
under every frame, a throughput trade that gains a simulation frame nothing.

## test_errors.cpp

A task exception must abort only its own run and must not kill the worker pool. The run that threw
surfaces the error to the caller, and a fresh graph on the same `Runtime` must still run and produce
correct results afterwards. The defect this file guards against latched `shutdown_requested` on a
single throw and left the pool dead for good.

### `ThrowingKernelLeavesRuntimeReusable`

The command-group function of the first graph throws, so `run()` must return the error to the
caller and not hang.

## test_logging.cpp

The file includes `api/runtime/logging.hpp` and no other header of the library.

### `TheControlsAreCallableFromTheSurfaceHeader`

Two `static_assert` lines state that the header declares `set_log_synchronous(bool)` and
`set_log_sink(LogSink)` in `SushiRuntime`. The test calls `set_log_synchronous`, `set_log_sink`,
`set_log_level` and `log_level` and puts back what it changed. It logs nothing: the header has no
call that writes a record, and `tests/foundation/logging/test_logger.cpp` covers what
synchronous delivery does.

The file covers what a program sees of the logger when it creates a `Runtime`. It is here and not
in the `logging` module because the lines it looks for are logged by `device` and `topology` while
`Runtime::create()` runs.

### `Medium_LoggingDefaults`

The fixture puts the logger in synchronous mode with the default sink, and afterwards restores the
default sink and the level the process had. Synchronous mode also waits for records that earlier
tests of the program queued, so none of them lands inside a capture.

### `capture_streams_around_create`

The helper creates and destroys a default `Runtime` between GoogleTest's `CaptureStdout` and
`CaptureStderr`, which redirect the two file descriptors. The streams are captured this way and not
through a sink because the claim is about what reaches the terminal, including anything written
past the logger.

### `CreateIsSilentAtTheDefaultLevel`

At `Warn` both streams are empty. The test sets `Warn` itself, since `SR_LOG_LEVEL` in the
environment of the test run would otherwise decide the level. Every start-up line of a healthy
machine is `Info`; a machine on which start-up warns fails this test, and the captured text is in
the failure message.

### `DeviceLineArrivesThroughTheSinkAtInfo`

At `Info` with a sink installed, the sink receives the `Device: ...` line of
`device_queue_set.cpp` at level `Info`, and both streams stay empty because the sink replaced the
default.

## test_runtime.cpp

The file covers `Runtime::create()`, `RuntimeConfig` and the `AdvancedControls` view. Four
`static_assert` lines at the top state that a `Runtime` moves without throwing and does not copy, so
a copy constructor that returns fails the compile and not a test.

### `OneStructConfiguresAndTheViewOutlivesTheRuntime`

One `RuntimeConfig` with `worker_count` 2 must produce a pool of two workers. The view is taken
while the `Runtime` lives and questioned again after the `Runtime` is destroyed: it shares ownership
of the context, so it must give the same device count. A view that held a reference would read a
destroyed context here, which AddressSanitizer reports.

### `DevicesListsEveryDeviceInOrder`

`devices()` must hold one entry per device, and entry `i` must describe the device `DeviceIndex{i}`
selects. The test compares each entry's name with the name the SYCL device behind queue `i` reports,
then allocates a buffer with no `DeviceIndex` and asks the allocator which device owns it: that is
entry 0.

### `DefaultConfigCreates`

`create()` with no argument must yield a runtime with profiling and the rebalancer off that runs a
graph. Every factory is called through a `const Runtime`, so a factory that lost its `const` fails
the compile.

### `EveryFieldReachesTheRuntime`

Each field of `RuntimeConfig` but `first_core` is set away from its default and read back from the
runtime; `Medium_CoTenancy` reads `first_core` back on a machine with cores to
reserve. The api struct is mapped to the engine's settings by hand in one function, so a field that
function forgets would otherwise keep its default silently.

### `AMovedRuntimeKeepsItsContext`

A move hands the context over and does not build a second one.

### `ACallerWaitsAndCopiesThroughTheDeviceQueue`

The test submits a kernel on the queue `advanced().queue()` returns and does not wait for its
event. `Runtime::wait()` returns once the kernel has written the buffer, and a `memcpy` through
the same queue brings the values to a host vector. The test names no type of `Internal::`.

### `AQueueOfAnUnknownDeviceStops`

`advanced().queue()` asked for the device after the last one stops the process.

### `ARequestNoDeviceCanSatisfyReturnsAllocationFailed`

`buffer()` and `state()` are asked for half the address space in bytes, which no device has; the
memory module has no seam that makes an allocation fail. Both return `AllocationFailed`, the
buffer's message holds the element count, and a buffer of 16 bytes is granted afterwards.

### `LiveAllocationCountFollowsABuffer`

`advanced().live_allocation_count()` is read before anything is allocated. A buffer raises it by
one and a state by two more. When both leave their scope the count is back at its first value.

### `AThreadOutlivingTheRuntimeTearsDownCleanly`

A thread that allocated through a `Runtime` and is still alive when that `Runtime` is destroyed
unwinds after it. A long-running simulation process hits this shape when a worker outlives the
context it allocated from.

The risk is thread-local state that outlives what it points into. Epoch reclamation keeps a slot per
thread, and a thread that reaches its exit-time destructors after the `Runtime` is gone must not
touch freed memory.

The thread allocates and frees, so that its exit-time destructors have touched the per-thread
bookkeeping of the runtime. It runs those destructors when the test joins it. Reaching the statement
after the join without a fault is the assertion: under AddressSanitizer it is a use-after-free
check, and without AddressSanitizer the defect still crashes often enough to fail the test. The
`SUCCEED()` after the join records that the test got there on purpose.

## test_runtime_config.cpp

The file covers the co-tenancy contract: the runtime is a tenant of the machine, not its owner.

Inside a game the runtime shares the machine with a main thread, a render thread, an audio callback
and asset I/O. A batch runtime takes every core and pins worker 0 onto the core the host's main
thread runs on. A host has to be able to keep some cores for itself. These tests assert the
mechanism that allows it (a worker count, a core range, and the option not to pin at all) and check
that a narrower pool still executes correctly.

### `expect_runs_correctly`

`expect_runs_correctly` runs a small graph and checks every element, which proves that a
configuration executes and does more than construct.

### `DefaultsToOneWorkerPerCoreFromCoreZero`

The default is the sole-tenant behaviour: one worker per logical core, starting at core 0. A silent
change to the default would move the performance profile of every existing consumer.

### `ReservesTheCoresTheHostAskedFor`

A host that wants cores 0 to `reserved - 1` for its own threads asks for a pool that starts past
them. Both the count and the placement must be exactly what the host requested, because a runtime
that quietly took more cores would defeat the reservation.

### `ASingleWorkerStillExecutesTheWholeGraph`

A pool much smaller than the machine still runs everything correctly. The graph does not depend on
there being a worker per core.

### `FloatPolicyRunsUnpinned`

The `Float` policy leaves placement to the operating system. The pool is still the requested size,
but no worker is bound. That is the right choice inside a container whose CPU allowance is enforced
by quota and not by affinity.

### `AnOutOfRangeCoreRangeIsClampedNotIgnored`

A core range that runs off the end of the machine is clamped, not obeyed. The operating system's
affinity calls accept an out-of-range mask and then do not bind, so without the clamp the runtime
would report a reservation it never made.

The file covers what the hardware filter selects and how it fails.

Both properties move a failure earlier. A double-precision kernel on a device without
`sycl::aspect::fp64` fails inside the driver at launch, with a message that names neither the device
nor the aspect; `require_fp64` moves that failure to discovery. A filter that matches nothing would
otherwise surface as "Queue index out of bounds" from the first allocator call, which is the first
thing a player on a machine with no SYCL device would see.

### `any_device_has_fp64`

`any_device_has_fp64` returns true when at least one SYCL device on the lane reports fp64. The
result decides whether a `require_fp64` runtime is expected to construct or to fail on that lane.

### `RequireFp64AdmitsOnlyFp64Devices`

Every device the filter admits reports fp64. The test asserts this against the discovery layer
directly, so the property is checked on the list and not inferred from a count that a deduplication
step could also explain.

### `RequireFp64StillConstructsOnAConformingLane`

The filter does not reject the whole machine: a lane that has an fp64 device still builds a runtime
with `require_fp64` set. The CPU lanes this suite runs on (POCL and the Intel OpenCL CPU runtime)
all report fp64, so this is the branch the test normally takes.

On a lane with no conforming device the failure must still be the `Unsupported` error
`Runtime::create()` returns, and not a driver error from inside a kernel launch.

### `AnEmptyFilterFailsWithUnsupported`

A filter that admits nothing fails at construction with a code a caller can branch on. A machine
with no SYCL device at all has the same shape, which is why the error carries `ErrorCode::Unsupported`
and not a bare out-of-bounds message from the first queue lookup.

The message must name the filter, so the reader can tell a machine that has nothing from a filter
that excluded everything.

### `DefaultFilterIsUnaffected`

The default filter admits everything, so the empty-filter check leaves the common path alone: a
plain `Runtime::create()` still finds the devices of the lane.

The file covers what a caller reads when the library refuses a request. It is here and not in a
module because the refusal it uses is raised by `RuntimeContext` and returned through `Runtime`.

### `AFilterThatExcludesEveryDeviceReturnsUnsupported`

A filter that admits no device class makes `Runtime::create()` return `ErrorCode::Unsupported` with
a message, and no exception leaves the call. Topology returns an empty list for that filter and
`RuntimeContext` turns the empty list into the failure; `Medium_DeviceSelection` checks that
the message names the filter.
