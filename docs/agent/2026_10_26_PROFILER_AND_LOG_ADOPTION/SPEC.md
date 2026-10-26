# SushiRuntime adopts SushiProfiler, and logs where it is silent: analysis

**Status:** Open — in progress; all code of PLAN.md is written and nothing is built; section 8 holds the decisions and the amendments.

**Implements:** PROFILING#P1, PROFILING#P2, PROFILING#P3, PROFILING#P4

Decision D22 opened `docs/design/PROFILING/README.md`; its phases are the ones named above.

Read on 2026-10-26: this repository on branch `feature/layered_execution`, SushiProfiler before
its v0.1.0 tag (`bc86dc9`), SushiLog at `e39e190` (last tag v0.2.3; the runtime pins v0.2.2 in
`cli/sushistack.deps.toml:71-73`). Every claim below carries the file and line it was read at. A
claim that was not verified says so.

## 1. Profiling

### 1.1 What the runtime measures today, and who reads it

| Measure | Produced at | Ends in | Read by |
| --- | --- | --- | --- |
| Device times of one dispatch (submit, start, end) | `event_polling.hpp:393-427` (scheduler path), `chain_run.cpp:277-306` (chain path) | `ProfilingLedger`, then `RunReport::node_events` (`scheduler_execution_engine.cpp:371`) | Tests only; no file under `applications/` reads `node_events`, `node_timings` or `worker_timings` (grep, 2026-10-26) |
| Host-native node times | `task_scheduler.cpp:630-659` | the same ledger; moved onto the event clock at `scheduler_execution_engine.cpp:375-388` | the same |
| One `NodeTiming` per registration index | `scheduler_execution_engine.cpp:391-424` | `RunReport::node_timings` | the same |
| Worker busy, stealing, polling and idle nanoseconds | `task_scheduler.cpp:429-559` | `RunReport::worker_timings` (`scheduler_execution_engine.cpp:426-439`); `scheduling_overhead_ms` is the polling sum (`:442`) | the same |
| Run wall time, plan compile time | `step_run.cpp:213-231`, `step_driver.hpp:509-515` | `total_duration_ms`, `graph_analysis_duration_ms` (`run_report_builder.hpp:60`) | the same |
| Explicit transfer bytes | `step_run.cpp:236-237` | `host_to_device_bytes`, `device_to_host_bytes` | the same |

Not measured anywhere: the time `USMAllocator::allocate` and `release` take (`usm_allocator.cpp:101-140`),
the time a thread waits for `pending_mutex` or any other lock (list in 1.6), the wait in
`complete()` split into latch, quiescence and asynchronous errors (`scheduler_execution_engine.cpp:579-588`),
the cost of `queue.submit` (`task_scheduler.cpp:727`; only the first kernel's call is timed, as the
clock pairing, `:714-746`), the breakdown of a plan build (`plan_cache.cpp:100-147`), and the device
total of a run that is not profiled (1.2, item 2).

### 1.2 The path from a SYCL event to a Source

1. A node dispatch has device timestamps only when its plan has `plan.profiling` set. The flag is
   written when a plan is built (`plan_cache.cpp:138`, `region_composer.cpp:382-391`) from the
   graph's request (`plan_cache.hpp:97-103`), which defaults to `RuntimeConfig::profiling`
   (`step_run.cpp:180-183`).
2. A profiled run takes a different queue than an unprofiled one. The scheduler asks
   `dispatch_queue()` for the out-of-order profiling queue (`task_scheduler.cpp:224-229`,
   `QueueFlavour{false, true}`); the chain asks for `QueueFlavour{true, graph.profiling}`
   (`chain_run.cpp:90-91`). With profiling off the chain submits with no event at all
   (`chain_run.cpp:75-82`, `:230-248`), so an unprofiled run has no device time. SushiAI's plan of two
   compared runs (CLIENTS.md, SushiAI section) is the only way to get both numbers; the cost of the
   second is not measured.
3. A queue of a flavour that no earlier run asked for is built on first use under a `call_once`
   (`device_queue_set.cpp:218-234`). The first profiled run pays for it, unrecorded.
4. Each dispatch's event is read after it completes: `read_event_timestamps()` makes up to three
   `get_profiling_info` calls and swallows every exception (`event_timestamps.hpp:54-81`). The
   scheduler path does it in the poller (`event_polling.hpp:399`), the chain path after its one
   wait (`chain_run.cpp:283`).
5. The record is `NodeEventRecord`, 40 bytes: `node_index`, `invocation`, `launched_width`,
   `live_width`, `submit_ns`, `start_ns`, `end_ns` (`run_report.hpp:112-121`). It names no device,
   no queue and no submitting thread.
6. `ProfilingLedger` stores the records per registration index at a high-water-mark size
   (`profiling_ledger.hpp:101-145`, `:158-169`) and appends them in node order at `collect()`
   (`:178-191`). `collect_profiling()` then copies them into `report.node_events` and sets
   `report.clock` from the ledger's pairing (`scheduler_execution_engine.cpp:371-372`).
7. The clock pairing is one `ClockPairing{event_ns, steady_ns, uncertainty_ns}` per run
   (`run_report.hpp:130-135`, `profiling_ledger.hpp:227-237`). The first kernel submission to claim it
   times its own `submit` call with `steady_clock`: `steady_ns` is the call's start and
   `uncertainty_ns` is the whole call's duration (`task_scheduler.cpp:714-746`,
   `chain_run.cpp:253-272`). `event_ns` is that kernel's `command_submit`, read at reap
   (`event_polling.hpp:406-407`, `chain_run.cpp:287-288`). A run with no device submission gets the
   identity pairing `{run_start, run_start, 0}` (`profiling_ledger.hpp:236`). The first submit of a
   process may include driver compilation; the size of that is unverified.
8. Host-native nodes carry steady times in the ledger. `collect_profiling()` converts them onto the
   event clock with the offset `steady_ns - event_ns` (`scheduler_execution_engine.cpp:375-388`), so
   every record of a report is on the event clock and the source converts all of them back:
   `steady = t - clock.event_ns + clock.steady_ns`. `steady_clock` here and `TraceClock::steady_now_ns()`
   in SushiProfiler are the same clock (`trace_clock.hpp:69-74`), so `SourceContext::to_record_ns`
   takes the result directly.

Findings in this path:

- **Three writers of one record.** `NodeEventRecord` is assembled in `event_polling.hpp:409-425`,
  `chain_run.cpp:290-303` and `task_scheduler.cpp:645-659`, each with its own `claim_slot` and drop
  rule. Adding a field means three edits; the plan makes it one first (PF-3).
- **One pairing for every device.** Two devices may not share an event clock; a CPU SYCL device and a
  GPU are the likely pair on the owner's machine. Nothing in the record says which device a dispatch ran
  on, so the report cannot even be split to pair per device. Unverified on hardware: whether the
  devices of one backend share a timeline.
- **Dispatches vanish silently.** Four places drop a record with no trace (table in 2, items E4 and
  E5); in a release build `ProfilingLedger::collect` caps the row instead of asserting
  (`profiling_ledger.hpp:183-186`).
- **`NodeTiming` is a per-run node table built from strings.** It copies each node's name every
  run (`scheduler_execution_engine.cpp:409-423`; `foundation/report/README.md` says the labels are
  rebuilt each run by design), keys rows by `Node::plan_index` and not by registration index
  (`run_report.hpp:83`, `scheduler_execution_engine.cpp:415`), and skips a node that never ran (`:408`).
  It cannot serve as the join table.
- **`OperationID` has no text.** It is a 32-bit hash of the name (`operation_id.hpp:40-105`); the
  runtime holds no table from hash to name. A span attribute `operation` can be the hash only (D10).
