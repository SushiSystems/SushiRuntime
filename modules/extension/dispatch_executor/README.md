# dispatch_executor

The module holds the worker's job queue and the thread that runs the kernels a master dispatches,
with the bricks that thread is built from. It is an internal module: its headers reach the modules
that name it and the tests, and are never installed. It is built with the distributed layer, under
`SR_ENABLE_DISTRIBUTED`, and every name it declares is in `SushiRuntime::Distributed`.

It depends on `core`, `log`, `api`, `device`, `region`, `transport`, `wire` and `worker_types`, and
its source on `frame_transport`.

## Public entry points

| Header | Holds |
| --- | --- |
| `dispatch_plan.hpp` | `admit_dispatch()`, `judge_job()`, `reads_a_written_buffer()`, `usable_chunk_bytes()`, `EnqueueStatus`, `JobVerdict` |
| `dispatch_executor.hpp` | `DispatchExecutor`, `DispatchExecutorOptions` |
| `dispatch_job.hpp` | `DispatchJob`, `DispatchEntry`: one queued dispatch, and one job of a batch with the run of its kernel |
| `dispatch_recorder.hpp` | `DispatchRecorder`: the graph each batch runs on, the borrowed buffers, the run |
| `dispatch_settler.hpp` | `DispatchSettler`: the commit and output stream of a job that ran, every failed `TASK_RESULT`, the task counters |
| `held_tasks.hpp` | `HeldTasks`: the task ids held until their result leaves |
| `worker_buffer_cache.hpp` | `WorkerBufferCache`: cached buffers, chunk assembly, the byte budget, pins |
| `dispatch_executor_log_category.hpp` | `K_DISPATCH_EXECUTOR_LOG_CATEGORY` |

## dispatch_plan.hpp

The decisions of the executor are functions of plain values. `admit_dispatch()` answers `Queued`,
`QueueFull` or `Stopping` from the queue length, its bound and whether `stop()` began.
`judge_job()` names, in this order, an unknown operation, a device the worker does not have, and a
device outside the context of the cache, or lets the job `Run`. `reads_a_written_buffer()` tells
whether a job reads a buffer the running batch writes, which then opens the next batch.
`usable_chunk_bytes()` gives the chunk size one frame can carry, 256 KiB for 0. The executor asks
them and acts on the answers.

## dispatch_executor.hpp

The exec thread takes the queued jobs into one batch, pins each job's outputs, gets each input from
the cache or pulls it from the master, checks the job's device, records one task per job and runs
the batch as one run of the executor's `Graph`; `DispatchSettler` reports each job. `step()` does
the same for one batch on the calling thread, so a caller that owns the cache and the transport
drives the executor without the thread. The executor reads no time itself: a pull's bound is the
cache's wait for a chunk, set by `pull_idle_timeout`.
