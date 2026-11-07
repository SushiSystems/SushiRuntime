# dispatch_executor tests

The tests of the `dispatch_executor` module, mirroring `modules/extension/dispatch_executor/`.
`tests/extension/dispatch_executor/CMakeLists.txt` builds them into `sr_dispatch_executor_test_objects`, linked into
`sr_extension_tests`. `sr test` runs them with the rest; `sr run sr_extension_tests --
--gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.
The executor runs small on a host runtime in `test_dispatch_executor.cpp`, the cache in
`test_worker_buffer_cache.cpp`. The threads, the pull over a connection and the real device
contexts are the boundary checks of `Medium_DispatchExecutorTest` in
`tests/extension/distributed/test_worker_agent/test_worker_agent.cpp`.

## compile_*.cpp

Each `compile_<header>.cpp` compiles one header of the module on its own, so the header carries every
include it needs: `dispatch_executor`, `dispatch_executor_log_category`, `dispatch_job`,
`dispatch_recorder`, `dispatch_settler`, `held_tasks` and `worker_buffer_cache`. The runtime promises of
these bricks are held by `test_dispatch_executor.cpp` and `test_worker_buffer_cache.cpp`.

## test_worker_buffer_cache.cpp

`Small_WorkerBufferCache` builds the cache on `TestSupport::HeapCacheAllocator`: release of an
unpinned and of a pinned buffer, the least-recently-used eviction, pinned buffers kept, an output past
the budget, an allocator with no room, an output of another size, a gap in a pull, a whole pull, an
abort, `clear()` and a second reader of a pulled buffer.

## test_dispatch_executor.cpp

`Small_DispatchExecutor` runs the executor on `Runtime::create_host()` with host kernels and reads the
replies at the master end of a `TestSupport::ExecutorFabric`: an output the cache cannot hold, buffer
sizes and chunks, the stream ending with its result, a released output streaming its own copy, a
missing device, a generic lambda kernel, a kernel that throws, two writers of one buffer in order and a
reader of a queued output in the next batch.

## test_dispatch_plan.cpp

The file tests the pure decisions of the executor. It needs no thread, no device and no transport.

| Test | What it proves |
| --- | --- |
| `Small_DispatchPlan.ADispatchIsQueuedWhileTheQueueHasRoom` | A queue below its bound queues the dispatch. |
| `Small_DispatchPlan.AFullQueueRefusesTheDispatch` | A queue at its bound answers `QueueFull`. |
| `Small_DispatchPlan.AStoppingExecutorAnswersStopping` | A stopping executor answers `Stopping`, full or not. |
| `Small_DispatchPlan.AJobWithAKernelAndAUsableDeviceRuns` | A known kernel on a device of the cache's context runs. |
| `Small_DispatchPlan.AnUnknownOperationIsNamedBeforeTheDevice` | The kernel is judged before the device. |
| `Small_DispatchPlan.ADeviceTheWorkerDoesNotHaveIsNamed` | A device index at or above the count is `DeviceMissing`. |
| `Small_DispatchPlan.ADeviceOutsideTheCacheContextIsNamed` | A device outside the cache's context is named. |
| `Small_DispatchPlan.AJobReadingABufferTheBatchWritesOpensTheNextBatch`, `AJobReadingNothingTheBatchWritesJoinsIt` | The batch order rule. |
| `Small_DispatchPlan.TheChunkSizeIsTheDefaultOrWhatOneFrameCarries` | 0 takes 256 KiB and a large size is cut to one frame. |
