# api tests: runtime

The tests of `Runtime` and the logging controls of `api/runtime/`.
They are part of `sr_api_tests`; [the page of the program](../README.md) says how to run it.

## test_logging_names.cpp

The file includes `api/runtime/logging.hpp` and no other header of the library.

### `TheControlsAreCallableFromTheSurfaceHeader`

Two `static_assert` lines state that the header declares `set_log_synchronous(bool)` and
`set_log_sink(LogSink)` in `SushiRuntime`. The test calls `set_log_synchronous`, `set_log_sink`,
`set_log_level` and `log_level` and puts back what it changed. It logs nothing: the header has no
call that writes a record, and `modules/foundation/logging/tests/test_logger.cpp` covers what
synchronous delivery does.

## test_runtime_entry.cpp

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
runtime; `tests/integration/test_co_tenancy.cpp` reads `first_core` back on a machine with cores to
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
