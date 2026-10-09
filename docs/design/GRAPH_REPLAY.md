# SushiRuntime: replaying a graph's device work as one submission

**Status:** Open — P0, P1 and P3 are built and not measured; P2 is not started.

Designed 2026-09-19; every decision in §F taken by the owner the same day. Consumer: SushiEngine's matter device solver, whose device arm is launch-bound (§0).
Evidence: the feasibility spike under sushiengine-30's scratchpad (`graph_spike/`, run 2026-09-19)
and the engine's measurement report in SushiEngine's `docs/design/MATTER_DEVICE_SOLVER.md`, R28 and R29.

## 0. The problem, measured

On an RTX 3080 Ti a settled tick of the matter solver's 129-particle scene issues 1,484 node
dispatches through `API::Graph::run` and costs 42 ms. Its kernels do almost no work: summed
kernel execution is 24 ms of the 42, and the mean dispatch costs 28 us of wall time. The host
solver runs the same tick in under 5 ms. Grid sort alone is 53 of the 67 dispatches a substep
issues, because each prefix scan, counting sort and reduce lowers to several nodes.

A bare SYCL probe on the same machine, same kernels in a dependency chain of K nodes:

| Device | Wait after every kernel | Chain on one in-order queue, one wait | Recorded graph, replayed |
|---|---|---|---|
| RTX 3080 Ti (CUDA) | ~12 us/node | ~6.5 us/node | ~1.1 us/node |
| Ryzen 5 7600X (OpenCL CPU) | ~11.4 us/node | ~6.0 us/node | ~4.7 us/node |

Per node, `Graph::run` today costs more than twice the probe's worst column. Replaying a recorded
graph would take the 1,484-dispatch tick from about 18 ms of launch overhead to about 1.6 ms.

## A. What `Graph::run` does today (facts)

- `run(steps, reuse)` goes `try_run` → `drive` → `drive_impl` (`api/graph/graph.hpp:2377-2787`).
  Each step calls `before_step()`, `plan_.rearm()`, then `engine.execute(plan_)`, which is
  `submit(); complete();` (`modules/engine/execution/source/execution_engine.cpp:430-438`). `complete()` blocks the
  caller until the step's DAG drains (`execution_engine.cpp:565`). **A multi-step run therefore
  waits on the host after every step.**
- `submit` hands the roots to the persistent work-stealing `TaskScheduler`
  (`execution_engine.cpp:551`, `modules/engine/scheduler/source/task_scheduler.cpp:1083`). Each node is popped by
  whichever worker reaches it and submitted in `TaskScheduler::execute_task` with
  `q.submit([&](sycl::handler& h) { h.depends_on(deps); task->execute(h); })`
  (`scheduler.cpp:865-869`), the only device submit site for graph nodes.
- GPU and CPU queues are out of order; `in_order` is added only for other device types
  (`modules/engine/execution/source/runtime_context.cpp:332-339`). Order comes from explicit events, `depends_on`,
  plus a host-side atomic `in_degree` DAG that decides readiness (`graph/node.hpp:316, 556-590`).
- Completion is discovered by worker threads polling
  `event.get_info<command_execution_status>()` in a spin and backoff loop, then `wait()`
  (`scheduler/event_polling.hpp:296-404`).
- Per node the host also pays: a fresh `std::vector<sycl::event>` from
  `SmallEventVector::to_vector()` (`scheduler.cpp:721`, `node.hpp:93-105`), an `EpochGuard`
  enter and exit (`scheduler.cpp:520`), ref-count and in-flight atomics
  (`event_polling.hpp:242-267, 355, 515`), and a successor push to another worker's deque.
- Late-bound counts, bases and enabled flags are resolved once per step on the calling thread,
  not per node (`api/detail/step_machinery.hpp:118-161`). `sized_from_device` reads a host-USM
  slot at dispatch.
- A graph holds `KERNEL` nodes, `HOST` nodes that submit their own device work
  (`submits_device_work_`, `node.hpp:301-313`) and pure-native `HOST` nodes run inline on a worker
  (`scheduler.cpp:725-764`). Nothing in the tree uses `command_graph` or `ext_oneapi_graph`.

## B. Goals and non-goals

Goals: a graph whose device work can be replayed pays close to the probe's replay cost per node,
on every device SushiRuntime targets; a graph that cannot be replayed still pays no more than the
chain column; `run(steps)` stops waiting on the host between steps where nothing needs it; the
result of every run is bit-identical to today's.

Non-goals: changing any public verb's meaning; kernel-argument update inside a recorded graph
(the spike did not test it and the CPU device reports only limited graph support); multi-device
graphs, which keep today's scheduler.

## C. Design: three layers, each a fallback for the one above

**Layer 0, every graph, no behaviour change.** Remove the per-node allocation by handing
`depends_on` a view over `SmallEventVector`'s inline storage instead of a fresh vector. Measure
before and after; this is the cheapest part of the gap and must land first so the other layers
are measured against an honest baseline.

