# Dispatch idle on the scheduler path: plan

**Status:** Open — release at submit implemented and syntax-checked on 2026-11-25, not yet built, run or measured.
**Designs:** LAYERED_EXECUTION

SushiAI RW-102 measured YOLOv4 (608×608, batch 2, RTX 3080 Ti at 75 % power, release build) on the
scheduler path. A step takes 486 ms median and `device.run` 448 ms, over 5302 tasks. The profiler
puts `device_idle` at 127 ms per step. This plan names what keeps the device idle between kernels,
from the sprec record and the Nsight trace, and removes the largest part.

## What the record and the trace show

Medians over the 20 active periods of the sprec record, per step:

- The device window, from the first span's start to the last span's end, is 448.4 ms. The device
  is busy for 360.8 ms of it and idle for 90.6 ms. The other 36 ms of the profiler's 127 ms fall
  outside the window: `data.read` 19.6 ms, `host.assign` 9.3 ms, `host.readback` 2.2 ms.
- 3584 gaps open when the device drains and the next kernel's `dispatch.task` has not begun yet.
  They hold 80.7 ms of the 90.6 ms. In 7.5 gaps per step the task began before the drain.
- In those gaps, drain to task begin is 5.6 µs median (p90 9.3 µs, p99 29 µs), 34.9 ms per step;
  task begin to submit is 0.15 µs median, 0.72 ms per step; submit to kernel start is 7 µs median,
  46 ms per step.
- The next kernel ran on the thread that reaped its predecessor in 3578.5 of these gaps.
- Gaps where the next kernel was queued before the drain: 552 per step, 11.4 ms.

The Nsight trace gives counts over 23 steps: 193 streams created, `cuEventSynchronize` 186 978
calls, `cuEventQuery` 3.59 M, `cuEventCreate`/`Record`/`Destroy` about 123 810 each against
123 809 device operations, and no `cuStreamWaitEvent`. Its one unperturbed segment (5307
operations, 412 ms window) has 97.3 ms idle, of which 3326 host-paced gaps take 90.4 ms and 653
device-side gaps take 6.9 ms. Every consecutive pair of operations ran on different streams.

## The cause

A successor is released only after its predecessor's event completes. `EventPolling::poll_events`
(`modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp:326`) sees the
completion through `cuEventQuery`, `trigger_successors` (`:422`) pushes the successor, and the
worker pops and submits it in its next round (`modules/engine/scheduler/source/worker_rounds.cpp:296-340`,
`:494`). No dependency reaches the device: `sycl_deps` holds only manual external events. Each
kernel edge therefore costs one host reaction and one launch while the device waits.

| Candidate | Measured | Code | Share of in-window idle |
| --- | --- | --- | --- |
| Host waits on a predecessor's event before submitting | 3584 gaps, 80.7 ms per step | `event_polling.hpp:326`, `:348`, `:422`; `worker_rounds.cpp:296-340` | 89 % |
| Per-task event creation, synchronisation and polling | 1.0 event per operation, 1.53 `cuEventSynchronize` and 29.5 `cuEventQuery` per task; 169 ns per query | `event_polling.hpp:326`, `:330`, `:383` | inside the 5.6 µs pick; not separable |
| Stream count and stream choice | 128 compute and 64 transfer streams in rotation, every operation switches stream; device-side gaps 11.4 ms (sprec) or 6.9 ms (Nsight) | DPC++ out-of-order queue; `dispatch_queue_selection.cpp:82` | 8–13 % |
| Worker idle loop | A worker with events in flight yields and never sleeps; picks over 100 µs sum to 10.5 ms | `modules/engine/scheduler/source/worker_threads.cpp:101-103` | at most 12 %, part of the first row |
| Dependency resolution per task | 0.72 ms per step from task begin to submit; `dispatch.task` self time 6.94 ms | `worker_rounds.cpp:376-503` | under 1 % |
| Submission batching | `dispatch.submit` 20.2 µs median against 10 µs `cuLaunchKernel`; 406 submits over 100 µs sum to 183 ms per step; the funnel is off | `dispatch_record.cpp:147-151` | on the critical path only through the first row |

## Decisions

| Id | Decision | Reason |
| --- | --- | --- |
| D1 | When a worker submits a kernel node, each successor whose only outstanding predecessor is that node is claimed by a compare-and-swap of `in_degree` from 1 to 0, takes the node's event, and is pushed to the worker's own deque. Its submit adds `depends_on` on that event. | The device then orders the edge and the launch overlaps the predecessor. The chain layer already submits kernel command groups ahead of completion on the same plans (docs/design/GRAPH_REPLAY/README.md). The reap leaves `on_complete` unchanged: the claimed successor's `in_degree` goes to -1 there and is not reported again. |
| D2 | `submit_task()` takes a trailing `const sycl::event* after = nullptr`. | A new public signature. The defaulted parameter keeps every other caller, among them `chain_run.cpp` and `chain_replay.cpp`, unchanged. A pointer and not an event vector keeps the hot path free of a heap vector, as `kernel_group` already avoids one. |
| D3 | Only kernel nodes on both sides, placed on one device, whole (no `placed_split`), not the sentinel and not offloadable are released. | A host body runs on the worker and could read data its predecessor has not written. A split node submits several command groups, so one event does not cover it. An offloadable node may move to another device, whose queue may not share the event's context. |
| D4 | A worker releases nothing while it holds `K_SUBMIT_RELEASE_IN_FLIGHT` = 16 events. | Each poll round queries every event the worker holds, at 169 ns each. Sixteen bound the round at about 3 µs, below the 5.6 µs reaction this change removes, and leave one drain per 16 chained edges. The value is a first choice to measure, not a measured optimum. |
| D5 | A failed kernel's released successor may already be on the device. The run still records the failure and aborts. | RW-27 asks for a failed event to be recorded before its successors run. Release at submit gives that up for the released edges only; the chain path already behaves this way. |

## Expected numbers

From a replay of the record (`release at submit` with one submitter per thread, device-side
dependency cost 2 µs to 5.9 µs):

| Number | Today | Expected |
| --- | --- | --- |
| In-window idle per step | 90.6 ms | 15–35 ms |
| `device.run` | 448 ms | 375–400 ms |
| Step | 486 ms | 415–440 ms |
| Profiler `device_idle` | 127 ms | 55–75 ms |
| `cuStreamWaitEvent` per step | 0 | about 3500 |

Joins, the in-flight bound and host nodes keep the residual. With a single submitter the replay
gives no gain (464–474 ms), so a regression to one hot worker would show here first.

## Jobs

1. Done: `Node::release_at_submit()`, `released_after()` and the drop in `on_complete()`
   (`modules/engine/task/include/SushiRuntime/task/node.hpp`).
2. Done: the `after` parameter of `submit_task()` and its use in `submit_dispatch()`.
3. Done: `may_release_at_submit()` and `release_successors_at_submit()`
   (`modules/engine/scheduler/include/SushiRuntime/scheduler/submit_release.hpp`), called from
   `WorkerRounds::release_at_submit()` after a successful submit.
4. Done: `Small_Node` and `Small_SubmitRelease` tests, and two `Medium_TaskSubmission` tests.
5. Open: build, run the scheduler and task tests, and measure RW-102 again against the table above.
6. Open: update the scheduler README's release model and the vocabulary README's statement that a
   count is read after every predecessor's event completed.
7. Open: the stream choice (an in-order lane per device, docs/design/GRAPH_REPLAY/SUBMIT_FUNNEL.md) for the 7–11 ms of
   device-side gaps.
