# API quality control: review report

**Status:** Phase 1 (review) finished on 2026-10-11. Nothing in the repository was changed outside this folder. Phase 2 (repair) has not started; see `PLAN.md`.

Branch `feature/usability_refactor`. The surface reviewed is the 148 headers under `modules/*/*/include/`, the sources that implement them, the module READMEs and `applications/examples/`. Ten reviewers read disjoint areas; every critical and major finding below was then read in the code by the chief before it was kept. Nothing was built or run, so no finding here is confirmed by a compiler or a test.

## Verdict

The surface the usability refactor produced is consistent at the top: `Runtime`, `AdvancedControls`, `RuntimeConfig`, the error and logging headers and the umbrella match SPEC section 2, the examples use no deprecated spelling, and the two transports keep one contract. The defects sit one layer down, where a contract is written in a comment and nothing enforces it. Two are critical: the region composer drops a cross-region edge when a node overlaps an intra-region predecessor on part of a buffer, which is a data race in documented use, and `pin_current_thread_to_core` shifts by an unchecked core id. Forty-three are major. Most of them are a lifetime rule the code cannot check (a `State*` stored by a rule, a device counter slot with no liveness observer, a graph destroyed under a pending step), a failure path that leaves state half-changed (`RegionComposer::commit`, a chained run, `Cluster::attach` on a shared transport), or a foundation type whose behaviour differs from its documentation (`Result`, `span`, `sushi_ptr`). Three majors are structural (`Node`, `TaskScheduler`, `IExecutionEngine`) and are proposed for the backlog, not for this repair. The API is usable as it stands by a caller who follows every README; it is not yet safe for one who does not.

## Counts

| Area | Critical | Major | Minor |
| --- | --- | --- | --- |
| (a) api runtime and data | 0 | 2 | 11 |
| (b1) api graph builder | 1 | 4 | 8 |
| (b2) api recorder, step driver, run handle | 0 | 5 | 8 |
| (c) api vocabulary, launch, detail | 0 | 3 | 10 |
| (d) engine | 0 | 6 | 12 |
| (e1) foundation | 0 | 5 | 12 |
| (e2) platform | 1 | 6 | 12 |
| (f) distributed | 0 | 4 | 7 |
| (g) interop | 0 | 4 | 8 |
| (h) cross-cutting | 0 | 4 | 5 |
| Total | 2 | 43 | 93 |

A minor row often groups several sites of one pattern, so the minor count is of rows, not of edits.

## Confirmed findings: critical

### QC-01 `modules/surface/api/include/SushiRuntime/api/graph/boundary_access.hpp:156-160`, `:178`
- Defect: `compute_boundary_access` removes a node from the entry pins of a base as soon as an intra-region predecessor overlaps it on any byte, so the bytes the predecessor does not touch get no cross-region edge. `rewire_base` wires incoming edges to entry pins only (`region_composer.hpp:396`).
- Scenario: region 1 holds `w`, which writes `b[15,20)`. Region 2 holds `p`, which writes `b[0,10)`, and `q`, which reads `b[5,20)`. `p` overlaps `q` on `[5,10)`, so `q` leaves the entry list. `w` does not overlap `p`, so no edge is made, and `q` reads `[15,20)` unordered against `w`.
- Fix: exclude a node from entry only when its predecessors' accesses contain every interval it reads and their writes contain every interval it writes; otherwise keep an entry pin for the uncovered intervals.
- Breaks callers: no.

### QC-02 `modules/platform/topology/include/SushiRuntime/topology/affinity.hpp:57`
- Defect: `DWORD_PTR mask = (1ULL << core_id);` shifts by an unchecked `int`, while line 50 promises that "an out-of-range value is logged and ignored".
- Scenario: `pin_current_thread_to_core(100000)` or `(-1)` is an undefined shift. On x86 the count wraps, the thread is pinned to another core and no line is logged. A core above 63 cannot be named.
- Fix: range-check `core_id` in `pin_current_thread_to_core` and log the refusal. Processor groups (`SetThreadGroupAffinity`) and moving the body out of the header need a new source file and are left to a later task.
- Breaks callers: no.

## Confirmed findings: major

### (a) api runtime and data

**QC-03** `modules/surface/api/include/SushiRuntime/api/data/buffer.hpp:572`
- Defect: `share_with_offload()` registers the allocation whatever its residency.
- Scenario: `rt.buffer<float>(n, DeviceIndex{0}, Residency::Device).distributed()` registers device USM; `BufferRegistry::read` and `write` then `memcpy` it on the host (`modules/extension/distributed/source/buffer_registry.cpp:201`, `:221`). `State::distributed()` (`state.hpp:182`) has the same hole.
- Fix: `share_with_offload()` throws `invalid_access` when `!is_host_accessible()`.
- Breaks callers: only one that already shares a device-resident handle.

**QC-04** `modules/surface/api/include/SushiRuntime/api/data/buffer.hpp:222`
- Defect: "must be trivially copyable" is a `@tparam` sentence; no `static_assert` exists in the api headers.
- Scenario: `rt.buffer<std::string>(4)` compiles, assigns into unconstructed storage, and `read_window` and `write_window` `memcpy` over it.
- Fix: `static_assert(std::is_trivially_copyable<T>::value, ...)` in `Buffer<T>`.
- Breaks callers: only one already in undefined behaviour.

### (b1) api graph builder

**QC-05** `modules/surface/api/include/SushiRuntime/api/graph/dynamic_graph.hpp:153-160`
- Defect: `drop()` destroys the `Region` at once and states no precondition about a pending `run_async()` step, while `steps_` holds the region's `StepMachinery` by address (`step_machinery.hpp:221`).
- Scenario: `auto h = world.run_async(); world.drop(k); h.wait();` walks `aggregated_` into the freed recorder, and the region's scratch USM is freed under running kernels.
- Fix: `drop()` moves the region into a retired list that `rebuild_plan()` empties after `commit`, and refuses while a step is pending.
- Breaks callers: no.

**QC-06** `modules/surface/api/include/SushiRuntime/api/graph/region_composer.hpp:85-131`
- Defect: `commit()` removes cross edges and tears regions down before `build_region`, so a throw from a replay leaves the composer without the edges it removed.
- Scenario: A, B and C share base `b`; B is dropped and D added, and D's emitter throws in `recorder.replay`. The edges on `b` are gone and B's state erased. The retry finds no state for B, `b` never enters `dirty_bases`, and A and C run with no edge on `b`.
- Fix: build every dirty region into a local map first, then un-wire, tear down and install with steps that do not throw.
- Breaks callers: no.

