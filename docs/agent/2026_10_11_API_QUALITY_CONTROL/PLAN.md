# API quality control: repair plan

**Status:** Proposed on 2026-10-11; no task started. The findings are in `REPORT.md` of this
folder, by id (QC-nn).

## Rules of the repair

- A worker writes only the files its task lists, plus this folder. It adds no source file and
  edits no `CMakeLists.txt` or `cmake/` file: the build files belong to another session. A fix
  that needs a new `.cpp` is listed under "Not repaired" for that reason.
- No build and no test run by a worker. Each task ends with `clang -fsyntax-only` through
  `build/compile_commands.json` on the files it touched; the orchestrator builds and tests at
  the end of each wave.
- The orchestrator writes `docs/reference/CHANGELOG.md`, `docs/design/` and
  `REMAINING_WORK.md`. QC-137 lists the seven changelog lines owed today.
- A task that changes a public spelling keeps the old one as a `[[deprecated]]` forwarder of
  one call, per `api-stability`. No task below keeps a second implementation.
- "Approval" marks a task, or one fix in it, that the orchestrator must approve before it is
  dispatched: a change to a public interface's shape beyond a local fix, to data ownership,
  to a module boundary, or a removal of something public.

## Waves

Hierarchy first: foundation and platform, then engine, then the api detail layer, then the
recorder, then the drivers and builders that call it, then the extension and the examples.
Inside a wave the file sets are disjoint.

### Wave 1: foundation, platform, interop module

| Task | Findings | Files | Acceptance criterion |
| --- | --- | --- | --- |
| T01 span | QC-25, 26, part of 100 | `modules/foundation/core/include/SushiRuntime/core/span.hpp` | `SushiRuntime::span` is one class under C++17 and C++20, converts from `std::span` under C++20, and rejects an rvalue container of non-const elements |
| T02 result | QC-27 | `core/result.hpp` | Each of the eight accessors does what its Doxygen says, with one stated contract for the wrong alternative |
| T03 pointers | QC-28, 44 (`RefCounted`), 98, 105, part of 100 | `core/sushi_ptr.hpp`, `core/ref_counted.hpp` | `sushi_ptr(T*)` is `explicit noexcept`; `ref_count` is private `std::int32_t`; raw-pointer comparison builds no temporary |
| T04 core rest | QC-96, 97, 101, 102, 104, 106, part of 100 | `core/delegate.hpp`, `core/strong_id.hpp`, `core/bit.hpp`, `core/version.hpp`, `core/common.hpp`, `modules/foundation/core/README.md` | A `void` delegate accepts a value-returning callable; a throwing-move callable is refused at compile time; the README has the entry-point table |
| T05 logging | QC-24, 99 | `modules/foundation/logging/source/logger.cpp`, `logging/logger.hpp` | A log call after `shut_down()` never blocks on `sink_mutex_` |
| T06 topology | QC-02, 29, 33, 107, 115 (topology part) | `topology/affinity.hpp`, `topology/hw_discovery.hpp`, `modules/platform/topology/source/hwloc_topology_provider.cpp`, `numa_mapper.cpp`, `hw_discovery.cpp`, `modules/platform/topology/README.md` | An out-of-range core id is logged and ignored; the three NUMA lookups go through one helper that does not depend on ancestor links |
| T07 memory | QC-30, 31, 34, 44 (`UsmRegistry`), 108, 110, 112, 117 (allocator part) | `modules/platform/memory/source/usm_allocator.cpp`, `memory/usm_allocator.hpp`, `memory/usm_registry.hpp`, `memory/epoch_reclamation.hpp`, `memory/resource_region.hpp`, `memory/usm_allocation_owner.hpp` | A second `release()` of one address frees nothing; `set_release_observer(nullptr)` returns only when no `on_release` is running |
| T08 observability | QC-32, 111, 135, 115 (ledger part) | `observability/run_report.hpp`, `observability/profiling_ledger.hpp`, `observability/event_timestamps.hpp` | Every `RunReport` field comment states what `fill_run_report` writes; a moved-from ledger is empty |
| T09 interop | QC-39, 40, 41, 42, 127, 128, 129, 130, 131, 132, 133 | `modules/surface/interop/include/SushiRuntime/interop/*.hpp`, `modules/surface/interop/source/interop.cpp`, `modules/surface/interop/README.md`, `modules/surface/api/include/SushiRuntime/api/interop/interop.hpp`, `api/interop/README.md` | A default descriptor is refused with `invalid_access`; `device_import` is true only where `import_device` can succeed; `Interop` is built by `Runtime` only |