**Layer 1, the in-order chain.** At compile time `build_plan()` classifies the plan. A plan is
*chainable* when every node is a `KERNEL` node or a `HOST` node that submits device work, all on
one device, and no node reads a device-written value on the host between nodes. A chainable plan
is submitted from the calling thread, in the compiled topological order, to an in-order queue for
that device, with no scheduler hand-off, no polling and one wait at the end of the run. Every step
of `run(steps)` is submitted back to back; the single wait closes the last step. The scheduler
path stays as it is for every plan that is not chainable.

A run without profiling submits through `sycl::ext::oneapi::experimental::submit`, which returns
no event, so the queue tracks none for the nodes in flight; the queue's order carries the
dependency a returned event would, and the run's single wait closes it. A profiling run keeps its
per-node events, because the ledger reads their timestamps. Measured on a chain of trivial kernels,
the per-node cost holds near 10 us from 1 to 128 nodes, where keeping the events grew it from
15 us to 266 us.

**Layer 2, record and replay.** A chainable plan whose late-bound values all live in memory (§D)
is recorded once into a `sycl::ext::oneapi::experimental::command_graph` when it is compiled,
finalized, and replayed with one `queue::ext_oneapi_graph` per step. Recording is attempted, not
assumed from the device aspect: the spike found the OpenCL CPU device reports only
`ext_oneapi_limited_graph` yet records, finalizes and replays correctly. Any exception during
record or finalize marks the plan layer 1 for its lifetime and says so once in the log. A replayed
step is one submission; `run(steps)` is `steps` submissions back to back and one wait.

The spike measured one small graph replayed N times against one graph holding N copies: equal on
the GPU, the large graph 18% faster on the CPU, and the large graph's finalize 40 ms on the GPU
against 0.5-2.5 ms. So layer 2 records one step and replays it; it never unrolls steps.

## D. Late-bound values under replay (the hard part)

A recorded graph fixes every kernel's ND-range at record time. Today a late-bound count sets the
range at dispatch (`step_machinery.hpp:118-132`), so a recorded graph would replay a stale range.
Two things follow.

1. Under layer 2 a late-bound node is recorded at its **capacity** and its kernel reads the live
   count from a host-USM word the step machinery writes before each submission, returning early
   past it. Host-provided counts, bases and enabled flags become such words; the per-step
   resolution pass writes them instead of changing the range. The work above the live count is
   one bounds check per work-item.
2. `sized_from_device` and every internal pass that sizes itself from a value a previous node
   wrote on the device (the counting sort's digit plan, `graph.hpp:1929-2041`) cannot be resolved
   on the host between recorded nodes. Under layer 2 they read the same word on the device instead,
   which is already where the value lives. This is a change to the scan, sort and reduce lowerings,
   and it is the change that matters most to the matter solver, since grid sort is 79% of its
   dispatches.

A plan with any node that still needs a host read between nodes is not replayable and runs on
layer 1 or on the scheduler. `before_step`'s delegates still run on the host before each step and
must not read device results, which is today's rule for a host-provided count.

## E. Profiling, events and ordering

A replayed step yields one event, not one per node, so G1's per-dispatch `NodeEventRecord`s
(`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`) cannot come from layer 2. A
graph with profiling requested runs on layer 1, whose per-node events on an in-order queue are
real. `RunReport` states which layer ran, so a measurement never mixes them silently.
Determinism does not change: layers 1 and 2 submit in the compiled topological order, which is one
valid order of the same DAG, and every primitive's result is already defined to be order-free
(`fixed_order_scan.hpp`, the reduction's fixed tile plan).

## F. Owner decisions, 2026-09-19

All four taken as recommended.

1. **Automatic or opt-in.** Ruled: automatic per plan, with a `RuntimeSettings` switch that
   forces the scheduler path for comparison and debugging, and the chosen layer in `RunReport`.
2. **Capacity-sized ranges with an in-kernel bound (§D.1).** Ruled: yes, only under layer 2.
   Alternative: `command_graph` dynamic parameters, which the spike did not test and the CPU
   device's limited aspect likely excludes.
3. **Device-side sizing for scan, sort and reduce (§D.2).** Ruled: yes, as its own phase
   after layers 0 and 1, because it changes the primitives' lowering and needs their own
   bit-equality tests.
4. **Untested backends.** Level Zero, HIP and native CPU could not be run on this machine. Ruled:
   layer 2 enabled only on backends the spike or CI has run; the others take layer 1 until they
   have.

## G. Phases and tests

- **P0** layer 0: the allocation removal; a benchmark case recording per-node wall cost.
- **P1** layer 1: plan classification, the in-order chain, back-to-back steps; a functional test
  that a chainable and a non-chainable plan give bit-identical outputs, and that `RunReport`
  names the layer.
- **P2** layer 2 for plans without device-sized nodes: record, finalize, replay, fallback on
  failure; tests that replay equals layer 1 bit for bit over many steps with per-step values
  changing, and that a forced record failure falls back and logs once.
- **P3** device-side sizing in scan, sort and reduce (§D.2), so the matter solver's substep becomes
  replayable; the primitives' existing bit-equality tests must hold on every layer.

Each phase ends with the spike's table re-measured through `Graph::run` on every device this
machine has, so the gain is shown, not assumed.