**QC-07** `modules/surface/api/include/SushiRuntime/api/graph/plan_cache.hpp:69-73`
- Defect: `set_profiling` sets `dirty_ = true` when the value did not change; `DynamicGraph::request_profiling` returns early in that case (`dynamic_graph.hpp:248`).
- Scenario: `graph.set_profiling(true).run();` once per frame replays and compiles the graph every frame.
- Fix: return when `requested == profiling_requested_`.
- Breaks callers: no.

**QC-08** `modules/surface/api/include/SushiRuntime/api/graph/region_composer.hpp:497-514`, `:134`
- Defect: every commit walks every node of every region and allocates one `std::unordered_map` per region, while the `commit` brief (`:68`) says "from the changed regions and their shared boundaries only" and the README says the cost is independent of the untouched world.
- Scenario: one cell streams in per frame in a world of 10^5 regions: 10^5 map allocations and a full node walk per commit. `plan_.roots.assign` is O(roots) as well.
- Fix: keep a per-region index base and count in `RegionState` and renumber only rebuilt regions and those whose base moved; correct the two documents to what remains O(world).
- Breaks callers: no.

### (b2) api recorder, step driver, run handle

**QC-09** `modules/surface/api/include/SushiRuntime/api/graph/recorder.hpp:778`
- Defect: `take()` resets the moved-from recorder's `generation_` to zero, so a recorder that is moved from and refilled can reach the generation its owner's plan was built at.
- Scenario: `g.add(...); g.run();` then `Recorder stolen = std::move(g.recorder()); g.add(...); g.run();` gives generation 1 again; `PlanCache::stale` (`plan_cache.hpp:122`) says current and the old plan runs, over scratch the stolen recorder owns.
- Fix: `take()` leaves `other.generation_` advanced by one and never resets it.
- Breaks callers: no.

**QC-10** `modules/surface/api/include/SushiRuntime/api/graph/recorder.hpp:629-638`, `:710-713`
- Defect: `clear()` leaves `scope_open_` and `scope_has_node_` set, and `extend()` hands out the `Recorder&` with a scope open, so the only path an outside library gets a recorder through breaks `clear()`'s precondition.
- Scenario: `g.extend([](Recorder& r){ r.host({}, a); r.clear(); r.host({}, b); });` skips `rule_names_.push_back`, `rule_names_.size() - 1` wraps, and `replay()` reads `rule_names_[spec.rule]` out of bounds.
- Fix: `clear()` resets `scope_has_node_`. A recording-only type for `extend()` is the structural fix and changes the SPEC's one-class recorder; it is not proposed here.
- Breaks callers: no.

**QC-11** `modules/surface/api/include/SushiRuntime/api/graph/step_driver.hpp:822-826`
- Defect: under `StepSync::AtEnd`, `run_chained()` calls `steps.after_step()` before `chain.finish()` and `steps.check_faults()`; `run_scheduled()` checks first (`:786-790`).
- Scenario: a chained `run()` whose kernel faults has already advanced its States when the error surfaces, against the README's "a failed synchronous run leaves its states untouched".
- Fix: run the last step's `after_step()` after `finish()` and `check_faults()`; document that a failed multi-step chained run has advanced the earlier steps.
- Breaks callers: no.

**QC-12** `modules/surface/api/include/SushiRuntime/api/graph/step_driver.hpp:302-305`, `:482`, `:537`, `:727`
- Defect: `cancel()` and `reset_cancellation()` act on the runtime's one engine, while the pending marker is per graph.
- Scenario: `auto h = g1.run_async(); g1.cancel(); g2.run();` clears the flag at line 727, so `g1`'s step is no longer cancelled; reversed, `g2.cancel()` stops `g1`.
- Fix: `entry_refusal()` asks the engine whether a run is in flight, which needs a query on `IExecutionEngine`.
- Breaks callers: no; it adds a virtual to an engine interface.

**QC-13** `modules/surface/api/include/SushiRuntime/api/graph/step_driver.hpp:373`
- Defect: a move with a pending step is made safe (`end_step_before_move()`), destruction is not: `~StepDriver() = default`.
- Scenario: `RunHandle<Graph> h; { Graph g = rt.graph(); g.add(...); h = g.run_async(); }` frees the plan under the workers, then `~RunHandle` settles a step on a dead graph.
- Fix: `~Graph` and `~DynamicGraph` end the pending step through a driver member before the plan dies.
- Breaks callers: no.

### (c) api vocabulary, launch, detail

**QC-14** `modules/surface/api/include/SushiRuntime/api/detail/callable_checks.hpp:141`, `callable_traits.hpp:85`
- Defect: a coupled rule receives mutable pointers to the States it only reads; the check tests `Inputs*...` and `views_current` returns `StateView::cur`, a `T*`, while the assert text promises `const A* in0`.
- Scenario: `add(Coupled(u, v), [](std::size_t i, const float* c, float* n, float* vin) { vin[i] = 0; })` compiles and writes `v`'s current field, registered as a read; two rules reading `v` race.
- Fix: `views_current` returns `const T*` and `accepts_coupled_rule` tests `const Inputs*...`.
- Breaks callers: yes, a kernel declared with `A*` inputs stops compiling.

**QC-15** `modules/surface/api/include/SushiRuntime/api/vocabulary/dynamic.hpp:47`, `detail/emit_dynamic.hpp:72-90`, `graph/recorder.hpp:255`, `:457`
- Defect: `sized_from_device` and `based_at_device` keep a raw word pointer and no liveness observer; the sibling `LateCount::from_device` keeps `slot_life_` (`late_count.hpp:108`).
- Scenario: a counter buffer freed after `add()` is read by the kernel; `has_dangling_resource()` stays false.
- Fix: `DeviceSlot` carries the `LivenessObserver`; `adopt_device_bindings` and the two recorder members push it into `lives`.
- Breaks callers: no. Reported twice, by (b2) and (c).

