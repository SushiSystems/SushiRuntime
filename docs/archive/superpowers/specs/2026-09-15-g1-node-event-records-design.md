# SushiRuntime gate G1: per-node event records and a per-graph profiling switch

**Status:** designed 2026-09-15; every decision in §F taken by the owner the same day. Nothing is
built. Consumer: SushiEngine's matter device solver plan
(`docs/agent/plans/2026_09_15_MATTER_DEVICE_SOLVER_PLAN.md` there, tasks T1.1 and T2.2), whose
engine `NodeEvent` and `ClockPairing` this record matches field for field. Paths are relative to
this repository unless marked.

## 0. What the tree does today

- Queues are built once in `RuntimeContext::probe_hardware`, one per enumerated device, with
  `enable_profiling` added only when `RuntimeSettings::profiling` is true
  (`src/execution/runtime_context.cpp:296-322`; the setting is `runtime_settings.hpp:72`). The
  same `sycl::queue` goes into `all_queues` and `numa_aware_queues` (`runtime_context.cpp:322,335`).
- `RuntimeContext::profiling_enabled()` just returns that setting (`runtime_context.cpp:231-234`).
  Every consumer reads it: the scheduler publishes it to the poller per run
  (`src/scheduler/scheduler.cpp:240-242`), host-node timing is gated on it
  (`scheduler.cpp:725`), `Graph::run`/`run_async` reset and collect on it
  (`api/graph/graph.hpp:1325,1458,1584-1586,1626`), and so does `DynamicGraph`
  (`api/graph/dynamic_graph.hpp:370,439,931,1053-1086`). `DynamicGraph` even skips filling
  `plan_.all_nodes` when it is off (`dynamic_graph.hpp:928-934`).
- The poller reads `command_start`/`command_end` and folds the difference into
  `Node::record_completion(device_ns)` (`scheduler/event_polling.hpp:402-427`,
  `graph/node.hpp:592-599`), which sums into atomics `NodeProfiling{invocations, device_ns, host_ns}`
  (`node.hpp:203-208`). `collect_profiling` turns them into `Core::NodeTiming` rows
  (`src/execution/execution_engine.cpp:313-331`, type at `core/run_report.hpp:77-84`).
- Dispatch produces exactly one `sycl::event` per node dispatch: `q.submit` for a kernel node, or
  whatever `execute_host(q, deps)` returns for a host-work node (`scheduler.cpp:808-833`). A
  host-work lambda may submit several command groups itself (`scheduler.cpp:744-747`), but the
  runtime only ever holds the one event it returns. Pure native nodes run inline on the worker
  (`scheduler.cpp:717-739`) and push a ready flag with no event (`scheduler.cpp:831-833`).

Consequence for the approved engine shape: "one record per SYCL command group" is observable by
the runtime only as "one record per node dispatch". See F1.

## A. The enable_profiling constraint

A SYCL event exposes `command_submit/start/end` only when its queue carries
`property::queue::enable_profiling`. The queue's property list is fixed at construction.

Cost of the property: I found no vendor number in the local toolchain, and I will not invent one.
What the local DPC++ headers show is mechanism, not cost: `submit_profiling_tag` falls back to a
barrier on a profiling queue and otherwise requires `aspect::ext_oneapi_queue_profiling_tag`
(`D:\Projects\sushistack\dependencies\toolchains\llvm-sycl\include\sycl\ext\oneapi\experimental\profiling_tag.hpp:21-40`,
aspect id 73 in `include/sycl/info/aspects.def:66`). The adapter sources (UR CUDA / Level Zero /
OpenCL) are not in the stack, so the per-submit cost (an extra device event object and timestamp
query per command group on CUDA; `CL_QUEUE_PROFILING_ENABLE` on OpenCL; a per-event timestamp
query on Level Zero) must be measured, not cited. G1 therefore carries a benchmark (E7) whose
result decides option (i) versus (ii).

Options:

1. **Always profiling queues, gate recording per graph.** Simple, one queue per device keeps
   every scheduler assumption (`get_queue(index)` cached per node, `scheduler.cpp:793-805`;
   USM context routing, `scheduler.cpp:744-750`). Cost: whatever the backend charges per submit,
   paid by every graph including unprofiled ones. That violates "zero cost when off" unless E7
   shows it is noise. Deterministic: identical dispatch path on and off.