Wave 1 closes with one build and the full test run by the orchestrator. T03 is expected to
break call sites in the tree; see "Approval".

### Wave 2: engine

| Task | Findings | Files | Acceptance criterion |
| --- | --- | --- | --- |
| T10 scheduler | QC-18, 19, 20, 86, 87, 88 (scheduler part), 92 | `scheduler/event_polling.hpp`, `scheduler/task_scheduler.hpp`, `scheduler/quiescence_latch.hpp`, `scheduler/range_task.hpp`, `scheduler/thermal_proxy_strategy.hpp`, `modules/engine/scheduler/source/task_scheduler.cpp` | An event that throws in `poll_events` is reaped once and the run ends with its error |
| T11 task | QC-44 (`Task`), 89, 90, 91 | `task/small_event_vector.hpp`, `task/node.hpp`, `task/task_types.hpp`, `task/task.hpp` | Assignment and move of `SmallEventVector` leave no live event past the count |
| T12 graph, execution | QC-83, 84, 88 (graph part) | `modules/engine/graph/source/task_graph.cpp`, `graph/task_graph.hpp`, `execution/runtime_context.hpp` | A registration index not drawn from `reserve_registration()` throws `invalid_graph` |

T10 carries two performance fixes (QC-19, QC-20). The orchestrator records the per-node
round-trip figure before and after; a fix that does not move it is reverted, per
`performance`. Wave 2 closes with one build.

### Wave 3: api data, vocabulary and detail

| Task | Findings | Files | Acceptance criterion |
| --- | --- | --- | --- |
| T13 data | QC-03, 04, 46, 47, 48, 49, 50, 53 (field only), 54, 55 | `api/data/buffer.hpp`, `api/data/state.hpp`, `api/data/README.md`, `api/detail/handle_access.hpp`, `api/runtime/runtime.hpp` | `distributed()` on a device-resident handle throws `invalid_access`; `Buffer<std::string>` does not compile |
| T14 vocabulary, detail | QC-14, 15 (slot type and emitter), 17 (new emitter forms), 75, 76, 77, 78, 79, 80, 81, 82 | `api/vocabulary/dynamic.hpp`, `late_count.hpp`, `dependencies.hpp`, `api/launch/extent.hpp`, `nd_range.hpp`, `api/detail/callable_checks.hpp`, `callable_traits.hpp`, `emit_state.hpp`, `emit_dynamic.hpp`, `emit_offload.hpp`, `emit_launch.hpp`, `device_fault.hpp`, the vocabulary, launch and detail READMEs | A coupled kernel declared with a mutable input pointer does not compile; each state emitter has a form that takes no `State<T>*` |

T13 renames `StateView::cur` to `current`; the eight detail readers are in T14's and T15's
files, so T13 keeps `cur` as a deprecated reference member for one wave and T16 removes it.
T14 adds the pointer-free emitter forms beside the old ones; T15 switches the recorder, and
T16 deletes the old forms. Wave 3 closes with one build.

### Wave 4: recorder

| Task | Findings | Files | Acceptance criterion |
| --- | --- | --- | --- |
| T15 recorder | QC-09, 10, 15 (lives), 16, 17 (switch), 65, 66, 69 | `api/graph/recorder.hpp` | No recording member stores a `State<T>*`; `clear()` inside `extend()` leaves a recorder that records and replays in bounds |

One build.

### Wave 5: drivers and builders