**QC-16** `modules/surface/api/include/SushiRuntime/api/detail/emit_offload.hpp:62-64`
- Defect: `offload_metadata` drops every parameter past `K_MAX_PARAMS` (8) without an error.
- Scenario: `add("k"_op, n, r, w, local, {1,2,3,4,5,6,7,8,9})` sends eight parameters.
- Fix: `Recorder::offload` and `offload_rule` throw `invalid_graph` at record for more than `TaskMetadata::K_MAX_PARAMS`.
- Breaks callers: only one that already loses data.

**QC-17** `modules/surface/api/include/SushiRuntime/api/graph/recorder.hpp:419`, `:439`, `:462`, `:557`; `detail/emit_state.hpp:79-85`
- Defect: a State rule stores `&state` and every replay dereferences it, while `State` is movable and the liveness token moves with the value, so the guard cannot see the move. `data/README.md:72` documents the limit.
- Scenario: `states.push_back(rt.state<float>(n)); g.add(states[0], fn); states.push_back(...)` reallocates and the next compile reads a dangling `State*`.
- Fix: the state emitters take the keys, the size and the shared `StateView<T>` by value instead of `State<T>*`.
- Breaks callers: no. Found by (h).

### (d) engine

**QC-18** `modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp:280`, `:288`
- Defect: a throw from `get_info` or `event.wait()` leaves the entry in the list, not ready and not reaped; the stall path at `:331` catches, the main path does not.
- Scenario: `dispatch_loop` records the failure (`task_scheduler.cpp:479-483`) and the next poll queries the same event. If it throws again, `drop_inflight()` never runs and `wait_quiescent()` never returns. Whether this toolchain's events throw repeatedly is not proven, which is why this is major and not critical.
- Fix: catch per entry in `poll_events`, mark the entry ready with no event and not executed, keep the first `exception_ptr`, rethrow after the pass.
- Breaks callers: no.

**QC-19** `modules/engine/scheduler/include/SushiRuntime/scheduler/task_scheduler.hpp:249-263`, `quiescence_latch.hpp:88`
- Defect: `profiling_active_`, `active_ledger_` and the two dispatch-step atomics sit directly before `quiescence_.inflight_`, which carries no `alignas`.
- Scenario: every worker writes `inflight_` twice per node and reads `profiling_active_` each loop iteration, so each completion invalidates the line all workers read. Not measured.
- Fix: `alignas(Core::CACHE_LINE_SIZE)` on `inflight_` with padding; move the round fields away from it; pad the steal counters.
- Breaks callers: no.

**QC-20** `modules/engine/scheduler/source/task_scheduler.cpp:682`, `:733`, `:738-745`
- Defect: one device dispatch default-constructs three `sycl::event`s and copies the real one twice. The scheduler README states that default construction allocates on this libsycl.
- Scenario: per node, on the path this project measured at 96 to 98% of a tick. Not measured.
- Fix: `ActiveEventList::push(PolledEvent&&)` filling the slot by move; initialise `e` from the submit; decide `ready` from the execution mode.
- Breaks callers: no.

**QC-21** `modules/engine/execution/include/SushiRuntime/execution/execution_engine.hpp:79-216`
- Defect: `IExecutionEngine` holds 16 virtuals for six concerns, most with no-op defaults.
- Scenario: each new capability edits the interface every engine and test double derives from.
- Fix: split by concern. Structural; see `PLAN.md`, "Not repaired".
- Breaks callers: yes.

**QC-22** `modules/engine/task/include/SushiRuntime/task/node.hpp:77-372`
- Defect: `Node` holds work payload, dependency edges, region layer, placement, identity and width, profiling and offload state.
- Fix: extract `NodePlacement` and `NodeIdentity` as `RegionLink` was. Structural.
- Breaks callers: yes.

**QC-23** `modules/engine/scheduler/include/SushiRuntime/scheduler/task_scheduler.hpp:95-505`
- Defect: `TaskScheduler` still holds thread lifecycle, injector queues, dispatch, steal topology, telemetry, migration, profiling round state and the offload sink.
- Fix: next bricks are an injector and a steal topology. Structural.
- Breaks callers: no.

### (e1) foundation

**QC-24** `modules/foundation/logging/source/logger.cpp:208`
- Defect: after `shut_down()` a log call takes `sink_mutex_` with a blocking `lock_guard`, although `shut_down()` (`:260`, `std::try_to_lock`) allows for that mutex being abandoned.
- Scenario: process exit ends the logger thread inside `deliver()`; the next static destructor that logs never returns.
- Fix: `shut_down()` records the abandoned mutex; `write()` then writes to stderr unlocked.
- Breaks callers: no.

**QC-25** `modules/foundation/core/include/SushiRuntime/core/span.hpp:34`
- Defect: `#if __cplusplus >= 202002L` makes `SushiRuntime::span` an alias of `std::span` per consumer, while the library is built as C++17 and exports functions and virtuals that take it (`registered_task.hpp:55-58`, `i_transport.hpp:254`, `offload_coordinator.hpp:87`).
- Scenario: a C++20 consumer's declaration mangles `std::span<...>`, the library exported `SushiRuntime::span<...>`, and the link fails.
- Fix: always define the shim under its own name; give it a converting constructor from `std::span` under C++20.
- Breaks callers: no for C++17 callers; a C++20 caller that relied on the alias loses it.

**QC-26** `modules/foundation/core/include/SushiRuntime/core/span.hpp:122`
- Defect: `span(Container&& c)` accepts an rvalue container for a non-const element type.
- Scenario: `span<int> s = make_vector();` compiles and dangles.
- Fix: constrain the constructor to lvalues, or to a const element type for rvalues.
- Breaks callers: yes, where a temporary is passed today.

**QC-27** `modules/foundation/core/include/SushiRuntime/core/result.hpp:115-186`
- Defect: eight accessors document "without bounds checking" and "undefined behavior"; the bodies are `std::get`, which throws `std::bad_variant_access`.
- Scenario: `try_run().value()` on an error throws a type that is not `SushiException`, against SPEC 2.5.
- Fix: one of two contracts, stated and kept: throw `SushiException` carrying the stored error, or `std::get_if` behind a `@pre`.
- Breaks callers: only one that catches `std::bad_variant_access`.

**QC-28** `modules/foundation/core/include/SushiRuntime/core/sushi_ptr.hpp:68`
- Defect: `sushi_ptr(T* p)` is implicit.
- Scenario: `Node n; void use(sushi_ptr<Node>); use(&n);` takes the count 0, 1, 0 and deletes a stack object.
- Fix: `explicit` and `noexcept`.
- Breaks callers: yes, every implicit `T*` conversion in the tree and above it.

