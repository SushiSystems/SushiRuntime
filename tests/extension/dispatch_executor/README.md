# dispatch_executor tests

The tests of the `dispatch_executor` module, mirroring `modules/extension/dispatch_executor/`.
`tests/extension/dispatch_executor/CMakeLists.txt` builds them into `sr_dispatch_executor_test_objects`, linked into
`sr_extension_tests`. `sr test` runs them with the rest; `sr run sr_extension_tests --
--gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.
The executor as a whole, with its cache, recorder and settler, is tested through a `WorkerAgent` in
`tests/extension/distributed/test_worker_agent.cpp`.

## compile_*.cpp

Each `compile_<header>.cpp` compiles one header of the module on its own, so the header carries every
include it needs: `dispatch_executor`, `dispatch_executor_log_category`, `dispatch_job`,
`dispatch_recorder`, `dispatch_settler`, `held_tasks` and `worker_buffer_cache`. The runtime promises of
these bricks are held through a `WorkerAgent`, by `Medium_DispatchExecutorTest` and
`Medium_WorkerBufferCacheTest` in `tests/extension/distributed/test_worker_agent.cpp`: a job needs a
runtime's queues, which the module's own tests do not build.

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