2. **A second, profiling-enabled queue per device, chosen per graph.** Build it in
   `probe_hardware` beside the plain one, sharing the same `sycl::context` (required: USM
   allocated through the plain queue's context must stay valid, and `context_index_for_ptr`
   routes on context, `scheduler.cpp:767`). Selection is one branch at queue lookup, keyed on
   the plan's flag. Off-graphs pay nothing beyond that branch. Costs: one extra queue object per
   device (native handles: a CUDA stream/context-local queue, a L0 command queue), and two queues
   on one device break "one queue per device" as an ordering assumption. A profiled graph and an
   unprofiled graph running concurrently on the same device no longer share a submission order;
   dependencies between them already go through `depends_on`, and in-order FPGA queues
   (`runtime_context.cpp:301-309`) would lose strict cross-graph order. Needs a lazy build
   option so machines that never profile pay no second queue.
3. **Profiling tag extension.** On devices with `ext_oneapi_queue_profiling_tag`, a tag event on a
   plain queue gives timestamps without the queue property, but only at tag points, not per
   kernel; it gives submit/start/end of the tag, not of the node. Not sufficient for per-node
   records. Rejected as the primary path, noted as a pairing helper.
4. **Keep the runtime-wide setting, make the graph flag a filter.** This is (1) with the queue
   decided by `RuntimeSettings`: a graph asking for profiling on a non-profiling runtime gets
   host records only, or an error. Honest but not what the owner ruled.

**Measured, 2026-09-15, RTX 3080 Ti, CUDA adapter.** A standalone DPC++ program submitted 200
`parallel_for` per tick and waited, three repetitions per row, ranges 64 and 16384:

| Queue | ms per tick |
| --- | --- |
| plain | 3.0 to 6.6 |
| `enable_profiling` | 7.7 to 8.6 |
| `enable_profiling`, three timestamps read per event | 8.2 to 9.9 |

About 24 µs per submission. Option (2), a lazily built second queue, is taken (F2).

## B. Public surface

```cpp
// core/run_report.hpp (beside NodeTiming)
namespace SushiRuntime::Core
{
    /** @brief Times one dispatch of one node, in nanoseconds on the backend's event clock. */
    struct NodeEventRecord
    {
        std::uint32_t node_index = 0;  /**< Registration index of the node in its graph. */
        std::uint32_t invocation = 0;  /**< Zero-based dispatch count of the node within the run. */
        std::uint64_t submit_ns  = 0;  /**< SYCL command_submit, or the paired host clock for a host node. */
        std::uint64_t start_ns   = 0;  /**< SYCL command_start, or the paired host clock. */
        std::uint64_t end_ns     = 0;  /**< SYCL command_end, or the paired host clock. */
    };

    /** @brief Pairs one event clock reading with one steady clock reading taken beside it. */
    struct ClockPairing
    {
        std::uint64_t event_ns       = 0;
        std::uint64_t steady_ns      = 0;
        std::uint64_t uncertainty_ns = 0;
    };

    struct RunReport
    {
        // ... existing fields unchanged ...
        std::vector<NodeEventRecord> node_events; /**< Empty unless the graph requested profiling. */
        ClockPairing                 clock{};     /**< All zero when node_events is empty. */
    };
}
```

Field-for-field identical to the engine's `NodeEvent`/`ClockPairing`
(`D:\Projects\sushiengine\.superpowers\sdd\2026_09_15_MATTER_DEVICE_SOLVER_PLAN\task-T1.1-shape.md`),
so the engine backend copies with no translation. `RunReport::reset()` (`run_report.hpp:~140`)
clears both, keeping capacity.

Graph-side switch, symmetric on both graphs:

```cpp
void Graph::set_profiling_requested(bool requested) noexcept;        // marks dirty_
bool Graph::profiling_requested() const noexcept;
void DynamicGraph::set_profiling_requested(bool requested) noexcept; // marks a full recompose
bool DynamicGraph::profiling_requested() const noexcept;

void Graph::run(Core::RunReport& report);                 // one step, clears and refills report
void Graph::run(std::size_t steps, Core::RunReport& report);
void DynamicGraph::run(Core::RunReport& report);
```