### (e2) platform

**QC-29** `modules/platform/topology/source/hwloc_topology_provider.cpp:90`, `numa_mapper.cpp:102`, `hw_discovery.cpp:182`
- Defect: `hwloc_get_ancestor_obj_by_type(topology, HWLOC_OBJ_NUMANODE, ...)` walks parent links; in hwloc 2 NUMA nodes are memory children and are never an ancestor.
- Scenario: `cpu_core_to_numa()` takes the `: 0` fallback for every core, so a two-socket machine reports one node and stealing is NUMA-blind. Read from the hwloc 2 object model, not run; a multi-node machine or `lstopo` settles it.
- Fix: one private helper over `hwloc_get_local_numanode_objs` or the object's `nodeset`, used at the three sites.
- Breaks callers: no.

**QC-30** `modules/platform/memory/source/usm_allocator.cpp:132-134`
- Defect: `release()` frees an address it has no record of with `sycl::free(address, queues_[0])`.
- Scenario: the allocator records every address it allocates, so "no record" is a second release or foreign memory: `alloc.release(p); alloc.release(p);` is a double free after one WARN.
- Fix: log and return without freeing.
- Breaks callers: one that relied on foreign USM being freed on device 0.

**QC-31** `modules/platform/memory/source/usm_allocator.cpp:111-113`, `usm_allocator.hpp:128-131`
- Defect: the observer must outlive "every release() that began before the removal", and the surface gives no way to know that; the pointer is loaded and called with no in-flight guard.
- Scenario: `Cluster` teardown stores null and destroys the coordinator while a `release()` on another thread has already loaded the pointer.
- Fix: `set_release_observer` waits for in-flight calls.
- Breaks callers: no.

**QC-32** `modules/platform/observability/include/SushiRuntime/observability/run_report.hpp:48`, `:166`, `:170`, `:175`, `:180`
- Defect: five fields document values the runtime never produces. `fill_run_report` (`run_report_builder.hpp:80-95`) pushes one row with `numa_id` at -1 and credits every task to device 0's class, against "one entry per active node"; nothing writes `warnings`.
- Fix: mark them reserved or "0 unless profiling", as lines 57 and 68 already do.
- Breaks callers: no.

**QC-33** `modules/platform/topology/include/SushiRuntime/topology/hw_discovery.hpp:92-94`
- Defect: "Duplicate SYCL devices ... are deduplicated", but the only dedupe is `seen_cpu_numa`, inside the CPU branch (`hw_discovery.cpp:187`).
- Scenario: a GPU exposed by two backends is returned twice and gets two queue sets.
- Fix: dedupe GPUs by PCI address, or correct the sentence.
- Breaks callers: no.

**QC-34** `modules/platform/memory/include/SushiRuntime/memory/epoch_reclamation.hpp:327`
- Defect: once `retire_count` reaches the threshold and nothing is reclaimable, every `retire()` scans all slots and the whole list.
- Scenario: one reader held across a long section makes N retires cost O(N^2).
- Fix: rescan only after the count grows by another `RETIRE_THRESHOLD`.
- Breaks callers: no.

### (f) distributed

**QC-35** `modules/extension/distributed/source/cluster.cpp:179`, `distributed_coordinator.cpp:711`
- Defect: the failure path of `Cluster::attach` stops a transport it did not start.
- Scenario: a second `attach` on a transport the first cluster started gets `AlreadyStarted`, then `coordinator.stop()` runs `transport->stop()` unconditionally. The exception text says the earlier cluster "stays installed"; it is installed on a dead transport. `worker_agent.cpp:496` has the same shape per the reviewer; that line was not read.
- Fix: the coordinator records that its own `transport->start()` returned Ok and stops the transport only then.
- Breaks callers: no.

**QC-36** `modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp:165`, `:170`, `:203`
- Defect: after `detach()` the handle still reports the stopped master's workers. `WorkerDirectory::close()` sets `closed_` and nothing else (`worker_directory.cpp:400-408`) and `wait_for_workers` returns `reached()` (`:359`).
- Scenario: `cluster.detach(); cluster.wait_for_workers(2, 1s)` returns true.
- Fix: `close()` retires every session; `wait_for_workers` returns `!closed_ && reached()`.
- Breaks callers: no.

**QC-37** `modules/extension/distributed/include/SushiRuntime/distributed/kernel_registry.hpp:52-53`, `source/kernel_registry.cpp:38`
- Defect: "from then on it is only read" is a rule nothing keeps; `register_kernel` writes an unguarded map the worker's exec thread reads.
- Scenario: `agent.start(); kernels->register_kernel(op, k);` is a data race, and the new op is never advertised.
- Fix: `KernelRegistry::seal()`, called by `WorkerAgent` and `run_worker`; `register_kernel` throws once sealed and refuses an empty kernel.
- Breaks callers: one that registers late.

**QC-38** `modules/extension/distributed/include/SushiRuntime/distributed/cluster.hpp:87-93`, `source/cluster.cpp:75-88`
- Defect: 0 means "no bound" for `task_deadline` and "already expired" for `handshake_timeout` and `liveness_timeout` (`worker_directory.cpp:301`, `:318`), and `check_options` validates neither.
- Scenario: `options.handshake_timeout = 0ms` closes every peer on the first monitor pass.
- Fix: `check_options` and `Cluster::liveness_timeout()` refuse a value below 1 ms.
- Breaks callers: one that passes 0 today.

### (g) interop

**QC-39** `modules/surface/interop/include/SushiRuntime/interop/external_memory.hpp:55`
- Defect: `ExternalHandleKind kind;` has no initializer and `require_importable` (`interop.cpp:140`) reads it.
- Scenario: `ExternalMemoryDescriptor d; d.handle = h; ...; import_device(d, ...)` reads an indeterminate value. SushiEngine declares the descriptor this way per the reviewer.
- Fix: an `Unspecified` enumerator as the default, refused with `invalid_access`.
- Breaks callers: no at source level; enumerator values shift.