- **Toggling profiling rebuilds the plan.** `PlanCache::set_profiling` marks the plan dirty
  (`plan_cache.hpp:99-102`). The sentinel's host work captures the flag when it is first wired
  (`scheduler_execution_engine.cpp:530-549`), so the flag cannot simply be read per run. A window that
  opens the gate at its warm-up period and closes it after the active ones costs two rebuilds if the
  host toggles per window (D7).

### 1.3 The device source

A `SushiProfiler::Source` named `sushiruntime.device` (CLIENTS.md, SushiRuntime section). What it
builds from a run, with the field each SushiProfiler value comes from:

| Span or table field | From | Note |
| --- | --- | --- |
| `queued_ns` | `submit_ns` through the pairing | a host node has `submit == start` (`task_scheduler.cpp:654-656`) |
| `start_ns`, `end_ns` | `start_ns`, `end_ns` through the pairing | |
| `uncertainty_ns` | `clock.uncertainty_ns`, saturated to 32 bits | the record field is `uint32_t` (`span_table.hpp`, `Span`); the pairing's is 64 bits |
| `track` | one per device (`DeviceDescription::name`, index) and one for host-native nodes | needs the device of a dispatch (D5); a node's device is fixed at its first dispatch (`task_scheduler.cpp:670-686`, `node.hpp` `cached_queue_index_`) or by the chain's queue (`chain_plan.cpp:213-228`) |
| `name` | the node table's name, interned | needs `intern` after `begin()` (SushiProfiler gap G3) |
| `submit_thread` | the submitting thread's profiler index | `Collector::thread_index()` read on the submitting thread (`collector.hpp`); the record has no field for it (D5) |
| attribute `node` | `node_index` | integer; the registration index of one `add()` call |
| attribute `invocation` | `invocation` | the step index within the run (`report/README.md`, NodeEventRecord) |
| attribute `operation` | node table, `OperationID::value` | integer hash (D10) |
| attributes `launched_width`, `live_width` | the record | omitted when `K_WIDTH_UNKNOWN` |
| period fact `layer` | `RunReport::layer` (`step_run.cpp:233`) | no SushiProfiler path for a per-period fact (G2) |

Alternatives that do not fit: `record_zone_at` writes a zone on the calling thread's ring
(`zones/README.md`, `records.hpp`), so it has no device track and no attributes; reading the plan's
ledger from the source would reach through `CompiledGraph` into engine internals.

What the source holds: a bounded queue of `(period, node table, records, pairing)` filled on the
thread that finishes a run and drained in `collect()` on the consumer thread (`sources/README.md`).
A queued run for a period the session never closes must not accumulate: `end()` drops it and the
queue has a cap, counted. Records are copied (40 bytes each), not moved out of the report, since the
caller still owns the report.

### 1.4 The node table and the join

CLIENTS.md adds a node table to `RunReport`, one row per registered node with name and `OperationID`,
indexed by `node_index`. The code supports that: the registration index is assigned once per public
`add()` call (`node.hpp:183`, `task_graph.cpp:174`), and `capture_profiling_state()` already sizes
per-index arrays from it (`compiled_graph.cpp:69-87`).

- Several internal nodes can share one index (`profiling_multiplicities`, `compiled_graph.cpp:81`), so
  `(node, invocation)` can repeat inside a run (`report/README.md`). The span table keeps one row per
  record, so nothing collapses; the pair is just not a key.
- The row needs: index, name, operation, whether the node is host-native (`compiled_graph.cpp:82-84`
  already computes it), multiplicity, and the device. The device is not known at compile time for a node
  that names no region; it is set at first dispatch (`task_scheduler.cpp:670-686`), so the row's device is
  filled at collect time, not at plan time.
- `Node::metadata.name` is a `const char*` (`task_types.hpp:66`) owned by the recorder
  (`recorder.hpp:914`). A table built once per plan copies the strings once; a table built per run copies
  them every run, as `NodeTiming` does now (D4).
- SushiAI's join is `NodeEventRecord::node_index` against its lowering's `(first, count)` registration
  ranges (CLIENTS.md, SushiAI section). Whether the index counts every task SushiAI's lowering emits is
  SushiProfiler's open question (README, Open questions); the runtime's part is that the index is what
  `add()` returned (`graph.hpp:148`, `Graph::rule_registration`).
- `PlanView` (`plan_cache.cpp:161-187`) already lists name and operation per recorded node, but by rule
  position and not by registration index; `rule_registration()` maps one to the other. It is a copy made
  on request, not a table a run carries.

### 1.5 The runtime's own zones

Proposed sites, group `Runtime`. Level 1 is a stage, 2 a fine zone, 3 per element, as the profiler
defines them (`CAPTURE_AND_SESSION.md`, "The hot path"). A zone belongs in an exported `.cpp`, not in
a header template: `StepDriver` and `Recorder` are templates a consumer compiles (`step_driver.hpp:502-520`
runs `build_plan` inline), so a zone there would be compiled at the consumer's level with a site per
translation unit.

| Zone | Site | Level |
| --- | --- | --- |
| `plan.build` | `PlanCache::build`, `plan_cache.cpp:100-147` | 1 |
| `plan.compile` | `TaskGraph::compile`, `task_graph.cpp:472-597` | 1 |
| `plan.prepare`, `plan.classify` | `plan_cache.cpp:137`, `:140` | 2 |
| `region.commit` | `region_composer.cpp:379-391` | 1 |
| `run.steps` | `Detail::run_steps`, `step_run.cpp:185-243` | 1 |
| `run.submit` | `SchedulerExecutionEngine::submit`, `scheduler_execution_engine.cpp:497-566` | 1 |
| `run.wait`, with `run.wait.latch`, `run.wait.quiesce`, `run.wait.errors` | `complete()`, `:575-604` (`:579`, `:582`, `:588`) | 1; children 2 |
| `chain.submit_step`, `chain.wait`, `chain.record_events` | `chain_run.cpp:143`, `:169`, `:277` | 1, 1, 2 |
| `dispatch.task`, `dispatch.submit`, `dispatch.host` | `execute_task` `task_scheduler.cpp:571`; `queue.submit` `:727`; `execute_native` `:635` | 2 |
| `wait.runtime`, `wait.queues` | `Runtime::wait` `runtime.cpp:218-221`; `DeviceQueueSet::wait_all` `device_queue_set.cpp:269` | 1, 2 |
| `memory.allocate`, `memory.release` and a value of the bytes | `usm_allocator.cpp:101`, `:116` | 2 |
| values `graph.nodes`, `graph.roots` at compile | `task_graph.cpp:472` onward | 1 |
| counters `transfer.h2d_bytes`, `transfer.d2h_bytes`, steals, tasks | the run-end block `step_run.cpp:229-237`, `engine.metrics()` | 1 |

Dependency resolution is not a per-node zone: `DependencyTracker::resolve` is lock-free and walks the
access history of a shard (`dependency_tracker.cpp:64-118`) once per region of every node, so a zone
there costs more than the walk on a short history. It is covered by `plan.compile`, and the edge count
can be a counter at the end of compile.

The scheduler's four time buckets (`busy`, `stealing`, `polling`, `idle`, `task_scheduler.cpp:429-559`)
stay buckets: an idle worker spins up to 100 times and then sleeps 100 µs in a loop
(`task_scheduler.cpp:552-559`), so a zone per phase would write events at the spin rate. They are read
at the end of a run and can be emitted as values by the thread that ends the run (D9).

### 1.6 Lock waits

`ContendedLock` and `SP_LOCK_WAIT` take `try_lock` first and open a zone only when it fails
(`lock_wait/README.md`). The contended locks of the run path are all `pending_mutex`
(`task_scheduler.hpp:165`): `arm_for_execution` (`task_scheduler.cpp:267`), `disarm` (`:285`), `stop`
(`:297`, `:316`), `drain_pending_submissions` (`:383`), the priority pop (`:497`), `submit` (`:869`),
`submit_batch` (`:896`) and `migrate_tasks` (`:991`). The worker park at `:451` waits on a
`condition_variable` with a `std::unique_lock<std::mutex>` and is idle by design; instrumenting it needs
`condition_variable_any` and is not proposed. Other locks:
`RunOutcome::error_mutex_` (failure path only, `run_outcome.hpp:62`, `:73`), `CompletionLatch::mutex`
(one waiter, `scheduler_execution_engine.cpp:64`), `QuiescenceLatch::mutex_`
(`quiescence_latch.hpp:70`, `:79`) and the distributed locks, which are out of this analysis's depth (6).
Only `pending_mutex` is proposed: it is the one many threads contend on.