| Task | Findings | Files | Acceptance criterion |
| --- | --- | --- | --- |
| T16 detail cleanup | removal of the forms T13 and T14 kept | `api/detail/emit_state.hpp`, `emit_dynamic.hpp`, `emit_offload.hpp`, `api/data/state.hpp` | No `State<T>*` emitter form and no `StateView::cur` remain |
| T17 step driver | QC-11, 13 (driver member), 70, 71, 72, 134 | `api/graph/step_driver.hpp`, `api/graph/run_handle.hpp` | A failed chained run leaves the last step's States unadvanced; the driver has one member that ends a pending step |
| T18 composer | QC-01, 06, 08, 60 | `api/graph/boundary_access.hpp`, `api/graph/region_composer.hpp` | The partial-overlap case of QC-01 yields the `w -> q` edge; a throw from `build_region` leaves every earlier edge in place |
| T19 builder surface | QC-45, 59 | `api/graph/builder_surface.hpp` | Each deprecated `vector<void*>` overload is one forwarding call |

One build.

### Wave 6: graphs, extension, examples

| Task | Findings | Files | Acceptance criterion |
| --- | --- | --- | --- |
| T20 graphs | QC-05, 07, 13 (destructors), 58, 61, 62, 64 | `api/graph/graph.hpp`, `dynamic_graph.hpp`, `region.hpp`, `plan_cache.hpp`, `backing_graph.hpp`, `api/graph/README.md` | A graph destroyed with a pending step ends the step first; `drop()` with a step pending is refused |
| T21 distributed | QC-35, 36, 37, 38, 119, 120, 123, 124, 125 | `distributed/cluster.hpp`, `kernel_registry.hpp`, `buffer_view.hpp`, `in_process_transport.hpp`, `tcp_transport.hpp`, `modules/extension/distributed/source/cluster.cpp`, `distributed_coordinator.cpp`, `worker_agent.cpp`, `worker_directory.cpp`, `kernel_registry.cpp`, `i_offload_policy.cpp`, `modules/extension/distributed/README.md` | A failed second `attach` leaves the first cluster's transport running; a detached handle reports no worker |
| T22 examples | QC-43, 138 | the nine programs under `applications/examples/`, `applications/examples/README.md` | Every `main` prints `SushiException::what()` and returns 1 |
| T23 api READMEs | QC-51, 52 | `modules/surface/api/README.md`, `modules/surface/api/include/SushiRuntime/README.md` | Every link in the two folder tables resolves |

One build, then the full run.

## Tasks that break a public spelling

| Task | Change | Forwarder |
| --- | --- | --- |
| T03 | `sushi_ptr(T*)` becomes `explicit` | None possible: an implicit conversion cannot be deprecated. Call sites are fixed in the same wave; the changelog entry names the break |
| T09 | `ExternalHandleKind` gains `Unspecified` first | None needed at source level; values shift, so SushiEngine rebuilds |
| T09 | `Interop` constructor becomes private | None; `Runtime::interop()` is the replacement |
| T14 | coupled kernels must take `const A*` inputs | None possible: the old form is the defect |
| T13 | `StateView::cur` becomes `current` | Deprecated reference member until T16; the type is `detail` |
| T19 | `add(OpID, n, vector<void*>, vector<void*>, ...)` becomes deprecated | It is the forwarder |
| T21 | `KernelRegistry::register_kernel` throws once sealed | None; registering after start was a data race |
| T21 | `handshake_timeout` and `liveness_timeout` of 0 are refused | None; 0 closed every peer |

## Approval needed from the orchestrator

1. T01, QC-25: `SushiRuntime::span` stops being `std::span` under C++20. It changes the type
   in every exported signature for a C++20 consumer.
2. T02, QC-27: which contract `Result` keeps for the wrong alternative: throw
   `SushiException`, or a `@pre` with `std::get_if`. Recommended: throw `SushiException`
   when `E` is `Core::Error`, since SPEC 2.5 says the library throws one type.
3. T03, QC-28: `explicit sushi_ptr(T*)`. The worker needs every implicit conversion in the
   tree listed first; that list is a `haiku` exploration before the wave, and its files
   extend T03's set or become their own task.