**QC-40** `modules/surface/interop/include/SushiRuntime/interop/external_memory.hpp:56`
- Defect: nothing states who closes `handle` after a successful import, a refused import, or the `Buffer`'s destruction.
- Scenario: a caller closes a descriptor the backend took over, or leaks one it only duplicated. Which backend takes ownership of which kind was not verified.
- Fix: state the rule per `ExternalHandleKind` in the field comment and the README.
- Breaks callers: no.

**QC-41** `modules/surface/interop/source/interop.cpp:81-89`, `:164-168`
- Defect: `capabilities().device_import` is true for a GPU with the import aspect and no UUID, and `require_same_device` then refuses every descriptor on it.
- Fix: compute the UUID first and set `device_import` only when it is present.
- Breaks callers: no.

**QC-42** `modules/surface/api/include/SushiRuntime/api/interop/interop.hpp:58`
- Defect: `Interop`'s constructor is public and accepts a null context; the sibling `AdvancedControls` keeps it private with `friend class Runtime`.
- Scenario: `API::Interop interop(nullptr); interop.capabilities(DeviceIndex{0});` dereferences null at line 70.
- Fix: private constructor, `friend class Runtime`.
- Breaks callers: a direct construction outside `Runtime`; none found in SushiEngine by the reviewer.

### (h) cross-cutting

**QC-43** `applications/examples/quickstart.cpp:40` and five more of the nine programs
- Defect: `main` calls `Runtime::create()` and `run()` with no `try`.
- Scenario: on a machine where SYCL reports no device, the first program a user copies ends in `std::terminate` without printing `what()`. `quickstart.cpp` was read by the chief; the other five are the reviewer's list.
- Fix: the `catch (const SushiException&)` that `operating_path_demo.cpp:154` already has.
- Breaks callers: no.

**QC-44** `modules/platform/memory/include/SushiRuntime/memory/usm_registry.hpp:46`, `modules/foundation/core/include/SushiRuntime/core/ref_counted.hpp:42`, `modules/engine/task/include/SushiRuntime/task/task.hpp:46`
- Defect: three classes defined whole in their headers carry `SUSHIRUNTIME_API`; no `usm_registry.cpp`, `ref_counted.cpp` or `task.cpp` exists.
- Scenario: `UsmRegistry` exports its inline members and puts a `std::mutex` and a `std::unordered_map` layout into the DLL's ABI. The changelog already removed the macro from `RangeTask` for this reason.
- Fix: drop the macro from `UsmRegistry` and `RefCounted`; decide `Task` (polymorphic).
- Breaks callers: no.

**QC-45** `modules/surface/api/include/SushiRuntime/api/graph/builder_surface.hpp:477`, `:493`, `:242`
- Defect: the two deprecated `std::vector<void*>` overloads hold their own body (`sink().kernel(n, whole_buffers(reads, writes), ...)`) and do not forward to the `Reads`/`Writes` overload; the third `vector<void*>` form at `:242` is not deprecated.
- Fix: each builds `Reads`/`Writes` and calls the replacement in one line; deprecate `:242`.
- Breaks callers: no; one new warning.

## Confirmed findings: minor

One row per pattern. "B" is "breaks a caller".

### (a) api runtime and data
- QC-46 `api/data/state.hpp:129`, `:136`: `State::operator[]` and `host()` throw an error that names `Buffer::operator[]`. Fix: pass their own name to `require_host_accessible`. B: no.
- QC-47 `api/runtime/runtime.hpp:105`, `:119`: `@throws` says the message carries "the index and the count"; it carries the device count and names the allocator. Fix: check the index in the allocating `Buffer` constructor, name `Runtime::buffer`. B: no.
- QC-48 `api/data/README.md:118`, `api/detail/handle_access.hpp:90`: "expires when the buffer is destroyed or moved from" is false for a move (`buffer.hpp:257`). Fix: "destroyed or assigned over". B: no.
- QC-49 `api/data/README.md:122-125`: says three key members carry no attribute; all five forwarders are `[[deprecated]]`. B: no.
- QC-50 `api/data/README.md:50`: "The memory is shared USM" is false for two residencies. B: no.
- QC-51 `modules/surface/api/README.md:30`, `:72`: lists `task_types.hpp` as declaring nothing and places `_op` in `vocabulary/`, whose `op_literal.hpp` is deleted. B: no.
- QC-52 `modules/surface/api/include/SushiRuntime/README.md:3-6`, `modules/surface/api/README.md:8-13`: folder table with dead links; documents `version.hpp`, which lives in `core`. B: no.
- QC-53 `api/data/state.hpp:55`: `StateView::cur` not renamed to `current` as SPEC 2.9 says; eight detail sites read it. B: no (detail).
- QC-54 `api/data/buffer.hpp:333`, `:300`; `state.hpp:94`: `operator[]` states no `@pre` on the index or an empty handle; the `empty()` brief contradicts its `@return`. B: no.
- QC-55 `api/data/buffer.hpp:62-78` and three README sentences: rationale and em dashes in comments; "the classic cause", "actually", "the deliberate escape hatch". B: no.
- QC-56 `api/data/buffer.hpp:69-104`: five public names besides `Buffer` in one header, so `DeviceIndex` drags in SYCL and the offload coordinator. Fix: their own header. B: no.

### (b1) api graph builder
- QC-57 `api/graph/region.hpp:120-128`: a recording through a kept `Recorder&` is never noticed; documented as a `@pre`, which is why it is minor. Fix: a change listener on `Recorder`. B: no.
- QC-58 `api/graph/dynamic_graph.hpp:317-323`, `:377-382`: `prepare_run()` re-aggregates every region's machinery at every run entry, hookless regions included. B: no.
- QC-59 `api/graph/builder_surface.hpp:195-218`: SPEC 2.3 promises `add(Dynamic, ...)` for every form; the capture and `Coupled` forms have no Dynamic overload. B: no.
- QC-60 `api/graph/region_composer.hpp:152-153`: the `rearm()` brief says it "refreshes the per-step updaters"; it does not. B: no.
- QC-61 `api/graph/dynamic_graph.hpp:236`, `:251`; `region_composer.hpp:136`: the profiling request has two owners. B: no.
- QC-62 `dynamic_graph.hpp:133`, `:143`, `:166`; `plan_cache.hpp:63`; `region_composer.hpp:165`, `:168`: queries without `[[nodiscard]]`. B: no.
- QC-63 `api/graph/dynamic_graph.hpp:418-419`: includes `runtime.hpp` at its end; `graph.hpp` does not. B: yes, a unit that includes only this header and names `Runtime`.
- QC-64 register: `backing_graph.hpp:24`, `:37`; `builder_surface.hpp:572`, duplicate include at `:50`/`:53`; `graph.hpp:55-58`; `plan_cache.hpp:58-60`; `dynamic_graph.hpp:261`, `:284`; graph `README.md:491`, `:699`, `:757`, `:801`. B: no.