### 1.7 Which period a span belongs to

`PeriodTimeline::current()` is one relaxed load and returns `K_NO_PERIOD` before the first mark
(`period_timeline.hpp:51-70`). The runtime has no handle to the followed timeline: the source factory
receives only its JSON settings (`registry.hpp`, `SourceFactory`), `Source::begin` receives a
`SourceContext` with no timeline (`source_context.hpp`), and `Session::timeline(name)` is a method of
the session. SushiProfiler gap G4.

Where to read it. A blocking run ends in `run_steps` (`step_driver.hpp:649-676`, `step_run.cpp:185-243`);
an overlapped one is split into `submit_step` (`step_run.cpp:245-271`) and `finish_step`
(`:273-302`), and the caller may wait long after the period moved on. The period must be read when the
work is submitted, so an async step stores it in `AsyncRunState` at `:269` and not at `finish_step`. A
step that finishes after its period closed has no way in: the consumer closes a period once a drain is
`linger_ms` past its end (`consumer/README.md`, step 5; default 100 ms in the configuration example) and
calls `collect` once (SushiProfiler gap G10).

### 1.8 The environment contributor

`EnvironmentContributor::contribute(RecordFacts&)` sets named facts when the session starts
(`environment_contributor.hpp`); a contributor is held by reference and must outlive every session
(`registry.hpp`). What the runtime can state, and from where:

| Fact | From |
| --- | --- |
| `sushiruntime.version` | `core/version.hpp` |
| `sushiruntime.node`, `.os`, `.labels` | `Runtime::topology()` (`runtime.cpp:181-184`, `node_facts.hpp`) |
| per device: name, kind, memory, compute units, NUMA node, backend | `AdvancedControls::devices()` (`advanced_controls.hpp`), `DeviceDescription` (`device_description.hpp:39-47`) |
| per device: driver version | not held; `sycl::info::device::driver_version` through `AdvancedControls::queue(i).get_device()` (unverified that the toolchain answers it on every backend) |
| SYCL toolchain | compile-time macros of the runtime library; unverified which |
| `sushiruntime.profiling`, `.rebalancer`, worker count, pinning | `AdvancedControls::is_profiling_enabled`, `is_rebalancer_enabled`, `worker_pool()` |
| `build` facts: `sushiruntime.build_type`, optimisation, sanitiser | compile definitions of the runtime library; the contributor must be compiled inside the runtime library, since `compare_records` refuses records whose `build` facts differ (`environment/README.md`) |

`DeviceDescription` carries `sycl::backend` but no driver (`device_description.hpp:39-47`), and the
device line the runtime logs at start omits both (`device_queue_set.cpp:171-173`). The same line is the
one place an operator reads the backend and driver; item L-M3 in 2 adds them there.

### 1.9 Overhead, and how the compiled level keeps it off the hot path

Numbers below are the profiler's budgets (README, "Performance budget"), not measurements of this
runtime. PF-16 measures.

| Case | Cost, from the profiler budget | Where in the runtime |
| --- | --- | --- |
| `SR_PROFILE_LEVEL` 0, or `SR_ENABLE_PROFILER` off | no instruction; the sources are not built, the library builds without SushiProfiler present (CLIENTS.md) | everywhere |
| Level 1, gate closed | one relaxed load and a branch, under 2 ns, per stage zone: about five per run | `run.steps`, `run.submit`, `run.wait`, plan build |
| Level 2, gate closed | the same, two or three per node dispatch (`dispatch.task`, `dispatch.submit`) | `task_scheduler.cpp:571`, `:727`; small against a SYCL submit, which is a driver call; to be measured |
| Level 2, recording | under 25 ns a zone | same |
| Level 3 | per spin or per steal; not compiled in release | `event_polling.hpp:270`, `task_scheduler.cpp:804` |

The larger cost is not the zones. Turning device timing on changes the queue, adds three
`get_profiling_info` driver calls per dispatch (`event_timestamps.hpp:60-72`), a `snapshot_widths` per
dispatch (`task_scheduler.cpp:751-754`), two `steady_clock` reads per host-native node
(`:634-642`) and a ledger slot (`profiling_ledger.hpp:158`). That cost is why the profiler's design reads two
runs. The level macro cannot remove it; only a plan built without `plan.profiling` does.

`SR_ENABLE_LOGGING` is a header macro with no CMake option (`log_gate.hpp:37-45`); the profiler's
`SR_PROFILE_LEVEL` and `SR_ENABLE_PROFILER` are proposed as CMake options (CLIENTS.md names them),
so each library chooses its density (`ENGINE_CHANGES.md`).

### 1.10 A process boundary the design does not yet cross

`sushiruntime` is a shared library (`SushiRuntimeLibrary.cmake:23`) and SushiProfiler v0.1.0 is built as
a static library (`SushiProfilerLibrary.cmake:19`, "static, the orchestrator's proposal" in its README).
The collector's gate is a static member (`collector.hpp`, `gate_`) and `Collector::instance()` builds a
singleton. If the runtime library links SushiProfiler statically and a host links it too, the library's
zones go to the library's collector and the host's session drains the host's. The host sees no runtime
zone. SushiLog avoids this with a shared library and a hidden-symbol static one (`README.md` of
SushiLog). SushiProfiler gap G1 and decision D2; nothing in the profiling tasks can be accepted before
it is settled.

## 2. Logging

Method: every `catch`, every discarded `Result` or return value (`static_cast<void>`, `(void)`), every
fallback, every place that creates a failure status (`make_error`, `Error{ErrorCode...}`) and every module
with no category was listed, then each read. The runtime has 32 source files with `SR_LOG_*` calls
(192 call sites). 80 `catch` blocks exist; the `distributed` ones were scanned for a log call in the same block, and
all but the entries below either log or hand the exception to a status that is logged at one boundary
(`dispatch_settler.cpp`, `in_process_transport.cpp:log_handler_failure`). `surface/api` has 97 failure
sites and no `SR_LOG_*` call and no category. The `distributed` layer (11 970 lines) was scanned, not
read in full (6).

Categories that exist: `execution`, `graph`, `scheduler`, `distributed`, `offload_coordination`,
`device`, `memory`, `topology`, `memory_exchange`. Proposed new ones (D13): `api`,
`process_environment`, `network_socket`. The drops of E4 and E5 are logged where the run is collected, under
`execution`, so `observability` stays a data holder with no category.

Levels follow `logging`: WARN is a recovery or fallback, ERROR a failed operation whose status the
caller sees, DEBUG state at a decision point. "Count" means the code keeps a counter and writes one
line when the run, the construction or the interval ends, never one per element.

### 2.1 Places that fail silently, swallow, fall back unrecorded or drop a status