4. T03, T07, T11, QC-44: removing `SUSHIRUNTIME_API` from `RefCounted`, `UsmRegistry` and
   `Task`. `Task` is polymorphic; dropping the macro moves its vtable into each consumer.
5. T07, QC-30: `release()` of an unrecorded address stops freeing. A caller that hands the
   allocator foreign USM leaks it from then on.
6. T09, QC-39: the new enumerator; SushiEngine must be rebuilt against it.
7. T14, QC-14: const inputs for coupled kernels; SushiBLAS and SushiEngine kernels with
   mutable input pointers stop compiling.
8. T17, QC-12: not in the plan as written. The fix needs `IExecutionEngine::run_in_progress()`,
   a new virtual on an engine interface. If approved it joins T12 (interface) and T17 (caller).
9. T21, QC-37 and QC-38: `KernelRegistry::seal()` is a new public member; refusing zero
   timeouts changes what a valid `ClusterOptions` is.
10. T18, QC-08: either the renumbering becomes incremental (a data-layout change inside
    `RegionState`) or the two documents are corrected to O(total nodes). Recommended: the
    incremental form, since the README's claim is the feature.

## Not repaired, and why

- QC-21, QC-22, QC-23 (`IExecutionEngine`, `Node`, `TaskScheduler`): each is a split of a
  public type across module files, with new headers and build edges. They belong in
  `docs/design/REMAINING_WORK.md` as their own pieces of work.
- QC-02, the part beyond the range check (processor groups, moving `affinity.hpp`'s body to a
  source file), and QC-109 (`EpochManager` out of the header): both need a new `.cpp`, which
  needs a build file this plan may not touch.
- QC-56 (`DeviceIndex` and three more types out of `buffer.hpp`): a new public header; a
  module-boundary choice.
- QC-57 (change listener on `Recorder`), QC-10's structural form (a recording-only type for
  `extend()`), QC-67 (`untracked_kernel` argument order), QC-68 (`scratch()` returning a
  span): each reshapes the recorder that SPEC section 3 fixed and SushiBLAS is about to build
  on. One ruling should cover the four.
- QC-63 (`dynamic_graph.hpp` including `runtime.hpp`): removing it breaks every unit that
  relies on it, for no behaviour gained.
- QC-73 (zero-size `parallel_for`): no backend failure is known; a guard costs a branch per
  submit. Repair when a backend shows it.
- QC-74 (NdRange tiling check): SYCL reports it at dispatch already; a record-time check is
  an improvement, not a defect fix. Backlog.
- QC-85 (`Node::profiling`): deleting a public field; needs a search of the repositories
  above first.
- QC-93, QC-116, QC-136 (naming across modules), QC-103 (`sycl::detail` entry point), QC-113,
  QC-114, QC-122: renames and reshaped interfaces, each costing a deprecation cycle in three
  repositories. The constant spelling needs a ruling first: this phase's own new names are
  split between `K_UPPER_SNAKE` and bare `UPPER_SNAKE`.
- QC-95 (logger join under `FreeLibrary`): unverified; needs a run on a host that unloads
  the library.
- QC-121 (outbox allocation per control frame), QC-117 (ledger cursors): costs with no
  measurement; `performance` asks for the number first.
- QC-126 (handle-kind table): there are two kinds and one per platform; the table earns its
  place with the third.
- QC-94, QC-118 and the register rows not folded into a task above: comment and README
  wording in files no repair task opens. One mechanical `haiku` pass after the last wave,
  once the code under the comments has stopped moving.
- QC-137: the changelog lines are the orchestrator's.

## Models

Per `model-routing`: T04, T08, T22, T23 and the closing register pass are spec-complete and
go to `haiku` at `medium`. T01, T02, T03, T05, T09, T11, T12, T13, T16, T19 go to `haiku` at
`high`. T06, T07, T10, T14, T15, T17, T18, T20, T21 need judgment across files and go to
`opus` at `low`. No task needs `opus` at `high`. A failed task climbs one rung.