### (b2) api recorder, step driver, run handle
- QC-65 `recorder.hpp:684-687`: `replay()` names "the last node added" after an emitter that added none, renaming the previous rule's node. B: no.
- QC-66 `recorder.hpp:434`, `:482`, `:507`, `:743`: error texts name `BuilderSurface` spellings, so a library calling `recorder.state_rule` reads about an `add()`. B: no.
- QC-67 `recorder.hpp:332-401`: `untracked_kernel(launch, fn, placement)` puts the placement after the callable, unlike every sibling. B: yes.
- QC-68 `recorder.hpp:602`: `scratch()` returns `T*`; SPEC section 3 promises a span. B: yes.
- QC-69 `recorder.hpp:184`, `:196`: count overloads chosen by SFINAE, so a mistyped callable gets "no matching function" in place of the one `static_assert`. B: no.
- QC-70 `run_handle.hpp:117`, `:126`: `pending()` and `ready()` do not ask `owns_step()`. B: no.
- QC-71 `step_driver.hpp:407`: `now() + budget` overflows for `hours::max()`. B: no.
- QC-72 register: `step_driver.hpp:581`, `:679`, `:726`, `:786` (trailing comment); `run_handle.hpp:181`, `:190`. B: no.

### (c) api vocabulary, launch, detail
- QC-73 `detail/emit_launch.hpp:211`, `:226`, `:245`: `submit_launch` submits a zero-size `parallel_for`, which the module README calls non-portable; the late-bound paths guard it. No backend failure was reproduced. B: no.
- QC-74 `launch/nd_range.hpp`: the tiling precondition is stated and checked nowhere. B: no.
- QC-75 `detail/callable_traits.hpp:56-58`: `is_per_element_v` tests `F`, not `const F&`, so a `mutable` lambda passes the assert and fails inside the emitter. B: no.
- QC-76 `detail/callable_traits.hpp:46`, `:51-53`: comments say the traits select `add()` overloads; the README says they no longer do. B: no.
- QC-77 `detail/emit_state.hpp:64-71`, `:121-130`: State, coupled, untracked and offload nodes stamp no `baked_width`, unlike their siblings; `offload_node` writes its own `parallel_for`. B: no.
- QC-78 `detail/device_fault.hpp:66-68`: fault fields narrowed to `uint32`. B: no.
- QC-79 `detail/emit_launch.hpp:135`, `:145`: `origin + extent` wraps. B: no.
- QC-80 `detail/emit_dynamic.hpp:68`: brief says "count source"; it copies the base source too. B: no.
- QC-81 `late_count.hpp` (`from_host`): one provider type is polled at dispatch for `LateCount` and in `before_step` for `Dynamic`; no thread is named. B: no.
- QC-82 register: `vocabulary/dependencies.hpp`, `launch/extent.hpp` ("under the name it had before"), `vocabulary/dynamic.hpp:88`, vocabulary and detail READMEs. B: no.

### (d) engine
- QC-83 `graph/task_graph.hpp:94-100`, `task_graph.cpp:156-158`: a caller-supplied registration index is not checked; `NO_REGISTRATION` sizes two vectors to 2^32. B: no.
- QC-84 `execution/runtime_context.hpp:129-136`, `runtime_context.cpp:288-296`: `install_offload` releases the previous coordinator while a `TaskGraph` may hold its address; the `@pre` does not name live graphs. B: no.
- QC-85 `task/node.hpp:185`, `node_profiling.hpp`: `Node::profiling` has no reader or writer per the reviewer's grep; 24 bytes of atomics per node. B: yes if deleted.
- QC-86 `scheduler/source/task_scheduler.cpp:515-683`: `is_graph_node()` and `get_execution_mode()` called twice per node. B: no.
- QC-87 `scheduler/event_polling.hpp:54`: `using namespace SushiRuntime::Core;` at namespace scope in a public header. B: yes, if a caller relies on the leak.
- QC-88 false Doxygen: `task_scheduler.hpp:57`, `:75`, `:395`; `thermal_proxy_strategy.hpp:40`, `:55`; `task_graph.hpp:59-62`. B: no.
- QC-89 `task/small_event_vector.hpp:58-71`: assignment leaves old events alive; the defaulted move keeps `static_count`. B: no.
- QC-90 `task/node.hpp:225-239`: `add_successor` is `noexcept` and calls `new`. B: no.
- QC-91 `task/task_types.hpp:84`, `:159`, `:179`: `set_param` ignores a bad index; the hash casts through signed `char`. B: no.
- QC-92 `scheduler/range_task.hpp:67`, `:93`, `:104`: `start > end` accepted; `is_splittable()` and `split()` disagree for 1025 to 2047. B: no.
- QC-93 consistency: `I` prefix on four interfaces and none on two; `get_` accessors beside bare ones; enumerator case; classes not `final`; private section first in four. B: yes for renames.
- QC-94 register: listed in the reviewer's item 18 (`graph/chain_plan.hpp:46`, `compiled_graph.hpp:59`, `:99`, `runtime_settings.hpp:56`, `task_types.hpp:54`, the scheduler, execution and task READMEs). B: no.