A setter, not a `GraphOptions` struct: the graphs take no options object today (`graph.hpp:1167-1254`
has only `run`, `run_for`, `run_until`), and a one-field struct is premature. The flag is stamped
into `CompiledGraph` at `build_plan()` as `bool profiling`, and every current
`ctx_->profiling_enabled()` read on the run path becomes `plan_.profiling`.
The existing `Core::RunReport run(...)` overloads stay and forward to the new ones.

`RuntimeSettings::profiling` stays (F3) as the default value a new graph's
`profiling_requested()` starts from, and under option (2) also as "build the profiling queues
eagerly". Do not remove it: CONTRIBUTING says pre-1.0 changes carry no changelog yet but do
update INTRODUCTION.md and ARCHITECTURE.md (`docs/CONTRIBUTING.md:224-240`); the `api-stability`
skill governs deprecation and should be read by the implementer. Decided in F3.

`NodeTiming`/`WorkerTiming` stay; they derive from the same capture (F4).

## C. Capture without steady-state allocation

- **Buffer.** `CompiledGraph` gains a `ProfilingLedger`: one `std::vector<NodeEventRecord>` slot
  array plus per-node write cursors (`std::atomic<std::uint32_t>` next invocation). Sized at
  `build_plan()` to `node_count * capacity_per_node`.
- **k per node is not known up front.** A dispatch yields one event (section 0), so for
  `run(steps)` k = steps, known before the loop (`graph.hpp:1596`). For `run_for`/`run_until`
  and for `run(RunReport&)` called every frame, k is 1 per call. Rule: size to the high-water
  mark; grow only when a run exceeds it (the first run of a new shape allocates, later runs of the
  same shape do not). `run(steps)` reserves `steps` before the loop, so the growth is before
  dispatch, never inside the poller.
- **Device node.** In `accumulate_profiling` (`event_polling.hpp:402`), read all three
  timestamps and write `{plan->registration_index, cursor++, submit, start, end}` into the
  node's slot. Written by exactly one reaping worker per dispatch (`node.hpp:584-590` documents
  the same guarantee), so the slot write needs no lock; the cursor is atomic.
- **Host node.** The inline native path (`scheduler.cpp:725-733`) already takes steady time
  around `execute_native()`. Store raw steady `h0` as submit and start, `h1` as end, flagged host.
  At collect, convert with the run's pairing: `event = steady - (pairing.steady_ns - pairing.event_ns)`.
  A host-work node with a returned event is a device record; one returning an empty event gets the
  host treatment.
- **Pairing.** Taken once per run in the scheduler before the first root is dispatched: read
  `steady_clock` into `steady_ns`; the first device submission of the run records its event;
  after `submit` returns read steady again for `uncertainty_ns`; `event_ns` is read from that
  event's `command_submit` at reap. A run with no device submission (all native) sets
  `event_ns = steady_ns`, uncertainty 0 (same as NativeBackend). The "first submission" is
  claimed with a compare-exchange on a per-run atomic flag so concurrent workers race safely.
- **Collect.** `collect_profiling` appends the used slots into `report.node_events` in node order;
  `report.node_events` is `clear()`ed, not reassigned, so its capacity persists (the E3 test
  proves it).
- **Off.** One `if (plan.profiling)` in the poller and one at host dispatch; the poller's
  existing `profiling_` atomic (`event_polling.hpp:195,265,300`) becomes the per-run copy of the
  plan flag.

## D. Node index mapping

- **Static Graph.** `plan_index` is NOT registration order. `TaskGraph` pushes nodes onto a
  lock-free LIFO stack (`src/graph/task_graph.cpp:136-144`) and `compile()` pops it into
  `all_nodes` (`task_graph.cpp:569-584`), so `all_nodes` is reverse registration order, and
  `plan_index = i` is stamped from it (`execution_engine.cpp:614-621`). Also `add()` shapes may
  register more than one node each (`graph.hpp:125-408`). Fix: stamp a `registration_index`
  on the node at `TaskGraph::add` time from a per-graph counter, and emit that as `node_index`.
  One `Graph::add` that registers several internal nodes gives all of them one index (F5).