| # | Place | What happens now | What to log | Level, category |
| --- | --- | --- | --- | --- |
| A1 | `recorder.hpp:976-983` (`admit`), `recorder.cpp:62-63` | A refused recording returns `rule_count()` and records nothing; the first refusal is kept for the next run and every later one is dropped | the call name, error code and message, "nothing recorded; the next run of this graph returns the first refusal"; later refusals too, naming that they are not kept | WARN, `api` |
| A2 | `run_handle.hpp:225-231` (`settle`) | A step nobody waited for is finished and its `Result` is discarded | when it failed: the error code and message and "the outcome of an unwaited step is dropped" | WARN, `api` |
| A3 | `step_driver.hpp:316-325` (`end_pending_step`, the graph's destructor) | `dropped` is discarded; a failed `complete_step` also skips `after_step` | the same line as A2 | WARN, `api` |
| A4 | `plan_cache.cpp:149-154`, `plan_cache.hpp:97-103` | A plan is rebuilt for four different reasons (first build, recorder generation, placement generation, profiling request) and nothing says which | reason, node count, compile milliseconds | DEBUG, `api` |
| A5 | `context_memory.hpp:124-126` | `usm_kind_of` returns `unknown` on a `sycl::exception` | address, device, `what()` | DEBUG, `api` |
| A6 | `runtime.cpp:198-215` and the other 97 failure sites of `surface/api` | A refusal reaches the caller as a status; nothing is logged where it is made | D14 decides; if yes, once, where the status is created | per D14, `api` |
| E1 | `scheduler_execution_engine.cpp:588-600` | When a task error exists, `raised` (a queue's asynchronous error) is dropped | both messages: "a device queue also reported: ...; the task failure is returned" | WARN, `execution` |
| E2 | `task_scheduler.cpp:485`, `:490` | The abort lines carry no error code, no message and no node | `error.message` and the code; the node name when the poller can give it | ERROR, `scheduler` |
| E3 | `event_polling.hpp:354-370` | After `K_STALL_DRIVE_THRESHOLD` idle polls the oldest event is waited on; a throw there replaces `first_failure` (`:363`), losing the earlier one, and sets `oldest.ready` anyway (`:367`) | count of stall drives per run at the end of the run; the replaced error when it is replaced | DEBUG count, WARN on replace, `scheduler` |
| E4 | `event_polling.hpp:399-413`, `chain_run.cpp:283-284`, `:292-293`, `profiling_ledger.hpp:183-190`, `task_scheduler.cpp:647-648` | A dispatch whose timestamps cannot be read, whose ledger row is full or which has no event is dropped; in a release build the row is capped without a word | per run: how many dispatches were left out, for which cause, and the node index of the first | WARN count, `execution` |
| E5 | `scheduler_execution_engine.cpp:379`, `:395` | A record whose `node_index` is past the tables is skipped | folded into E4's count | WARN count, `execution` |
| E6 | `task_scheduler.cpp:103-105` | The worker count is cut to `K_MAX_WORKER_THREADS`, or raised from 0 to 1, with no line (the pin range clamp at `:112-122` does log) | requested and applied counts and the maximum | WARN, `scheduler` |
| E7 | `task_scheduler.cpp:158-160` | A pinned worker whose core is outside `core_to_numa` is mapped to NUMA node 0 | the worker, the core | DEBUG, `scheduler` |
| E8 | `task_scheduler.cpp:695-696` | While profiling, a dispatch whose queue is not found in the device set takes the unprofiled queue and produces no timing | the device and node; counted per run | WARN count, `scheduler` |
| E9 | `task_scheduler.cpp:576-597` | Tasks of an aborting or cancelled run, and nodes whose late-bound control is off, are skipped with no record | at the end of the run: skipped for abort or cancel, skipped for disabled | INFO on cancel with the count, DEBUG for disabled, `scheduler` |
| E10 | `chain_plan.cpp:210-225`, `step_run.cpp:217-221` | A plan that cannot chain falls to the scheduler with no reason (native host work, a coordinator's node, a device value read, no USM region, two devices, unordered host work, `force_scheduler`); `README` of the module lists them | once per plan: the layer chosen and the first reason it was not the chain | DEBUG, `execution` |
| E11 | `task_graph.cpp:287`, `:290`, `:353`, `:356`, `:452`, `:455` | A region whose `base` is null creates no dependency | count of untracked regions per plan build | DEBUG count, `graph` |
| M1 | `usm_allocator.cpp:107-108` | A failed SYCL allocation returns null; the status the caller builds (`recorder.cpp:96-103`) is the only trace | kind, bytes, alignment, device | WARN, `memory` |
| M2 | `device_queue_set.cpp:224-232`, `:309` | A queue of another flavour is built on first use; a request for out-of-order on an in-order device is served in order | device, flavour, build milliseconds; the substitution | DEBUG, `device` |
| M3 | `device_queue_set.cpp:171-173` | The device line has no backend and no driver version | add both | INFO, `device` |
| P1 | `stop_signal.cpp:83`, `:90` | `std::signal` returning `SIG_ERR` is stored and not examined; no handler is installed and the worker will not stop on Ctrl-C | the signal and `errno` | WARN once, `process_environment` |
| P2 | `socket_posix.cpp:369`, `:696`, `socket_windows.cpp:354`, `:706` | `TCP_NODELAY` failure is discarded; frames then wait for Nagle | peer, the OS error | WARN once per peer, `network_socket` |
| P3 | `cluster_monitor.cpp:232-233`, `:249-250` | The `SendStatus` of a PING and of the closing GOODBYE is discarded | peer and status | DEBUG, `distributed` |
| P4 | `hw_discovery.cpp:303-316` | A PCI address string that fails the regex is left unmapped with no line | the string | DEBUG, `topology` |
| F1 | `assert.cpp:41`, `:60-64`; 28 files use `SR_VERIFY` or `SR_FATAL` | A failed check writes one `fprintf` line to stderr and aborts; no `SR_LOG_FATAL` call exists in the library (grep) | the check text through SushiLog `FATAL`, so a host's sink sees it | FATAL, category per D16 |

Over-logging met in the same files, for the same reader: a `DEBUG` line per node added
(`task_graph.cpp:244`, `:296`, `:362`, `:461`), per dispatch (`task_scheduler.cpp:617`) and per replayed
step (`scheduler_execution_engine.cpp:602`), and an `INFO` per plan compile (`task_graph.cpp:597`).
`logging` forbids per-element lines; `DEBUG` stays compiled in under `NDEBUG` (`log_gate.hpp`), so each is
a load and a branch on the hot path. They move to `TRACE`, which `NDEBUG` removes.

### 2.2 Where a log line and the profiler overlap

A log line says a thing happened; the profiler says how long it took. They do not duplicate: E4 and E8
leave holes in a record, so the log line is how a reader learns a record is incomplete. A rebuild (A4) is
a log line and also a `plan.build` zone. Counters that feed a line at the end of a run are the same
numbers the profiler can take as values (`step_run.cpp:229-237`).

## 3. What SushiLog lacks

Read: `log_record.hpp`, `log_calls.hpp`, `log_front.hpp`, `log_sink.hpp`, `modules/foundation/log/README.md`,
`modules/core/text/README.md`, `docs/guides/LOGGING_GUIDE.md`.

| # | Gap | Evidence in the runtime |
| --- | --- | --- |
| L1 | **Log once per key.** SushiLog has no suppression | `RulePlacement::is_first_warning` keeps a mutexed vector of `(rule, cause)` pairs to warn once (`rule_placement.cpp:172-180`, `rule_placement.hpp:127-130`, with `forget_warnings()` at `:166`); E8, P1 and P2 need the same |
| L2 | **Log every Nth, or at most once per interval** | `is_logged_attempt` logs attempt 1 and every 10th (`tcp_transport.cpp:70-75`, used at `:419`, `:521`, `:627`); E3 and E4 need an interval |
| L3 | **A counter that reports itself.** `logging` says "a counter, reported once per interval" for anything per element; SushiLog has none | E3, E4, E5, E8, E9, E11 each keep a count the library has nowhere to report; `profiling_ledger.hpp:183` asserts in debug and drops in release |
| L4 | **Structured fields on a record.** `LogRecord` holds level, project, module, file, line, thread, time and one message string (`log_record.hpp`); a sink cannot filter by node, device, peer, run or period | E2 cannot say which run or node; the profiler's period index would let a log line be placed inside a profiled step; every distributed line carries the peer inside text |
| L5 | **A steady or TSC timestamp.** `time_ns` is the system clock since the Unix epoch (`log_record.hpp`) | A WARN cannot be placed on a profile's timeline, whose record clock counts from the session's first calibration point on the steady clock (`source_context.hpp`, `to_record_ns`) |
| L6 | **A thread name.** A record carries `[t3]`, a number SushiLog assigns on first use | The scheduler workers (`task_scheduler.cpp:217`, `:409`), the rebalancer and the cluster threads read as `t5`; SushiProfiler names its threads (`Collector::set_thread_name`) and the two numbers do not match |
| L7 | **A capture sink for tests.** Every test that reads a log builds its own | `SeenRecord` and `collect_into` (`tests/foundation/log/test_logger.cpp:58-91`), `ErrorLineCapture` (`tests/platform/memory/test_usm_allocator.cpp:176-197`), and ad hoc lambdas at `tests/extension/distributed/test_distributed.cpp:1883`, `:4073` and `tests/extension/offload_coordination/test_offload_dispatcher.cpp:370`, `:589`, `:648` |

Not a SushiLog gap, though it looked like one: a log call from a library below its front exists as
`SushiLog::log_at` (`log_calls.hpp`), so item F1 needs a runtime decision (D16), not a SushiLog change.

## 4. What SushiProfiler v0.1.0 lacks for SushiRuntime

Read: the READMEs of `zones`, `periods`, `sources`, `environment`, `config`, `session`, `registry`,
`consumer`, `lock_wait`, the headers `source.hpp`, `source_context.hpp`, `span_sink.hpp`,
`span_table.hpp`, `registry.hpp`, `collector.hpp`, `period_timeline.hpp`, `environment_contributor.hpp`,
`chrome_trace_writer.cpp`, `device_idle_analysis.cpp`, `docs/reference/RECORD_FORMAT.md`.

| # | Gap | Where the runtime needs it |
| --- | --- | --- |
| G1 | **Static library only, one collector per binary image.** `SushiProfilerLibrary.cmake:19` builds static; the gate and the collector are statics (`collector.hpp`) | The runtime is a shared library (`SushiRuntimeLibrary.cmake:23`); 1.10 |
| G2 | **No way for a source to state a per-period fact.** `PeriodRecord::facts` exists (`period_record.hpp:97`, read and written by `record_json`) but no session or consumer code sets it, and `Source` and `SpanSink` have no call for it | `RunReport::layer` for the period (`step_run.cpp:233`); CLIENTS.md says the source "reports the run's execution layer for the period" |
| G3 | **A source cannot intern a name or convert a time after `begin()`.** `SpanSink::append` refuses an unknown `NameID` (`span_sink.hpp`); `intern`, `add_track` and `to_record_ns` are on `SourceContext`, which only `begin` receives and whose lifetime is not promised | Node names, operations as text and attribute values are known only when a graph compiles, long after the session started (1.2, 1.4) |
| G4 | **A source cannot reach the followed timeline.** The factory gets JSON settings, `begin` gets a `SourceContext` with no timeline, and `PeriodTimeline` has no lookup by name | `PeriodTimeline::current()` at the point a run is handed over (1.7) |
| G5 | **A source cannot learn, before a period opens, whether it will be recorded.** `PeriodTimeline::observe` holds one observer and the window takes it (`period_timeline.hpp`, `period_observer.hpp`); `Collector::recording()` answers only for the period already open | The runtime must switch plan profiling on before the first recorded period, since it costs a plan rebuild (1.2, item 9; D7) |
| G6 | **Spans that arrive after their period closed are lost.** A period closes at `linger_ms` after its end and `collect` runs once (`consumer/README.md`, step 5); ENGINE_CHANGES.md says "nothing is dropped for arriving late" for spans | `run_async` / `RunHandle::wait` (`step_run.cpp:245-302`) finishes a step when the caller waits; SushiEngine overlaps frames this way |
| G7 | **Overlapping spans on one track.** `chrome_trace_writer.cpp:284-285` writes every span as a complete event on `(device process, tid = track)`; `device_idle_analysis.cpp:74-98` merges overlaps for busy time. I did not render a trace, so the Chrome display of partial overlap is unverified | An out-of-order profiling queue runs kernels concurrently (`task_scheduler.cpp:224-229`); host-native nodes run on several workers at once |
| G8 | **No documented `register_profiler_parts` pattern.** ENGINE_CHANGES.md promises "each library exports one `register_profiler_parts(Registry&)`" (line 14); no header, name or example exists in v0.1.0 | The runtime's public registration function (D12) |
| G9 | **`SpanSink` takes no counters or values.** A source adds spans only | Per-run counters can be written with `SP_COUNTER` on the run's thread, inside the period; not a gap for the plan, listed so it is not looked for |

The idle drainer's budget is the profiler's own open item (its RW-5) and does not block this work.

## 5. Decisions for the owner

Each is a question with the options found. None is chosen here. D1 to D12 and D23 bear on profiling, D13 to
D19 on logging, D20 to D22 on the work itself.

**D1. Where does the runtime's profiling code live?** The runtime's zones (inside the shared library),
the device source, the node table and the contributor need homes in the tier order
(`SushiRuntimeTiers.cmake`).
- (a) A foundation module `profile`, header-only and internal, holding the `SR_PROFILE_LEVEL` ceiling and the
  `SR_ZONE` family as `log` does for `SR_LOG_*`; and a surface module `profiling` holding the source,
  the contributor and the registration function. Each module has one job; no module below `surface`
  includes SushiProfiler except through `profile`.
- (b) One module `profiling` in the platform tier next to `observability`, holding the macros and the
  source. Fewer modules; `observability` consumers then link the source's SushiProfiler dependency.
- (c) Zones only inside the library, and the source in a separate optional library `sushiruntime_profiling`
  outside the shared library, so the runtime itself never links the session tier. Zones then need a hook
  interface and lose the inline gate.

**D2. How does SushiProfiler cross the shared-library boundary?** (1.10, G1)
- (a) SushiProfiler ships a shared library and a hidden-symbol static one, as SushiLog does; the runtime and
  every host link the shared one.
- (b) SushiProfiler stays static; the runtime library carries a private collector and a host session sees
  only host zones unless the runtime exports a function that starts the session inside the library.
- (c) The runtime builds its zones against a function table a host installs (no static state in the library),
  at the price of an indirect call per record.

**D3. How is the device source fed?**
- (a) The runtime pushes: the context holds a `RunObserver` interface (declared in `report`, next to
  `RunReport`); `run_steps` and `finish_step` call it with the report, the node table and the period.
- (b) The host pulls: the host (SushiAI) hands every `RunReport` to `DeviceSource::add_run()` itself; the runtime
  does nothing and has no observer.
- (c) The source reads the plan's ledger directly; rejected on the encapsulation of `CompiledGraph`, listed because it
  needs no report copy.

**D4. Where does the node table live, and who owns it?**
- (a) A vector in `RunReport` copied every run (CLIENTS.md as written); costs a string per node per run.
- (b) A `std::shared_ptr<const NodeTable>` built once per plan, shared by every report of it.
- (c) Not in the report: `Graph::node_table()` returns it and the host passes it to the source with the report.

**D5. What does `NodeEventRecord` gain?** It is 40 bytes and public (`run_report.hpp:112-121`); a new field
raises the minor version (the SOVERSION carries it before 1.0, `SushiRuntimeLibrary.cmake:15-20`).
- (a) `device_index` and `submit_thread` (to 48 bytes); both known where the record is made.
- (b) Only `submit_thread`; the device is a column of the node table filled at collect time (1.4).
- (c) Neither: tracks come from the node table, and spans get `submit_thread` 0 (the profiler then cannot place a span
  under the host zone that launched it, and `device_idle` attributes gaps to `launch` or `host` only).

**D6. One clock pairing per run, or one per device?** (1.2, item 7)
- (a) Keep one; document that the devices of one run share a timeline. Needs a hardware check of the owner's
  CPU and GPU devices first.
- (b) `RunReport::clock` becomes a list, one pairing per device, each claimed by that device's first kernel.
- (c) Keep one and make a run touching two devices refuse timing with a logged WARN.

**D7. How does device timing follow the capture window?** (1.2, item 9; G5)
- (a) The host calls `Graph::set_profiling(true)` before the first warm-up and `false` after the last active
  period; two plan rebuilds per capture.
- (b) The runtime keeps timing on from session start to stop; the window only decides what the record keeps. The
  profiled queue and its cost stay for the whole session.
- (c) Make the flag switchable without a rebuild: the sentinel reads it per run (`scheduler_execution_engine.cpp:530-549`).
  An engine change, also useful without the profiler.

**D8. What are the compiled levels?** `SR_PROFILE_LEVEL` per build type, and the level of the per-dispatch
zones.
- (a) Release 1, debug 2; `dispatch.*` zones at level 2.
- (b) Release 0 unless the option is set; debug 2.
- (c) Release 1; `dispatch.*` at level 3, so no per-node zone is compiled in release.

**D9. What happens to the four worker time buckets and `RunReport::worker_timings`?** (1.5)
- (a) Keep them, and emit their per-run sums as profiler values at the end of the run.
- (b) Remove them and `worker_timings` (a public member) once zones and lanes cover them; needs the deprecation path
  of `api-stability`.
- (c) Keep both for one release.

**D10. How is an operation named in a span?** `OperationID` is a hash only (1.2).
- (a) The `operation` attribute is the hash as an integer; the node name carries the human text.
- (b) The runtime gains an operation-name table that libraries fill (`"gemm"_op` registers `"gemm"`); a new public interface.
- (c) The libraries put the operation name in the node's `name`; no runtime change.

**D11. One source for several runtimes in a process?** A host may hold more than one `Runtime`
(layered execution runs a replica's compiled step on a runtime of its own, `LAYERED_EXECUTION/README.md`, "What a
training run over several machines needs"), and the registry refuses a source name twice (`registry.hpp`, `add_source`).
- (a) One `sushiruntime.device` source fed by every runtime that registers with it; tracks are named by runtime and device.
- (b) One source per runtime, named `sushiruntime.device` plus the node name of `NodeFacts`.
- (c) One runtime per session: documented, not enforced.

**D12. What is the public registration interface?** (G8) A host must register the source and the contributor, and
the contributor must outlive the session (`registry.hpp`).
- (a) A free function `register_profiler_parts(Registry&, const Runtime&)` returning an owning handle the host keeps.
- (b) A member `Runtime::profiler_parts()` returning the same handle.
- (c) A host-side adapter in SushiAI and the other consumers; the runtime exports only the source class.

**D13. Which modules get a log category?** A module logs only under its own (`logging`).
- (a) `api`, `process_environment`, `network_socket`: the three this analysis needs.
- (b) Also `task`, `dependency_tracking`, `link_security`, `buffer_exchange`, `sycl`: every module with code that can fail.
- (c) `api` only, and the other places log under their caller's module.

**D14. Is a failure status created at the public boundary logged?** (A6) `logging` lists "every failure status created at a
boundary" under what is logged, and also says a failure is logged once.
- (a) ERROR at each of the 97 `surface/api` sites, through one helper that takes the `api` category.
- (b) WARN for a refused argument or state, ERROR for a failed device operation.
- (c) None: the caller holds the status; only A1 to A3, where the status is dropped, are logged.

**D15. How is the deferred refusal of `Recorder` handled?** (A1) A refused recording returns `rule_count()` and
the error surfaces at the next run (`recorder.cpp:62-63`).
- (a) Log it where it is made (A1) and keep the behaviour.
- (b) Return a `Result` from every recording call; a public API change in the surface RW-6 already reviews.
- (c) Both, in two releases.

**D16. Does a failed `SR_VERIFY` or `SR_FATAL` go through SushiLog?** (F1) `foundation/core` has no log dependency.
- (a) `core` depends on SushiLog and calls `SushiLog::log_at` at `FATAL` under a `core` category; edits
  `SushiRuntimeTiers.cmake` and `core`'s CMake.
- (b) `Runtime::create` installs an assertion handler (`set_assertion_handler`, `assert.hpp`) that logs `FATAL` and then
  lets the default run; checks before `create` and in tests still write to stderr only.
- (c) Leave it: assertions are the process's last words and stay on stderr.

**D17. Remove `SR_LOGGER_SET_SYNC_MODE`?** It forwards to `SushiLog::set_log_synchronous` (`logger.hpp`, last lines), which the
`logging` skill's "Fronts" section forbids; the header is installed, so it is public, and `test_runner` uses it once (`test_main.cpp:56`). Options: remove now, deprecate for one
release, or keep and note it as a known deviation. (`api-stability` decides the path.)

**D18. Who goes first, SushiLog's additions or the runtime's logging?** (section 3)
- (a) SushiLog adds L1 to L3 and L7 first; the runtime logging waits and uses them.
- (b) The runtime logs now with local counters and the two hand-rolled throttles, and moves to SushiLog's when it ships.
- (c) Split: the runtime logs what needs no throttle now (A1 to A5, E1, E2, E6, E7, M1 to M3, P1, P3, P4) and waits for the rest.

**D19. Does this work cover the distributed layer?** CLIENTS.md lists nothing there; the layer's timing is
unmeasured (`LAYERED_EXECUTION/README.md`, "Several machines", RW-21, RW-22).
- (a) No: P3 stays on the local run path; a later phase adds zones and flows for dispatch and settle.
- (b) Yes: zones and `SP_FLOW` around dispatch, transport and settle, so R13's measurement can read a record.

**D20. Do the build-file edits run in one sitting?** The pin, the option, the tier table, the package file and the CI clone
touch files that the SYCL compile-time work also edits (`SushiRuntimeModule.cmake`, `SushiRuntimeSyclTarget.cmake`,
`docs/agent/2026_10_26_SYCL_COMPILE_TIME/PLAN.md`). Options: after that work's owner build closes; or interleaved with a
rebase of one onto the other.

**D21. Bump the SushiLog pin to v0.2.3 with this work?** (`cli/sushistack.deps.toml:71-73` pins v0.2.2; v0.2.3 is tagged.)
Not read: what v0.2.3 changes.

**D22. Open the design topic `PROFILING`?** The repository's rule is one document per topic (`docs/design/README.md`);
profiling has none, so the work folder cannot carry `Implements`. Options: open `docs/design/PROFILING/README.md` with phases that
this plan's tasks implement; or extend `LAYERED_EXECUTION`, which is about placement and does not fit; or run this as work with no design line.

**D23. Which SushiProfiler tag does the runtime pin?** The task statement pins v0.1.0 as `[sushisystems.sushiprofiler]`. Gaps G1 to G4
and G6 of section 4 sit in that library and block the device source and the zones.
- (a) The tag waits until SushiProfiler closes G1 to G4 and G6; the runtime pins that tag.
- (b) v0.1.0 is tagged as it is; the runtime pins it for the front and the build wiring (PF-0, PF-1) and moves to a later tag
  for the source (PF-6) and the zones that need a shared collector.
- (c) The runtime works around G3 by keeping the `SourceContext` pointer `begin()` receives, which the interface does not promise. Listed
  because it needs no change in SushiProfiler; it relies on an unwritten contract.

## 6. What this analysis did not cover

- `extension/distributed` (11 970 lines), `offload_coordination`, `network_socket`, `wire`, `frame_transport`,
  `link_security`: scanned by the `catch` and failure-site scripts and sampled, not read in full. Their silent
  places beyond P2 and P3 are not listed.
- `surface/api` templates under `include/.../detail/emit_*.hpp`, `interop`, `dynamic_graph.hpp` and `region_composer.cpp`
  (753 lines): grepped for failures and refusals, not read for fallbacks.
- `work_stealing`, `region_boundary`, `probe_tasks`, `topology` internals other than `affinity.cpp`, `hw_discovery.cpp:259-340`.
- The graph replay path (`docs/design/GRAPH_REPLAY/README.md`): SushiProfiler's open question on whether a replayed
  period can be timed as one span depends on it; the code was not read.
- SushiBLAS and SushiAI sides of the join.
- Anything measured: no build, no run, no benchmark, no trace was rendered. Costs in 1.9 are the profiler's
  budgets and arithmetic on them.

## 7. SOLID breaches and dependencies seen

| Where | Breach | Cost |
| --- | --- | --- |
| `event_polling.hpp:393-428`, `chain_run.cpp:277-306`, `task_scheduler.cpp:630-660` | One record, three writers with their own drop rules | A new field is three edits; the drop rules already differ (E4) |
| `scheduler_execution_engine.cpp:367-443` (`collect_profiling`) | One function copies records, converts clocks, aggregates `NodeTiming` and builds worker rows | The node table and the source each need a piece of it |
| `task_scheduler.cpp` (1 023 lines) | Injector queues, steal topology and time buckets beside the dispatch loop | Already RW-46; the zones of 1.5 land in it |
| `step_driver.hpp` (714 lines), `recorder.hpp` | Build, run and refusal logic in header templates a consumer compiles | A zone or a log call cannot sit there (1.5) |
| `foundation/log/.../logger.hpp`, `SR_LOGGER_SET_SYNC_MODE` | A front forwards to SushiLog | D17 |
| `foundation/core/source/assert.cpp:41` | A second path writes to stderr beside SushiLog | D16 |
| `RulePlacement::is_first_warning`, `tcp_transport.cpp:70-75` | The project keeps its own log throttles | L1, L2 |
| `platform/observability` (module flag `SYCL`) | `profiling_ledger.hpp` and `transfer_counters.hpp` need no SYCL; only `event_timestamps.hpp` does | Includers pay for the module's SYCL flag; small, noted |

Unneeded dependencies: none among the modules. A script compared each module's `DEPENDS` and `PRIVATE_DEPENDS` with the
`SushiRuntime/<module>/` includes of its own files (44 modules) and found no declared-and-unused and no used-and-undeclared
edge. External dependencies (hwloc, SushiLog) were not re-audited.

## 8. Decisions

The owner delegated every decision of section 5 to the orchestrator on 2026-10-26: the SOLID
choice that holds as the stack grows, with SushiAI's inference profile as the goal to reach soonest.

| # | Decision | Why |
| --- | --- | --- |
| D1 | (a) A foundation module `profile`, the only includer of SushiProfiler below `surface`, holding `SR_PROFILE_LEVEL` and the `SR_ZONE` family; a surface module `profiling` holding the device source, the contributor and the registration function | One job per module, shaped as `log` is |
| D2 | (a) SushiProfiler v0.2.0 ships a shared library; the runtime and every host link it | One collector per process; SushiProfiler's `docs/agent/2026_10_26_CLIENT_CONTRACT/SPEC.md` |
| D3 | (a) A `RunObserver` interface declared in `report`; a runtime fans a run out to any number of observers, added with `Runtime::add_run_observer`; the runtime hands it a `RunObservation` (layer, node table, records, per-device pairings, drop counts) on every run while it is attached, when the run's device work completes; no period is read | The runtime depends on its own abstraction, never on SushiProfiler or on a host remembering to pass reports |
| D4 | (b) A `std::shared_ptr<const NodeTable>` built once per plan, shared by every report of it | Strings copied once per plan, not per run |
| D5 | (a) `device_index` and `submit_thread`, 48 bytes | Both are known where the record is made; without them a span has no track and no launching zone |
| D6 | (b) One pairing per device, claimed by that device's first kernel | A CPU and a GPU device do not share an event clock; one pairing would misplace every span of one of them |
| D7 | (b) Device timing is on exactly while at least one observer is attached: adding the first is the request, and each run reads the answer once when it is armed (R20), so no plan is rebuilt; the capture window decides only what the record keeps | No plan rebuild per window or per observer and no SushiProfiler G5 |
| D8 | `SR_PROFILE_LEVEL` 2 in every build type; `dispatch.*` at level 2; spin and steal sites at 3 | The launch gap per dispatch is the question SushiAI asks; PF-13 measures the closed-gate cost and lowers the default to 1 if it misses the budget |
| D9 | (b) Remove `RunReport::worker_timings`, `node_timings` and `NodeTiming`, `scheduling_overhead_ms`, `total_duration_ms`, `graph_analysis_duration_ms` and the transfer byte fields; the buckets become profiler values and the stages zones (D24) | One profiling system: the owner asked that everything the profiler measures leave the runtime |
| D10 | (a) The hash as an integer attribute; the node name carries the text | No new public table until a reader needs the operation's text apart from the node's |
| D11 | (a) One `sushiruntime.device` source fed by every runtime; tracks named by runtime node and device | The registry holds one source per name; layered execution's several runtimes land on one timeline |
| D12 | (a) `register_profiler_parts(SushiProfiler::Registry&, const Runtime&)` returning an owning handle | The pattern SushiProfiler v0.2.0 fixes for every library |
| D13 | (a) `api`, `process_environment`, `network_socket` | The three the found places need |
| D14 | (c) Only a dropped status is logged (A1 to A3); a returned status is the caller's to log | A failure is logged once, by whoever holds it last |
| D15 | (a) Log the refusal where it is made; keep the behaviour | The public change belongs to RW-6's review |
| D16 | (b) `Runtime::create` installs an assertion handler that logs FATAL | No new tier edge under `core` |
| D17 | Deprecate `SR_LOGGER_SET_SYNC_MODE` for one release, then remove it | `api-stability` |
| D18 | (b) The runtime logs now, with local counters and its two hand-rolled throttles in place, shaped so SushiLog's log-once, rate limit and counter replace them when its RW-3 and RW-4 ship (LG-7) | The owner asked for all code before any build; SushiLog's backlog holds L1 to L7 |
| D19 | (a) The local run path only | The distributed layer's timing is its own phase |
| D20 | The build edits wait until the SYCL compile-time work's build closes | One build file edit at a time |
| D21 | Yes, with this work's first build edit | The runtime runs on the released log library |
| D22 | Open `docs/design/PROFILING/README.md` with this plan's phases | The work folder then carries `Implements` |
| D23 | (a) Pin SushiProfiler v0.2.0, which closes G1, G3, G4, G6 and G8; G2 is answered by a `layer` span attribute | No reliance on an unwritten contract |
| D24 | Timing leaves `RunReport` entirely. The runtime keeps only the capture nobody else can do: reading SYCL event timestamps, one record writer (PF-2), the per-device pairing, and the counters it alone sees. These reach the outside through `RunObserver` alone, and only while an observer that asks for timing is attached; `RunReport::node_events`, `RunReport::clock`, `RuntimeConfig::profiling` and `Graph::set_profiling` go. Device timing is on exactly while an observer is attached (D7). SushiBLAS's tests that read `node_events` install a test observer; SushiAI's trainer drops its `NodeTiming` totals for P5's zones | A second timing path in the report is a second profiler to keep true |

The plan's own questions: tasks run one sonnet worker for the repository; the orchestrator builds
after each stage; no task is written against a stub.

### Amendments after review

An independent review of the plan and the working tree produced these rulings of the orchestrator.
Where one contradicts a row above, the amendment holds.

| # | Amendment | Effect |
| --- | --- | --- |
| R1 | Timing leaves `RunReport` (D24) is its own task, TL-1; PF-11, PF-12 and DOC-1 follow D24 and the revised D9 | The plan names the removal as work, not as a side effect |
| R2 | No period is read anywhere. The source places each span by its converted start against the `PeriodSpan`, drops an older one with `drop_late` and keeps a later one | The per-device pairing (D5, D6) is load-bearing: a span with no pairing has no start in the steady clock, so it has no period |
| R3 | An observation is handed to the observer when the run's device work completes, where the poller or the chain's wait learns it, not at `finish_step` | A caller that waits late loses nothing; a run whose work itself finished after `linger_ms` is counted in the record's drops |
| R4 | Attachment is the request, and any number of observers may attach (SushiEngine needs observations without a session; the profiler parts need them for `sushiruntime.device`). `[[nodiscard]] RunObserverHandle Runtime::add_run_observer(std::shared_ptr<RunObserver>)` adds one; the handle is move-only and removes its observer when destroyed or reset. There is no `wants_device_timing()`. The list is one immutable object behind one atomic shared pointer, copied on every add and remove, so a run reads one consistent list without a lock; device timing is on while the list is not empty | No run sees an observer without the timing it asked for; adding and removing an observer cost no plan rebuild (R20) |
| R5 | `on_run` is called on every run while the observer is attached, on every attached observer | A run of a plan built before the attach still reports, with no records |
| R6 | The source learns the runtime at registration, `register_profiler_parts(Registry&, const Runtime&)`; track names come from one public function, `device_track_name(const Runtime&, std::uint32_t)`, which the source uses too. A node table row carries `std::optional<NodeCost>` | No library rebuilds the track string; absent cost omits `flops`, `bytes` and `dtype`, a stated zero writes zero |
| R7 | The worker bucket values are written on every run beside the run counters, not only while timing is on | A profile of an unobserved run still shows the buckets |
| R8 | The asynchronous path carries the execution layer in `AsyncRunState` from `submit_step` | `finish_step` fills `RunReport::layer` without reading the engine |
| R9 | `RunObservation` carries the run's drop counts; the source forwards them with `drop_late`, and the source queue's drop-oldest counts too | `drops.spans_dropped` of the record covers every dispatch the capture lost |
| R10 | One ledger function fills a whole record; the scheduler, the poller and the chain call it, so `submit_thread` is assigned in one place | A new field is one edit |
| R11 | `profile.hpp` pulls no `<mutex>` into its includers | The front stays light; a file that uses `SR_LOCK_WAIT` includes `<mutex>` itself |
| R12 | The parts object holds its own handle, so its destructor removes its observer and only its own | A parts object that dies before its runtime leaves no dangling attachment |
| R13 | The removals of D24 are breaking and made now, with no deprecation release (before 1.0, `api-stability` allows it); one changelog entry per removed member names its replacement | PLAN.md lists the migration step of each consumer |
| R14 | Tests are written against `RunObservation`; the end-to-end test checks `submit_thread`, the cost attributes and `layer` | The proof covers what the profiler reads |
| R15 | Graph label (additive). A span's node index is unique only within one plan, and one runtime runs several graphs, so `Graph::set_label(std::string)` (and `DynamicGraph::set_label`) names a graph; a changed label applies at the next plan build and forces none. `NodeTable` carries the `label`, copied once per plan, and the device source writes it as the name attribute `SushiProfiler::DeviceSpanKeys::K_GRAPH` when it is not empty, interned once per table | Spans of two graphs with equal node indices differ by `graph`; no header already sent out changes, since both are additions |
| R16 | Origin (additive). `NodeOptions::origin` (`std::string_view`, empty by default) is copied at record time as `name` is, carried through `NodeStamp`, `TaskMetadata`, `PlanNode` and `NodeTableRow::origin`, and written as `K_ORIGIN` when it is not empty, interned once per row per table | A caller says which of its operations a node belongs to; no new include in `node_options.hpp` |
| R17 | Graph identity (additive). A `RunObserver` is runtime-wide, so a per-graph consumer would see every graph's runs, and a label is not an identity. Every owner of plans (`Graph`, `DynamicGraph`, the plan cache and the region composer they derive from) reports `[[nodiscard]] std::uint64_t id() const noexcept`: taken at construction from one process-wide atomic counter that starts at 1, never reused, kept across a move (a moved-from owner reports 0). `RunObservation::graph_id` carries the id of the owner whose run it is, on the blocking and the asynchronous path, and `NodeTable::graph_id()` carries it for matching a table to its owner. The device source does not write it as an attribute, since the `graph` label covers the timeline | Two graphs on one runtime and one observer: each observation names its own graph |
| R18 | Source name (additive). The public header `<SushiRuntime/profiling/source_names.hpp>` holds `inline constexpr std::string_view K_DEVICE_SOURCE_NAME = "sushiruntime.device";` and includes only `<string_view>`; the device source and `register_profiler_parts` use it, and so does SushiEngine | One spelling of the source name; the constant leaves `profile_keys.hpp`, where it was not public |
| R19 | Configuration by a generated header. `SR_ENABLE_PROFILER` and `SR_PROFILE_LEVEL` are not compile definitions of every module target, since a level change would recompile the whole runtime. The build generates `<SushiRuntime/profile/profile_config.hpp>` from `modules/foundation/profile/profile_config.hpp.in` with `configure_file`; `profile.hpp` includes it and defines neither macro. SushiProfiler is a prebuilt package found with `find_package(SushiProfiler CONFIG REQUIRED)` under `SR_ENABLE_PROFILER`, not through `SUSHIPROFILER_DIR`. No SushiProfiler header reaches a header SushiBLAS or SushiAI include: `profile.hpp` is included from `.cpp` files and the profiling module only | A level change recompiles the files that use a zone and nothing else |
| R20 | Device timing is a per-run choice (supersedes the rebuild costs of R4 and D7). A plan carries everything it needs to time unconditionally and is built once: the `NodeTable`, and a ledger that a run sizes when it first times. Each run reads `wants_device_timing()` once when it is armed (the scheduler's `arm_for_execution`, the chain run, the asynchronous step, a region composer's plan) and is timed exactly when the answer was yes; `CompiledGraph::profiling` holds that armed value. Staleness and `rebuild_reason` no longer mention timing. The run that begins right after `add_run_observer` is timed and delivers full records. What differs in a timed run is the queue: a timed dispatch uses the device's profiling-enabled queue, built on first use, and the chain run takes the in-order profiling flavour. With no observer a run costs one load and a branch plus what the plan holds idle: the node table (one row per registration index, with its name string) and, once a run was timed, the ledger rows at their high-water size (48 bytes per recorded dispatch) | A profiler that starts and stops costs no rebuild of any graph, and no run hands on_run empty records because its plan missed a rebuild |
| R21 | The live session of SushiProfiler v0.3.0 (additive). (1) `DeviceSource::max_delay_ns()` reports 2 000 ms, the longest a run's device work is allowed to finish after the end of the period its first dispatch started in; the consumer then holds a period open that long, so such spans are no longer dropped as late. `drop_late` keeps counting what is genuinely late: a span that still arrives after the delay, a span with no pairing, the dispatches a run lost, and the source queue's overflow. (2) Device timing of a run is wanted while an observer is added and, for an observer added with a gate (the profiler parts add theirs with `Collector::sources_wanted`), while its gate is open; a paused session costs no device timing. The answer is still read once when the run is armed (R20). An observer whose gate is closed at delivery is not called | A paused session costs the runtime nothing, and a slow run lands in its period without a longer linger. `Runtime::add_run_observer` and every header sent out stay as they are; the gate belongs to the internal `observe_runs` |

Two requests of the consumers are in the same pass. A task body may hand over every event it submitted
(`note_dispatch_event`), and the dispatch record then runs from the earliest start to the latest end and
starts at the earliest submit. `NodeCost` carries `flops`, `bytes` and a `dtype`, and the source writes them as
attributes when a cost is stated.

### The scope of this pass

The owner then asked for all of the code to be written before any build. This pass writes every
task of `PLAN.md`; the logging tasks that need a SushiLog feature (LG-7) use local counters and
throttles now, and move to SushiLog's when its RW-3 and RW-4 ship. Every build runs at the end.
