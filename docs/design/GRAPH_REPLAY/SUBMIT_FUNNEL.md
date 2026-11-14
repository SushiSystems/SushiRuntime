# Submit funnel

How the scheduler path stops paying lock contention when many workers submit device work at once.
Phase P6 of [the topic](README.md) builds it. Layers 1 and 2 already submit from one thread; this
page is about the plans that stay on the scheduler.

## The problem as measured

An editor trace of the scheduler path holds 686k `dispatch.submit` zones spread over 12 workers
(`modules/engine/scheduler/source/worker_rounds.cpp`, the `Internal::Graph::submit_task` call), at
a mean of 65 us of host time each and about 51 per run, while each kernel runs for 10 to 60 us.
The caller then waits about 4.8 ms per run on `run.wait.latch`.

The owner asked whether the runtime can be made lock-free here. The runtime's own structures
already are: the work-stealing deques and the atomic in-degrees take no lock. The locks are inside
DPC++ and the CUDA driver, below `queue::submit`, and the runtime cannot remove them. It can stop
sending many threads into them at once.

## Where the locks are

Read from the DPC++ sources the toolchain was built from (`__SYCL_COMPILER_VERSION` 20251229) and
its CUDA adapter.

| Lock | Scope | Held across | Taken by |
| --- | --- | --- | --- |
| `queue_impl::MMutex`, a `std::mutex` | One per `sycl::queue` | The whole `handler::finalize()`, including the adapter call and `cuLaunchKernel` (`finalizeHandlerOutOfOrder` and the in-order variants in `queue_impl.hpp`) | Every submission, out-of-order or in-order, with or without an event |
| The cached kernel's mutex (`FastKernelCacheVal::MMutex`) | One per kernel name, device and context, so shared by every queue of the context | Setting the kernel's arguments and the launch (`enqueueImpKernel` in `commands.cpp`) | Every launch of that kernel |
| `KernelProgramCache::MFastKernelCacheMutex`, a spin lock | One per context | The kernel lookup only | Every launch |
| UR CUDA `ComputeStreamSyncMutex` | One per queue | The launch, when the queue reuses a dependency's stream | A launch whose dependency ran on the same queue |
| `Scheduler::MGraphLock`, a `shared_timed_mutex` | Process-wide | Command graph construction and enqueue | Only off the fast path: host tasks, accessors, streams, dependencies on another context or on a command not yet enqueued. Not on the measured path |
| The CUDA driver's own serialisation | One per CUDA context, so per device | `cuLaunchKernel`, `cuEventRecord`, `cuStreamWaitEvent` | Every call. Not in any source; the native column below measures it |

A timed run pays more per submission. With profiling on, the adapter creates three `CUevent`s per
kernel and records one of them on a separate host-submit-time stream, and SYCL reads a device
timestamp for the submission time.

So a second queue escapes only the first lock. The kernel mutex and the driver are shared by every
queue on the device, which is why one queue per worker measures worse, not better.

## Measurements

The probe submits a 51-node DAG per run: node `i` sits in lane `i % T` for `T` lanes, depends on
the previous node of its lane, and every fourth level also depends on the previous level of the
next lane. Each node is a `single_task` that mixes its inputs over 2000 steps, about 24 us on the
device. `T` persistent threads submit, one per lane. Every run starts from zeroed memory and its
51 outputs are compared with a host reference; every run of every row below matched. RTX 3080 Ti,
DPC++ with the CUDA adapter, Ryzen 5 7600X (12 logical cores), 200 timed runs after 20 warm-up runs.

`submit` is host time per submission on the submitting thread, in us; `run` is wall time per run,
from release to the last completion, in us. The graph row's `submit` is per replay, one per run.