- **DynamicGraph.** `plan_index` comes from `plan_index_counter_` that never resets
  (`dynamic_graph.hpp:730,1140`), so a region rebuilt ten times keeps climbing. Regions live in
  `std::map<RegionKey, ...>` (`dynamic_graph.hpp:1105,1109`): iteration is ascending key, not
  insertion. The approved shape says "across regions in insertion order", which the tree does not
  track. Options: (a) define `node_index` as ascending region key then add() order, reassigned
  at each recompose (dense, stable while the region set is stable); (b) add a region
  creation sequence and order by it. Option (a) is taken (F6), because it matches the documented
  cross-region ordering (`dynamic_graph.hpp:72-76`) and needs no new state. Decided in F6.

The index the records carry was not readable through the API that produced them: `Graph`
reported `size()`, a count of recorded rules, while a record named a registration index, and a
caller wanting to attribute a dispatch to its own `add()` had to guess the relation. SushiEngine
guessed the two were the same sequence and its matter solver's stage attribution silently blamed
the wrong stage. `Graph::rule_registration(rule)` closes it: the index each recorded rule took,
resolved when the plan compiles, `no_registration` before that and renumbered by a recompile.


## E. Tests (sr test), each failing for its own reason

1. `ProfilingNotRequestedLeavesNodeEventsEmpty` - profiling-capable runtime, graph not requested;
   fails if any record leaks.
2. `RequestedGraphRecordsOnePerDispatch` - 3 kernel nodes, `run(5)`; expects 15 records,
   invocations 0..4 per node, `submit <= start <= end`. Fails on double count or missing reads.
3. `NodeEventsReuseCapacity` - `run(report)` twice with the same report under the existing
   allocation counter (`tests/functional/integration/test_steady_state_allocation.cpp`); fails
   on any allocation in the second run.
4. `NodeIndexIsRegistrationOrder` - nodes registered A, B, C with distinct durations; fails
   against today's reverse `plan_index`.
5. `TwoGraphsIndependentSwitch` - one requested, one not, same runtime; fails if the flag is
   runtime-wide.
6. `HostNodeLandsOnEventClock` - a native sleep node beside a kernel; converted host records sit
   inside the run's event window within `uncertainty_ns`; fails on a missing or inverted offset.
7. `ProfilingOffDispatchCost` (benchmark, `tests/benchmark`) - same graph on plain vs profiling
   queue on each available backend; reports per-submit delta. Decides F2, not a pass/fail gate.
8. `DynamicGraphIndexAcrossRecompose` - two regions, drop and re-add one; indices stay dense and
   ordered per F6; fails against today's ever-growing counter.
9. Existing `Integration_Observability` tests (`test_observability.cpp:47-237`) keep passing
   with `RuntimeSettings::profiling` as the default.

## F. Owner decisions, 2026-09-15

1. **One record per node dispatch.** Command groups a host-work lambda submits internally are not
   recorded; capturing them needs a runtime-owned submit hook and is a separate gate.
   `invocation` is the step index within `run(steps)`, zero for a single run.
2. **Queue strategy: a second, profiling-enabled queue per device**, built lazily on the plain
   queue's context the first time a graph requests profiling, per the measurement in §A. The
   clock pairing is taken on the queue that ran the node.
3. **`RuntimeSettings::profiling` stays** as the default for a new graph's
   `profiling_requested()` and as the request to build the profiling queues eagerly. No
   deprecation.
4. **`NodeTiming` derives from the new records** at collect, so there is one capture path.
5. **`node_index` is the public registration.** Every internal node one `add()` call creates,
   such as a reduction's tile levels, shares that call's index, so several records may share a
   `(node_index, invocation)` pair. The engine sink groups by that pair.
6. **DynamicGraph order:** ascending region key, then `add()` order, numbered densely at each
   recompose and stable until the next one.
7. **A run with no device submission** records the identity pairing: `event_ns = steady_ns`,
   uncertainty zero.