### (e1) foundation
- QC-95 `logging/source/logger.cpp:252-253`: `shut_down()` joins from a static destructor; a `FreeLibrary` while the process lives would wait on the loader lock. Derived from the loader rule, not run. B: no.
- QC-96 `core/delegate.hpp:155`: `invoke_impl` is ill-formed for `R = void` with a callable that returns a value. B: no.
- QC-97 `core/delegate.hpp:159`, `:78`: `move_impl` is `noexcept` over a move that may throw. B: yes at compile time for such captures.
- QC-98 `core/sushi_ptr.hpp:237`: comparing with a raw pointer builds a temporary. B: no.
- QC-99 `logging/logger.hpp:184`: `SR_THROW_IF` declares `msg_` in the caller's scope. B: no.
- QC-100 `core/sushi_ptr.hpp:210`, `:219`, `:250`; `ref_counted.hpp:92`; `bit.hpp:49`, `:66`; `strong_id.hpp:55`; `span.hpp:132-173`: no `[[nodiscard]]`. B: a new warning.
- QC-101 `core/strong_id.hpp:46`, `:49`: public writable `raw`; no `std::hash`, no `operator<`; README says "trivial". B: no.
- QC-102 `core/README.md:57-58`: says "release fence"; the code is an acquire fence. B: no.
- QC-103 `sycl/sycl_compat.hpp:26`, `:37`: `@see` points at the wrong README; the module's entry point sits in `SushiRuntime::detail`. B: yes if moved.
- QC-104 `core/version.hpp:47`: brief on the encoder describes the encoded value. B: no.
- QC-105 `core/ref_counted.hpp:46`: `ref_count` is protected and typed `int32_t` without `<cstdint>`. Reported by (e1) and (h). B: no.
- QC-106 register and README shape: `bit.hpp:44`, `span.hpp:57`, `common.hpp:40`, `:50`, `ref_counted.hpp:60`, `core/README.md:29`, `:76`; the core README has no entry-point table. B: no.

### (e2) platform
- QC-107 `topology/source/numa_mapper.cpp:72`: accelerators take the CPU branch although their PCI address was resolved. B: yes if `HardwareNode` changes.
- QC-108 `memory/epoch_reclamation.hpp:237`: the deleter runs while the node is still linked; a `T` whose destructor retires re-enters. Latent: today's only `T` is trivial. B: no.
- QC-109 `memory/epoch_reclamation.hpp:130`, `:141`, `:264`: inline singleton and `thread_local` state in a public header, public constructor. Latent: only `task_scheduler.cpp` and `dependency_tracker.cpp`, both inside the library, use it. B: no.
- QC-110 `memory/epoch_reclamation.hpp:210-218`: the reclaim scan loads with `acquire` against a `seq_cst` fence on the reader side. Safe on x86. B: no.
- QC-111 `observability/profiling_ledger.hpp:56-87`: moves leave `other.node_count_` set over empty vectors. B: no.
- QC-112 `memory/resource_region.hpp:59`: two default regions overlap although the brief says an empty region overlaps nothing. B: no.
- QC-113 `topology/numa_mapper.hpp:55`, `:65`: `get_distance` has no caller and loads a topology per call; `map_distances` assigns NUMA nodes. B: yes.
- QC-114 `topology/i_topology_provider.hpp:59`, `:68`: the interface has a default body and shares its header with a second interface; four implementations are not `final`. B: yes.
- QC-115 false statements: `hwloc_topology_provider.hpp:72` ("O(1)"), `topology/README.md:73`, `profiling_ledger.hpp:173`, `usm_allocation_owner.hpp:60` (`noexcept`), `device_queue_set.hpp:125`. B: no.
- QC-116 naming: three spellings of the NUMA id, four constant forms, `NUMAMapper` beside `NumaRoute`. B: yes if renamed.
- QC-117 `memory/source/usm_allocator.cpp:115-116`, `profiling_ledger.hpp:242`: two mutexes per release; sixteen cursors per cache line. B: no.
- QC-118 register: `resource_region.hpp:40`, `run_report.hpp:142`, `hwloc_topology_provider.cpp:103`, `numa_mapper.cpp:216`, history in three READMEs. B: no.

### (f) distributed
- QC-119 `distributed/buffer_view.hpp:40-41` against `kernel_registry.hpp:61-62`: two lifetimes for one memory; the code keeps the second. B: no.
- QC-120 `distributed/cluster.hpp:107-233`: implicit move leaves a null `state_` every member dereferences. Fix: declare copies and no move. B: no.
- QC-121 `source/outbox.cpp:55`, `:102`; `tcp_transport.cpp:763`: one allocation and two mutexes per control frame. B: no.
- QC-122 `distributed/i_offload_policy.hpp:48`, `:75`: `WorkerStats::worker_id` is `int` with -1 beside `PeerId`; `alive` is always true; the name is the one `Stats` beside four `Statistics`. B: yes, every policy.
- QC-123 `in_process_transport.hpp:189`, `tcp_transport.hpp:107`: `struct Impl` declared in the public section. B: no.
- QC-124 `source/i_offload_policy.cpp:45-86`: "optimal", narrating comments. B: no.
- QC-125 `distributed/README.md:38-39`: lists the `_op` literal among excluded headers. B: no.

### (g) interop
- QC-126 `interop/source/interop.cpp:352-367`, `:94-109`: handle kinds dispatched by an enum, a switch, a per-platform constant and an `if/else`; a third kind edits four places. B: no.
- QC-127 `interop.cpp:191-201`, `:228-240`: unmap and release share one `try` with an empty `catch (...)`. B: no.
- QC-128 `interop.cpp:346-370`: no zero-size path, unlike `export_host_memory`. B: no.
- QC-129 `interop.cpp:363`: `static_cast<int>(descriptor.handle)` unchecked; the default 0 is a valid descriptor. B: no.
- QC-130 `api/interop/interop.hpp:68`, `:82`, `:103`; `interop/interop.hpp:55-75`: no `[[nodiscard]]`; two methods non-const. B: no.
- QC-131 `interop/interop.hpp:60`: `@return` false for zero bytes. B: no.
- QC-132 `modules/surface/interop/README.md:8`: section named after the facade method. B: no.
- QC-133 `api/interop/interop.hpp:24`, `:47`: "fluent" for a class that chains nothing. B: no.

### (h) cross-cutting
- QC-134 `api/graph/step_driver.hpp:683`, `:464`, `:576`: `noexcept` `try_` entries build `std::string` messages outside any handler. B: no.
- QC-135 `observability/event_timestamps.hpp:50`: `out` is written before the call that can fail. B: no.
- QC-136 naming across modules: `Detail` in 5 public headers against `detail` in 34; `K_UPPER_SNAKE` 17 names against bare `UPPER_SNAKE` 29, with this phase's own replacements split between the two; 14 `get_` accessors; `ctx_` against `context_`; split bool-query spellings. B: yes if renamed.
- QC-137 `docs/reference/CHANGELOG.md`, `## Unreleased`: seven deprecations are not named (`Cluster::attach` four-argument forms, `Runtime::create(DeviceFilter, RuntimeConfig)`, `get_completion_rate`, three-argument `migrate_tasks`, `State::advance()`, the two `vector<void*>` `add` forms). The orchestrator writes the changelog. B: no.
- QC-138 `applications/examples/README.md:3`: says each example includes the umbrella; `distributed_worker.cpp` does not. B: no.