| Strategy | Threads | Off: submit | Off: run | On: submit | On: run |
| --- | --- | --- | --- | --- | --- |
| Shared out-of-order queue, eager release | 1 / 4 / 12 | 5.0 / 34.6 / 114.5 | 1085 / 875 / 929 | 20.0 / 62.6 / 226.4 | 1273 / 1598 / 1744 |
| Shared queue, release on completion (today's scheduler) | 1 / 4 / 12 | 5.2 / 65.9 / 208.3 | 1419 / 1287 / 1208 | 13.6 / 115.7 / 347.6 | 1733 / 1931 / 1839 |
| (a) One out-of-order queue per worker | 1 / 4 / 12 | 5.1 / 51.2 / 199.3 | 1088 / 1079 / 1186 | 19.1 / 74.0 / 324.5 | 1294 / 1710 / 1920 |
| (a) Same, a distinct kernel per worker | 1 / 4 / 12 | 5.2 / 39.5 / 205.1 | 1089 / 862 / 1163 | 19.2 / 84.5 / 498.7 | 1279 / 1763 / 2585 |
| (b) One in-order queue per worker, events only where lanes cross | 1 / 4 / 12 | 4.1 / 35.9 / 121.6 | 1088 / 867 / 925 | 18.9 / 66.5 / 316.7 | 1288 / 1630 / 1683 |
| (c) Ring, one submitter, one in-order queue | 1 / 4 / 12 | 4.8 / 4.7 / 6.8 | 1126 / 1069 / 1059 | 19.6 / 20.6 / 28.7 | 1237 / 1250 / 1526 |
| (c) Ring, one submitter, one in-order queue per lane | 1 / 4 / 12 | 5.0 / 7.6 / 9.1 | 1077 / 477 / 542 | 17.8 / 22.3 / 25.6 | 1257 / 1169 / 1341 |
| (c) Ring, one submitter, the shared out-of-order queue | 1 / 4 / 12 | 5.1 / 7.1 / 10.6 | 1092 / 459 / 607 | 21.8 / 23.0 / 26.1 | 1374 / 1207 / 1378 |
| (c) The row above, release on completion | 1 / 4 / 12 | 5.6 / 8.0 / 12.4 | 1452 / 612 / 697 | 14.6 / 22.1 / 26.0 | 1788 / 1300 / 1360 |
| (c) Ring, per-lane queues, release on completion | 1 / 4 / 12 | 6.8 / 8.4 / 9.8 | 1471 / 552 / 538 | 15.2 / 23.6 / 25.4 | 1865 / 1330 / 1331 |
| Flat combining, per-lane queues, release on completion | 1 / 4 / 12 | 6.4 / 10.9 / 10.8 | 1503 / 721 / 701 | 15.5 / 24.7 / 25.6 | 1846 / 1335 / 1384 |
| (d) Recorded `command_graph`, one replay per run | 1 / 4 / 12 lanes | 17.1 / 15.9 / 40.5 | 1006 / 267 / 195 | 26.8 / 28.1 / 58.7 | 999 / 306 / 228 |
| (e) Native CUDA, one stream per worker | 1 / 4 / 12 | 3.0 / 9.0 / 30.7 | 1105 / 406 / 368 | 8.5 / 44.6 / 108.1 | 1273 / 689 / 753 |

In the ring rows the workers' own cost is the push into the ring: 0.02 to 0.33 us. A second sweep
of today's shape and the funnel's row repeated the first within 6% with profiling off
(1276 and 676 us at 12 workers); with profiling on the funnel's run time spread from 1360 to
1659 us against 1924 us for today's shape. Sweeps taken while another build loaded the machine
differed by up to 2x and are not in the table.

What the table says:

- Submission cost grows with the number of threads that submit, on any queue layout. From one
  thread a submission costs 5 us; from 12 it costs 115 to 205 us. The trace's 65 us mean sits between the
  probe's 4-thread and 12-thread columns.
- More queues do not help. The kernel mutex and the driver are per device, so (a) and (b) are no
  faster than one shared queue, and a timed run on per-worker queues is slower.
- The driver serialises too. Native CUDA from 12 threads costs 31 us per launch against 3.0 us
  from one, and 108 us against 8.5 us with timing events. No layout above SYCL removes that.
- One submitting thread keeps the cost at 5 to 12 us whatever the number of workers. On the
  scheduler's own shape, release on completion and the shared out-of-order queue, a run of 12
  workers drops from 1208 us to 697 us with profiling off and from 1839 us to between 1360 and 1659 us with it on.
- Replay is faster still, at 195 to 267 us, and stays the goal for every plan layer 2 accepts.

## Decision

The scheduler path gains an opt-in **submit funnel**: per device, workers hand ready kernel nodes
to one submitting thread through a lock-free ring, and that thread is the only one that enters the
device's submission locks. Off by default; with it off, nothing in the scheduler changes.

This follows the owner's ruling for the runtime: a feature is added, no existing behaviour is
removed or changed, and the runtime names no client.

## Design

### Shape

- **`SubmitFunnel`**, one per device of the `DeviceQueueSet`, created only when the setting is on.
  It owns a bounded multi-producer, single-consumer ring of dispatch records and one thread that
  drains it.
- A **dispatch record** carries what `WorkerRounds::execute_task` holds at the submit site today:
  the task, the selected queue, the node's `sycl_deps`, the profiling flag, the step, the device
  index and the originating worker's id.
- The funnel thread pops records in order and calls the same `Internal::Graph::submit_task` the
  worker calls today, with the same queue and the same dependencies. It reads `submit_steady_ns`
  just before the call, so the ledger's bound on the kernel's start stays true and gets tighter.
- The resulting dispatched entry goes back to the originating worker through a **landing ring**,
  one single-producer, single-consumer ring per worker. At the top of each poll round the worker
  moves its landed entries into its `ActiveEventList`. Polling, successor release, the in-flight
  count and the quiescence latch are unchanged: a node in the funnel is in flight exactly as a
  node whose event the worker is polling.
- When its ring is empty the funnel thread backs off as an idle worker does: it yields, then
  sleeps. The runtime is C++17 (RW-16 is blocked), so there is no `atomic::wait`; a condition
  variable may park it, taken only on the idle edge, never on the submit path.

### What the funnel takes, and what it leaves

| Node | Path with the funnel on |
| --- | --- |
| `KERNEL` node on a device queue | The funnel |
| `HOST` node that submits device work | Today's path: its body runs on the worker and submits there. It is rarer than a kernel node, and running a host body on the funnel thread would stall every other submission |
| Pure host node | Today's path: inline on the worker |
| Skipped, disabled or aborted node | Today's path: pushed ready, never submitted |
| A run of layer 1 or layer 2 | Not affected: both already submit from one thread |

The funnel changes who calls `submit_task`, not what it submits. Queue selection
(`select_dispatch_queue`), the out-of-order queue, the event per node and the order the DAG
allows all stay as they are, so every output is the same as without the funnel. One in-order queue
per lane measured a little faster (538 against 697 us), but it needs a lane assignment the
scheduler does not have; it is left for later.

### Profiling and the layers

A timed run keeps one event per node: the funnel submits through the same call, and the event
reaches the ledger through the landing ring. The ledger's records are unchanged. A timed run
stays submission-bound at about 26 us per node on CUDA, because the adapter's three events per
kernel cost that much from one thread; the funnel removes the contention on top of it, from 348 us
to 26 us per submission.

Layer 1 needs no funnel: its single submitting thread is already the funnel's shape. Layer 2 needs
none either: one replay per step. The funnel is for plans that stay on the scheduler: plans with
host nodes, plans across devices, and a `DynamicGraph` without `allow_chain(true)`. A device with a
funnel and a chain on the same queue do not meet: a run takes one path.

The `dispatch.submit` zone moves to the funnel thread, so that thread must be one the collector
records. A worker gains a `dispatch.funnel_push` zone.

### Alternatives set aside

- **One queue per worker**, out-of-order or in-order: no faster, because the kernel mutex and the
  driver are shared per device; slower when timed.
- **Flat combining**, where the worker that finds the ring unowned drains it, without a thread of
  its own: equal when release is eager (490 against 491 us), 30% slower when release waits for
  completion (701 against 538 us), because the worker that drains is a worker that is not polling.
- **Native CUDA submission through the interop handle**: 3.0 us per launch from one thread against
  5 us through SYCL, but it is CUDA only, it bypasses the queue's event model the ledger reads, and
  it still serialises in the driver.
- **One in-order queue behind the funnel**: the device then runs one kernel at a time; 1059 us
  against 607 us for the shared out-of-order queue.

### Where it lives

| Path | Owner (`sr design owner`) | Change |
| --- | --- | --- |
| `modules/engine/scheduler` | LAYERED_EXECUTION | New files for the funnel and the landing ring; the submit site in `worker_rounds.cpp` pushes a record when the device has a funnel; the poll round drains the landing ring |
| `modules/engine/work_stealing` | none | The bounded MPSC and SPSC rings, beside the work-stealing deque, if the owner wants them as a shared brick |
| `modules/engine/execution/include/SushiRuntime/execution/runtime_settings.hpp` | GRAPH_REPLAY | A `submit_funnel` flag, off by default |
| `modules/surface/api/include/SushiRuntime/api/runtime/runtime_config.hpp`, `modules/surface/api/source/runtime.cpp` | LAYERED_EXECUTION | The public `RuntimeConfig::submit_funnel` flag, copied into the settings |
| `modules/engine/task` | LAYERED_EXECUTION | None: `submit_task` is called unchanged |

The scheduler and the public config belong to LAYERED_EXECUTION, so P6 keeps that topic's intent
and does not change placement, the worker pool's size or its pinning.

### How P6 is tested

A plan run with the funnel on and off gives bit-identical outputs, on a DAG wide enough that
several workers submit at once, with profiling off and on; a timed run with the funnel on records
one `NodeEventRecord` per node, as without it. The probe's table is re-measured through
`Graph::run` with the funnel on and off, at 1, 4 and 12 workers.

### Open questions for the owner

- Should the funnel thread count against `worker_count`, or come on top of it? On a 12-thread
  machine with 12 workers it oversubscribes by one thread per device.
- Should `RunReport` say that a run was funnelled? It is a public field, so a new one is the
  owner's call; without it, only the setting says so.
- Should the rings live in `modules/engine/work_stealing` as a shared brick, or stay private to the
  scheduler?
- Does this page belong to LAYERED_EXECUTION, which owns the scheduler, rather than to GRAPH_REPLAY?
- AdaptiveCpp and Level Zero are not measured; their locks may sit elsewhere.
