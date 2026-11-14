# SushiRuntime: replaying a graph's device work as one submission

**Status:** Shipped — 2026-11-14
**About:** SushiEngine's matter device solver is launch-bound on the device.
**Modules:** modules/engine/execution

The reductions, the prefix scan and the counting sort this document names are SushiBLAS's;
[the design map](../README.md#names-that-left-the-runtime) says how to read them.

## Problem

SushiEngine's matter device solver is launch-bound on the device. A settled tick of its
129-particle scene issues 1,484 node dispatches through `API::Graph::run` and costs 42 ms, of
which 24 ms is kernel execution; the host solver runs the same tick in under 5 ms. Grid sort alone
is 53 of the 67 dispatches a substep issues, because each prefix scan, counting sort and reduce
lowers to several nodes. A bare SYCL probe puts a node near 1.1 us when a recorded graph is
replayed, and `Graph::run` costs more than twice the probe's worst column per node. The numbers
are in the spike report.

What `Graph::run` does:

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

## Decision

Goals: a graph whose device work can be replayed pays close to the probe's replay cost per node,
on every device SushiRuntime targets; a graph that cannot be replayed still pays no more than the
chain column; `run(steps)` stops waiting on the host between steps where nothing needs it; the
result of every run is bit-identical to today's.

Non-goals: changing any public verb's meaning; kernel-argument update inside a recorded graph
(the spike did not test it and the CPU device reports only limited graph support); multi-device
graphs, which keep today's scheduler.

`HostExecutionEngine`, the engine of a host runtime, runs a plan's nodes as jobs of a
`SeededExecutor` and never replays; the ruling that makes it is LAYERED_EXECUTION ruling 17.

The owner took all four decisions as recommended:

1. **Automatic or opt-in.** Automatic per plan, with a `RuntimeSettings` switch that forces the
   scheduler path for comparison and debugging, and the chosen layer in `RunReport`. The owner's
   later ruling for layer 2 (P5) replaces "automatic" there: a graph replays only after
   `allow_replay(true)`, and `RuntimeSettings::force_chain` (`RuntimeConfig::force_chain`) keeps every
   chained plan on layer 1 for comparison.
2. **Capacity-sized ranges with an in-kernel bound.** Yes, only under layer 2. The alternative,
   `command_graph` dynamic parameters, the spike did not test, and the CPU device's limited aspect
   likely excludes it.
3. **Device-side sizing for scan, sort and reduce.** Yes, as its own phase after layers 0 and 1,
   because it changes the primitives' lowering and needs their own bit-equality tests.
4. **Untested backends.** Layer 2 is enabled only on backends the spike or CI has run; Level
   Zero, HIP and native CPU take layer 1 until they have.

## Design

Three layers, each a fallback for the one above.

### Layer 0: every graph, no behaviour change

Remove the per-node allocation by handing `depends_on` a view over `SmallEventVector`'s inline
storage instead of a fresh vector. Measure before and after; this is the cheapest part of the gap
and lands first so the other layers are measured against an honest baseline.

### Layer 1: the in-order chain

At compile time `build_plan()` classifies the plan. A plan is *chainable* when every node is a
`KERNEL` node or a `HOST` node that submits device work, all on one device, and no node reads a
device-written value on the host between nodes. A chainable plan is submitted from the calling
thread, in the compiled topological order, to an in-order queue for that device, with no scheduler
hand-off, no polling and one wait at the end of the run. Every step of `run(steps)` is submitted
back to back; the single wait closes the last step. The scheduler path stays as it is for every
plan that is not chainable.

A run that records no device timing submits through `sycl::ext::oneapi::experimental::submit`, which returns
no event, so the queue tracks none for the nodes in flight; the queue's order carries the
dependency a returned event would, and the run's single wait closes it. A run that records device timing, which is a run while the runtime has an observer, keeps its
per-node events, because the ledger reads their timestamps.

### Layer 1 for a composed plan

A `DynamicGraph` takes layer 1 only when the caller opts in with `allow_chain(true)`; off, its plan
is never classified and runs on the scheduler as before. While on, every composition classifies the
composed plan with the same `classify_chain()` a static plan uses. The composer keeps each node's
in-degree in two parts, intra-region and cross-region, and numbers its nodes from a counter that only
rises, so before classifying it re-arms every node from those two parts and captures the plan's base
state: each node's position becomes its plan index and the baseline counts every predecessor. The
chain's order is then one topological order of the whole composed DAG, edges across regions
included and the completion sentinel left out; `classify_chain()` applies a static plan's rules,
except for unordered task bodies, below. A composition that adds or drops a region classifies again,
a chain an earlier composition classified is emptied when the switch is off or a commit fails
midway, and the profiling rule above holds unchanged.

### Task bodies the dependencies leave unordered

The chain runs every HOST node's body on the calling thread, one after another. Two task bodies that
no path of the plan orders may overlap on the scheduler's workers; on the chain they cannot. The
refusal that guards this came from the distributed worker: in
`DispatchesOverDisjointBuffersAreInFlightTogether` its two dispatches over disjoint buffers meet at a
rendezvous, and run in turn the first body waits for a second that has not started.

Before P7, `classify_chain()` refused every plan that held a HOST node and any two nodes no path
orders, so one task body beside two independent kernels kept a plan off the chain. P7 states the
rule as the hazard is: only two HOST nodes that no path orders are a concern. A task body beside
unordered kernels chains, because one host body runs at a time either way and the order of the
kernels on the in-order queue is one the DAG allows. The pairs come from one walk of the
topological order that carries, per node, the set of HOST nodes reaching it, O((nodes + edges) ×
hosts / 64).

For such a pair, disjoint data proves nothing on its own: the worker's two bodies touch disjoint
buffers and still need to overlap. What makes running them in turn safe is the task-body contract,
which says a body waits for nothing and touches only the regions it names
(`api/graph/plumbing/README.md`, add_task). A static `Graph` takes the chain without asking its
caller, so it cannot assume every body keeps that contract, and it keeps the refusal for any such
pair. A `DynamicGraph` takes the chain only after `allow_chain(true)`, and that opt-in also states
that its task bodies keep the contract. Under it the composer classifies with
`UnorderedHostWork::InTurnWhenDisjoint`: an unordered pair chains when both nodes name every byte
they touch, with no untracked region and no live address lists, and share no byte, reads included.
A shared read is safe in principle; it stays on the scheduler until a plan that needs it is
measured.

Splitting the plan, so that the calling thread runs host nodes at their dependency points and the
chain carries the rest, was weighed and not taken. It answers native host work, which is refused for
another reason, and not this refusal: a task node already rides the chain, and the calling thread
would run the two bodies in turn all the same. The decision sits wholly in classification, so
ChainRun, the one event per node of a timed run, and the order layer 2 would record do not change.

### Layer 2: record and replay

A chainable plan whose late-bound values all live in memory is recorded once into a
`sycl::ext::oneapi::experimental::command_graph` at its first replayed run after it is compiled or
composed, finalized, and replayed with
one `queue::ext_oneapi_graph` per step. Recording is attempted, not assumed from the device
aspect: the OpenCL CPU device reports only `ext_oneapi_limited_graph` yet records, finalizes and
replays correctly. Any exception during record or finalize marks the plan layer 1 for its
lifetime and says so once in the log. A replayed step is one submission; `run(steps)` is `steps`
submissions back to back and one wait.

Layer 2 records one step and replays it; it never unrolls steps, because the large unrolled
graph's finalize is far slower on the GPU.

#### The opt-in and the record's lifetime

Layer 2 is opt-in and off by default, for `Graph` and `DynamicGraph` alike: `allow_replay(true)`,
which both take from their shared step driver, lets a plan that already runs as a chain replay. Off,
nothing changes. A `DynamicGraph` runs as a chain only under `allow_chain(true)`, so it replays only
with both. The opt-in states, as `allow_chain(true)` does for task bodies, that every command group
the caller recorded submits the same work at every step; the runtime cannot read a raw command group
to check it. A move keeps the setting. `RuntimeSettings::force_chain`, set from
`RuntimeConfig::force_chain`, keeps every chained plan on layer 1 whatever its graph allows, so a run
can be compared with its replay.

The step loop picks the layer per run: the scheduler when the plan is not chainable or the runtime
forces the scheduler, layer 1 when the graph did not opt in, the run is timed or `force_chain` is on,
and layer 2 otherwise. The first such run records the plan, not the compile, so a plan that never
replays records nothing. The recording lives on the plan's chain record (`ChainPlan::recorded`), so
a recompile builds a plan without one and every composition, which classifies the plan again,
replaces the chain record and drops it; the next run records once more. A refusal or a failed
recording sets `ChainPlan::not_replayable`, and the plan stays on layer 1 until it is classified
again; it is never retried in between.

The recording runs on a private in-order queue of the plan's device, so no other submission to the
shared in-order queue can be captured. Each node is submitted in the chain's order with no event
dependency, as ChainRun submits it, and the queue's order becomes the graph's edges. The finalized
graph is replayed on the untimed in-order queue layer 1 uses, with `queue::ext_oneapi_graph`, once
per step. `ChainReplay` (`modules/engine/execution/`) is its own unit beside `ChainRun`; the step
loop is shared by both and the replay adds no branch to `ChainRun`.

#### What a recording refuses

A chainable plan is recorded only when every node's submission is one a recorded graph can keep.
Each node carries a `SubmitForm`, set by the verb that recorded it: a launch, a captured launch and a
raw command group are `Fixed`; an `add_dynamic` window is `LateWindow`; a device-sized launch is
`DeviceSized`; everything else is `StepBound`. The first refusal is kept and logged at debug level,
as `classify_chain()` logs its own:

- a task body, because a recording is not shown to capture what its body submits;
- a `StepBound` node, whose command group reads host state that may change between steps: a State
  rule reads the fields the State swaps, and a late-count resolver reads its sources at submit;
- a device-sized node, or a late-bound window with a device count or base slot, because P2 and P5
  cover only plans without device-sized nodes;
- a backend other than CUDA and OpenCL, which decision 4 keeps on layer 1 until the spike or CI
  has replayed on it.

Native host work never reaches layer 2: it keeps a plan off the chain already. Any exception while
recording or finalizing is logged once as a warning, with the plan's node count and the error, and
the run that met it continues on layer 1.

### Late-bound values under replay

A recorded graph fixes every kernel's ND-range at record time. Today a late-bound count sets the
range at dispatch (`step_machinery.hpp:118-132`), so a recorded graph would replay a stale range.

1. Under layer 2 a late-bound node is recorded at its **capacity** and its kernel reads the live
   count from a host-USM word the step machinery writes before each submission, returning early
   past it. Host-provided counts, bases and enabled flags become such words; the per-step
   resolution pass writes them instead of changing the range. The work above the live count is
   one bounds check per work-item.
2. `sized_from_device` and every internal pass that sizes itself from a value a previous node
   wrote on the device (the counting sort's digit plan, `graph.hpp:1929-2041`) cannot be resolved
   on the host between recorded nodes. Under layer 2 they read the same word on the device instead,
   which is already where the value lives. This is a change to the scan, sort and reduce lowerings,
   and it matters most to the matter solver, since grid sort is 79% of its dispatches.

A plan with any node that still needs a host read between nodes is not replayable and runs on
layer 1 or on the scheduler. `before_step`'s delegates still run on the host before each step and
must not read device results, which is today's rule for a host-provided count.

P5 builds item 1 for an `add_dynamic` window and leaves item 2 to the refusal above. While the
recording submits such a node, its `LateBindings::replay_words` points at three host-USM words, and
the window lowers to a launch at capacity whose work-items read the enabled flag, the count and the
base from them; layer 1 never sees the words and keeps its live-range launch. Before each replay
`ChainReplay` copies each window's enabled flag, count and base from its bindings into its words, so
the step machinery and its providers are unchanged. A disabled window returns before its bounds
check, which is what layer 1's skipped node leaves; a window that does not fit records the same
fault layer 1 records on the host.

The words are read when the replay executes, not when it is submitted, so a plan whose recording
reads words waits for the replay before it before writing the next step's words; its steps are one
submission each, but not back to back. A plan without a late-bound window replays its steps back to
back with one wait at the end. One set of words per parity, or the graph extension's dynamic
parameters, would lift the wait; neither is built, and decision 2 already set the dynamic
parameters aside.

### Profiling, events and ordering

A replayed step yields one event, not one per node, so G1's per-dispatch `NodeEventRecord`s
(the g1 node event records design spec in the version history) cannot come from layer 2. A
plan that records device timing runs on layer 1, whose per-node events on an in-order queue are
real. `RunReport` states which layer ran, so a measurement never mixes them silently:
`ExecutionLayer::Replay` names a replayed run, and a timed run of a graph that allows a replay
reports `ExecutionLayer::Chain`. The graph extension's `property::graph::enable_profiling` gives one
span per replay, not one per node, so layer 2 does not use it.

#### Cost of a timed chained submit

A timed chained submit costs the host more than an untimed one for causes that sit in the submit call,
not in the runtime's bookkeeping. The untimed chain submits through `experimental::submit` on a plain in-order queue and keeps no
event. The timed chain submits through `queue.submit` on a queue built with `enable_profiling`, keeps
the returned event, and takes a steady reading and a `PendingEvent` per node. Under CUDA the adapter creates three
`CUevent`s per profiled command, records them around the kernel and on the host-submit-time stream, and
SYCL reads a device timestamp for the submission time (`SUBMIT_FUNNEL.md`); each of those is a driver
call under the per-context serialisation. The runtime's own per-node work (`steady_now_ns`,
`snapshot_widths`, one `push_back` into the reserved vector) is under 0.2 microseconds and allocates
nothing. The reads of the events, the ledger writes and the observer hand-off happen after the wait, so they are in the
run's wall time and not in the `chain.submit_step` zone. This split is expected, not measured: `Benchmark_TimedChainSubmit` in
`tests/surface/profiling/` prints host microseconds per submit and per run with and without an observer (RW-140), and
the split is confirmed or corrected by its row on CUDA.
Determinism does not change: layers 1 and 2 submit in the compiled topological order, which is one
valid order of the same DAG, and every primitive's result is already defined to be order-free
(`fixed_order_scan.hpp`, the reduction's fixed tile plan).

### The scheduler path under many submitting workers

A plan that stays on the scheduler submits from every worker, and the submissions contend on
locks inside DPC++ and the CUDA driver. [The submit funnel page](SUBMIT_FUNNEL.md) maps those
locks, measures the alternatives and holds the opt-in design phase P6 builds.

With `RuntimeConfig::submit_funnel` on, each device gets one submitting thread on top of the
workers. A worker hands a ready kernel node to it through a lock-free ring and takes the event
back through a lock-free ring of its own, so only that thread enters the device's submission
locks. The queue, the dependencies and the one event per node stay as they are, so outputs and
`NodeEventRecord`s are the same as without the funnel, and `RunReport::funnelled` says that the
funnel submitted a run's kernel nodes. Off, which is the default, the scheduler runs as before.

## Phases

| Id | Phase | State |
| --- | --- | --- |
| P0 | Layer 0: the allocation removal; a benchmark case recording per-node wall cost | built |
| P1 | Layer 1: plan classification, the in-order chain, back-to-back steps. Test: a chainable and a non-chainable plan give bit-identical outputs, and `RunReport` names the layer | built |
| P2 | Layer 2 for plans without device-sized nodes: record, finalize, replay, fallback on failure. Test: replay equals layer 1 bit for bit over many steps with per-step values changing, and a forced record failure falls back and logs once. P5's opt-in implementation carries it out for both graph kinds | built |
| P3 | Device-side sizing in scan, sort and reduce, so the matter solver's substep becomes replayable. The primitives' existing bit-equality tests hold on every layer | built |
| P4 | Layer 1 for DynamicGraph as an opt-in: allow_chain(true) makes each composition classify its plan, so a chainable composed plan runs as a chain; off by default, so today's scheduler path is unchanged. Test: a composed plan gives bit-identical outputs on the chain and on the scheduler, and RunReport names the layer | built |
| P5 | Layer 2 for a graph that opts in with allow_replay(true), Graph or DynamicGraph, off by default: a chainable plan with no device-sized node, no task body, no State rule and no timed run is recorded at its first replayed run after each compile or composition and replayed with one submission per step; add_dynamic windows are recorded at capacity and read host words; a refusal names its reason, any record failure falls back to layer 1 for the plan's lifetime and logs once; RunReport names ExecutionLayer::Replay and RuntimeConfig::force_chain forces layer 1. Test: replay equals layer 1 bit for bit over many steps with a per-step window, for Graph and DynamicGraph | built |
| P6 | Submit funnel for the scheduler path, opt-in through RuntimeConfig::submit_funnel and off by default: per device, workers push ready kernel nodes into a lock-free ring and one thread submits them with the same queue, dependencies and per-node events (SUBMIT_FUNNEL.md). Test: a wide plan gives bit-identical outputs with the funnel on and off, profiling off and on, and a timed run keeps one NodeEventRecord per node | built |
| P7 | Layer 1 for a composed plan whose task bodies the dependencies leave unordered: classify_chain() refuses only a pair of HOST nodes no path orders, not every plan with a HOST node and an unordered pair; under allow_chain(true) such a pair chains when both declare every byte they touch and share none, reads included; a static plan keeps the refusal for any such pair. Test: two unordered task bodies over disjoint data in a composed plan run as Chain with the forced scheduler's bytes, two that read one buffer stay on the scheduler, and a static plan's unordered pair stays refused | built |

Each phase ends with the spike's table re-measured through `Graph::run` on every device the
machine has, so the gain is shown, not assumed. P0, P1 and P3 have not had that measurement.

### Work

<!-- work:begin -->
No open work.
<!-- work:end -->

## Open questions

- What does `Graph::run` cost per node on each device after P0, P1 and P3?