## Refuted, reclassified, duplicate

No finding was refuted outright. These were changed on verification:

- (d) `event_polling.hpp:280`, reported critical: kept as major QC-18. The stuck entry needs an event that throws on every poll, which nobody showed.
- (e2) `EpochManager` inline singleton, reported major: minor QC-109. The two users are both inside the library, so no second instance exists today.
- (e2) reclaim re-entrancy, memory order, ledger move, accelerator NUMA branch, reported major: minor QC-108, 110, 111, 107. Each is latent or needs a target the project does not build for.
- (e1) logger join under `FreeLibrary`, reported major: minor QC-95, unverified without a run.
- (c) zero-size `parallel_for`, reported major: minor QC-73. The only evidence is the README sentence; SYCL 2020 does not forbid an empty range.
- (b1) kept `Recorder&` on a region, reported major: minor QC-57, a documented precondition.
- (d) unchecked registration index and `install_offload`, reported major: minor QC-83, 84. The api layer builds a fresh `TaskGraph` per build and draws its own indices.
- Duplicates: the device slot without an observer (b2 and c, QC-15); the liveness comment (a and h, QC-48); `ref_count` (e1 and h, QC-105); `StateView::cur` (a and h, QC-53); `WorkerStats` (f and h, QC-122).

## Not reviewed

- Every `tests/` folder, beyond greps for call sites.
- `cmake/`, every `CMakeLists.txt`, `cli/`, `tools/`: another session holds them.
- Sources read by excerpt only: `task_graph.cpp`, `chain_plan.cpp`, `dependency_tracker.cpp`, `work_stealing_deque.hpp`, `runtime_context.cpp`; in the distributed module `peer_connection.cpp`, the socket files, `master_link.cpp`, `pending_tasks.cpp`, `buffer_exchange.cpp`, `worker_buffer_cache.cpp`, the wire codec.
- The export-macro check for foundation and platform modules in the missing-export direction; `explicit` on template and multi-line constructors; `noexcept` bodies outside `step_driver.hpp` and `event_timestamps.hpp`.
- Repositories above this one (SushiBLAS, SushiEngine), beyond two greps in SushiEngine. A finding marked "breaks a caller" was not checked against them.
- Nothing was compiled, built or run. QC-19, QC-20 and QC-34 are costs read from the code, with no measurement. QC-29 and QC-95 rest on library behaviour that was read, not observed.
- Of the majors, the chief read the cited lines of each; for QC-21, 22 and 23 that means the size and member list of the type, not every use, and for QC-43 one of the six programs.

## Dispatches

The harness reports tool uses, not turns; the number below is tool uses.

```
dispatch: (a) api runtime and data — opus low, 45 turns, passed
dispatch: (b1) api graph builder — opus low, 33 turns, passed
dispatch: (b2) api recorder, step driver, run handle — opus low, 36 turns, passed
dispatch: (c) api vocabulary, launch, detail — opus low, 23 turns, passed
dispatch: (d) engine — opus low, 30 turns, passed
dispatch: (e1) foundation — opus low, 37 turns, passed
dispatch: (e2) platform — opus low, 40 turns, passed
dispatch: (f) distributed — opus low, 39 turns, passed
dispatch: (g) interop — opus low, 32 turns, passed
dispatch: (h) cross-cutting — opus low, 102 turns, passed
```

Every reviewer's reply carried the four evidence items: SOLID checked as a named item, `humanizer` invoked, no source written, no scratch written.

## Repairs

Six waves repaired the findings between `492941c` and `bc12c24`. The orchestrator built and
tested each hand-back; the workers built nothing.

| Wave | Modules | Tests after the wave |
| --- | --- | --- |
| 1 | core, logging, topology, memory, observability, interop | 712 of 712 |
| 2 and 3 | scheduler, task, graph, execution, api data and vocabulary | 757 of 757 |
| 4 and 5 | api recorder, step driver, region composer, builder surface | 785 of 785 |
| 6 | api graphs, distributed, examples, READMEs, register pass | 798 of 798 |

Both critical findings are repaired. The findings that stay open are listed in
[`REMAINING_WORK.md`](../../design/REMAINING_WORK.md), section "API quality control", and the
ones a user can hit in [`KNOWN_ISSUES.md`](../../reference/KNOWN_ISSUES.md).

Two workers broke an instruction. T15 ran `git stash` and `git stash pop` once; the tree came
back unchanged. T21 wrote three files through Python in place of the edit tool.

```
dispatch: T01 span — haiku high, 33 turns, passed after chief rework
dispatch: T02 result — haiku high, 23 turns, passed
dispatch: T03 sushi_ptr — haiku high, 43 turns, passed
dispatch: T04 core rest — haiku high, 66 turns, passed
dispatch: T05 logging — haiku high, 37 turns, passed after chief rework
dispatch: T06 topology — opus low, 38 turns, passed
dispatch: T07 memory — opus low, 54 turns, passed
dispatch: T08 observability — haiku medium, 38 turns, passed
dispatch: T09 interop — opus low, 33 turns, passed
dispatch: T10 scheduler — opus low, 67 turns, passed
dispatch: T11 task — haiku high, 65 turns, passed
dispatch: T12 graph, execution — opus low, 76 turns, passed
dispatch: T13 api data — haiku high, 97 turns, passed
dispatch: T14 vocabulary, detail — opus low, 74 turns, passed
dispatch: T15 recorder, State view — opus low, 78 turns, passed
dispatch: T17 step driver — opus low, 119 turns, passed
dispatch: T18 region composer — opus low, 96 turns, passed
dispatch: T19 builder surface — haiku high, 99 turns, failed, rung 2 (unfinished; chief closed it)
dispatch: T20 graphs — opus low, 74 turns, passed
dispatch: T21 distributed — opus low, 125 turns, passed
dispatch: T22 examples — haiku medium, 46 turns, passed
dispatch: T23 api READMEs — haiku medium, 30 turns, passed
dispatch: closing register pass — haiku high, 161 turns, passed
dispatch: API quality control chief — opus medium, review and six repair waves, passed
```
