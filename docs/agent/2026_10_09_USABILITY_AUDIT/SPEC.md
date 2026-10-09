# Phase B: many-module layout

**Status:** Open. Step B0 done; the code steps of part 2 run before any file moves.

Three designers read the tree at `9956845` and returned the parts below. Nobody built or
configured anything for them; each part ends with what only a build can confirm. This head
states what the orchestrator took from them and the order the work runs in. The parts are as
returned, with headings demoted.

## Decisions

Taken by the orchestrator under the owner's ruling of 2026-10-09 ("tier table approved as
proposed, subject to what the code shows").

| Decision | Reason |
| --- | --- |
| `observability` sits in the platform tier, not the engine tier | `task`, `scheduler`, `graph` and `execution` all include it and it includes only `core` and `sycl` (part 1, offending edges; part 2, section 5) |
| Engine edges: `scheduler -> task`, `graph -> task`, `execution -> scheduler`, `execution -> graph`, `execution -> task`. Surface edge: `api -> interop` | Part 2, section 5 |
| `CompiledGraph`, `ChainPlan` and `PendingEvent` live in `graph`; `PinPolicy` and `WorkerPoolInfo` in `scheduler`; `RangeTask` stays in `scheduler` | Part 2, sections 3.1 and 3.2 |
| `API::Interop` and `HostExport` move to `api`; `interop` keeps the vocabulary and the three `Detail` functions | Part 2, section 3.4 |
| `Cluster` moves to `distributed` as `Distributed::Cluster::attach(runtime, ...)`; `Runtime::cluster` goes without a forwarder | `api` can no longer name a transport once `distributed` is a pure extension; no downstream repository uses the layer (part 2, section 4) |
| The offload seam (`OffloadCoordinator`, `OffloadSink`, `OffloadState`) is declared in `task`, namespace `Graph`; no header changes shape with `SR_DISTRIBUTED` | Part 2, section 4 |
| Namespaces keep their names in this phase | A rename is mechanical and separate (part 2, top) |
| Forwarding headers are generated from one table into an include root only the product carries, and are silent for consumers | A forwarder inside the module of the old path would be an upward include; a warning would break SushiBLAS under its `-Werror` (part 3, section 4) |
| Include roots are transitive; the checker's `rule_declared_dependencies` closes the gap | Part 3, section 1 |
| The three private headers of part 1 go under `source/`; the other five of CORE-12 stay public or internal | Part 1, item 1 of "could not place" |

## For the owner

1. Presets: part 3 recommends deleting `CMakePresets.json`, since `sr` and CI are the only
   callers of CMake and the three presets went stale unnoticed. Deleting a tracked file the
   orchestrator did not create waits for a yes; nothing else waits on it.
2. SushiBLAS, SushiAI and SushiEngine each need `set(SR_INSTALL ON)` before their
   `add_subdirectory(sushiruntime)`. The line is harmless against today's runtime. Those are
   other repositories; the edit is listed here and made when the owner says so.

## Order

Every step closes with `sr build --distributed`, `sr test` and, where the dispatch loop
changed, the benchmark lines of phase A. Workers do not touch build files; every CMake and CLI
change is the orchestrator's.

| Step | From | What | Runs |
| --- | --- | --- | --- |
| B0 | part 2, step 1 | Dead includes and the unused friend removed | done; 111 translation units pass `-fsyntax-only` |
| B1 | part 2, steps 2a, 2b, 2d, 3a to 3d | `task` headers split out with forwarders at the old paths; `PinPolicy` and `WorkerPoolInfo` to `scheduler`; `API::Interop` and `HostExport` to `api`; the four bricks as new files with unit tests | wave of seven workers |
| B2 | part 2, steps 2c, 3e | `CompiledGraph` and `ChainPlan` to `graph`; the seam headers | wave of two |
| B3 | part 2, steps 4, 5 | `RuntimeContext` on the bricks; scheduler off the context | in sequence |
| B4 | part 2, steps 6, 7, 8 | Offload state, control path and surface on the seam; `Cluster` to `distributed` | in sequence |
| B5 | part 2, step 9 | In-repo callers to the new spellings, then `[[deprecated]]` on the forwarders | wave, split by folder |
| B6 | part 3, steps 1 to 7 | CMake groundwork on the current tree: options file, `SR_SANITIZER`, SYCL flags on targets, install logic, test and module functions, tier table, forwarding table | orchestrator, alongside B1 to B5 |
| B7 | part 3, steps 8, 9; part 1 | The move into `modules/<tier>/<module>/`, lowest tier first, one module per commit; legacy tree deleted; checker rules on | orchestrator, scripted from the move map |
| B8 | part 3, steps 10 to 12 | Hidden visibility, `applications/examples/`, `.config/`, `tests/embed/` | orchestrator and workers |

## Part 1: move map

Read-only. Nothing in the repository was created, touched or deleted. The map covers 362 tracked files: HEAD moved to `9956845` while I worked, which added `PLAN.md`, `tests/common/measurement.hpp` and `test_layer_equivalence.cpp` to the 359 the task started from.

### Method

One throwaway Python script (`b1/move_map.py`, deleted) did three things.

1. **Placement.** `git ls-files` gives the list. A function `place(path)` returns destination, reason code, group and note. The rules in it are explicit tables that I filled by reading each header's `@brief` and include lines, each source's includes, and each test's `@brief`, suite names and includes: `HEADER_MODULE` (14 headers that change module), `PRIVATE` (3 headers), `NUMERIC` (3 headers), `SOURCE_RENAME` (2 sources), `UNIT` and `INTEGRATION` (one entry per test file: a module name or `root`). Everything else follows its folder.
2. **Row check.** The script asserts one row per tracked path and counts the rows of the emitted table.
3. **Edges after the move.** Every `#include` in a file that lands in a module's `include/` or `source/` is resolved to the file it names today (angle form, and the quoted relative form of the umbrella and two scheduler headers), then to that file's destination module. Lines are counted per (including module, included module), self-edges dropped. Module tests are left out of the edge table and checked separately: each must name only its own module, a lower tier, a module reachable over the four approved edges, or the umbrella. An `#ifdef SR_DISTRIBUTED` guard around an include is recorded.

Output of the row check:

```
tracked files: 362
table rows   : 362
by reason    : {'application': 9, 'config': 2, 'delete-candidate': 3, 'docs': 25, 'github': 2, 'module': 102, 'private': 3, 'stays': 135, 'tests': 81}
by group     : {'core': 16, 'logging': 2, 'sycl': 1, 'topology': 14, 'memory': 9, 'device': 0, 'observability': 5, 'task': 6, 'scheduler': 13, 'graph': 7, 'execution': 16, 'interop': 9, 'api': 49, 'distributed': 21, 'root tests': 54, 'applications': 9, 'config': 2, 'github': 8, 'docs': 36, 'cli': 37, 'cmake': 20, 'tools': 13, 'root': 12, 'merged or deleted': 3}
shared destinations (README merges expected):
   2 modules/engine/execution/README.md
   2 modules/engine/graph/README.md
   2 modules/engine/scheduler/README.md
   2 modules/extension/distributed/README.md
   2 modules/foundation/core/README.md
   2 modules/platform/topology/README.md
MODULE TESTS REACHING OUTSIDE WHAT THEIR MODULE MAY INCLUDE
(empty)
```

The test rule I applied is the skill's, which is narrower than the wording in the brief. A test goes to the module whose contract it pins when every header it names by path belongs to that module or to a module it may depend on; the umbrella is allowed. A test that needs a header of a higher module (in practice `execution/runtime_context.hpp` for a scheduler, graph or topology contract) stays under root `tests/`. Read literally, "names headers of more than one module by path" would also send `test_dependency_tracker.cpp` (graph + task) and `test_distributed.cpp` (distributed + task) to the root, and then `dependency_tracker.hpp` could not be private.

### Move map

Abbreviations in the Current column: `I/` = `include/SushiRuntime/`, `TF/` = `tests/functional/`. In a module group the Destination is relative to the module root named in the heading. `=` means the path does not change.

**core** (16), relative to `modules/foundation/core/`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `I/core/README.md` | `README.md` | docs | merge target |
| `src/core/README.md` | `README.md` | docs | merges into the module README |
| `I/core/bit.hpp` | `include/SushiRuntime/core/bit.hpp` | module | |
| `I/core/common.hpp` | `include/SushiRuntime/core/common.hpp` | module | |
| `I/core/delegate.hpp` | `include/SushiRuntime/core/delegate.hpp` | module | |
| `I/core/error.hpp` | `include/SushiRuntime/core/error.hpp` | module | |
| `I/core/export.hpp` | `include/SushiRuntime/core/export.hpp` | module | |
| `I/core/ref_counted.hpp` | `include/SushiRuntime/core/ref_counted.hpp` | module | |
| `I/core/result.hpp` | `include/SushiRuntime/core/result.hpp` | module | |
| `I/core/span.hpp` | `include/SushiRuntime/core/span.hpp` | module | |
| `I/core/strong_id.hpp` | `include/SushiRuntime/core/strong_id.hpp` | module | |
| `I/core/sushi_ptr.hpp` | `include/SushiRuntime/core/sushi_ptr.hpp` | module | |
| `I/version.hpp` | `include/SushiRuntime/core/version.hpp` | module | forwarder at old path |
| `src/core/version.cpp` | `source/version.cpp` | module | |
| `TF/unit/test_delegate.cpp` | `tests/test_delegate.cpp` | tests | |
| `TF/unit/test_ref_counted.cpp` | `tests/test_ref_counted.cpp` | tests | |

**logging** (2), relative to `modules/foundation/logging/`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `I/core/logger.hpp` | `include/SushiRuntime/logging/logger.hpp` | module | forwarder at old path |
| `TF/unit/test_logger.cpp` | `tests/test_logger.cpp` | tests | |

**sycl** (1), relative to `modules/foundation/sycl/`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `I/core/sycl_compat.hpp` | `include/SushiRuntime/sycl/sycl_compat.hpp` | module | forwarder at old path |

**topology** (14), relative to `modules/platform/topology/`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `I/topology/README.md` | `README.md` | docs | merge target |
| `src/topology/README.md` | `README.md` | docs | merges into the module README |
| `I/topology/affinity.hpp` | `include/SushiRuntime/topology/affinity.hpp` | module | |
| `I/topology/hw_discovery.hpp` | `include/SushiRuntime/topology/hw_discovery.hpp` | module | |
| `I/topology/hwloc_topology_provider.hpp` | `include/SushiRuntime/topology/hwloc_topology_provider.hpp` | module | |
| `I/topology/i_topology_provider.hpp` | `include/SushiRuntime/topology/i_topology_provider.hpp` | module | |
| `I/topology/matrix_distance_model.hpp` | `include/SushiRuntime/topology/matrix_distance_model.hpp` | module | |
| `I/topology/numa_mapper.hpp` | `include/SushiRuntime/topology/numa_mapper.hpp` | module | |
| `I/topology/remapping_topology_provider.hpp` | `include/SushiRuntime/topology/remapping_topology_provider.hpp` | module | |
| `src/topology/hw_discovery.cpp` | `source/hw_discovery.cpp` | module | |
| `src/topology/hwloc_topology_provider.cpp` | `source/hwloc_topology_provider.cpp` | module | |
| `src/topology/numa_mapper.cpp` | `source/numa_mapper.cpp` | module | |
| `src/topology/remapping_topology_provider.cpp` | `source/remapping_topology_provider.cpp` | module | |
| `TF/unit/test_affinity.cpp` | `tests/test_affinity.cpp` | tests | |

**memory** (9), relative to `modules/platform/memory/`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `I/memory/README.md` | `README.md` | docs | merge target |
| `I/memory/allocation.hpp` | `include/SushiRuntime/memory/allocation.hpp` | module | |
| `I/memory/epoch_reclamation.hpp` | `include/SushiRuntime/memory/epoch_reclamation.hpp` | module | not private, see below |
| `I/core/resource_liveness.hpp` | `include/SushiRuntime/memory/resource_liveness.hpp` | module | forwarder at old path |
| `I/core/resource_region.hpp` | `include/SushiRuntime/memory/resource_region.hpp` | module | forwarder at old path |
| `I/memory/usm_allocation_owner.hpp` | `include/SushiRuntime/memory/usm_allocation_owner.hpp` | module | flagged: includes `RuntimeContext` |
| `I/memory/usm_registry.hpp` | `include/SushiRuntime/memory/usm_registry.hpp` | module | |
| `TF/unit/test_epoch_reclamation.cpp` | `tests/test_epoch_reclamation.cpp` | tests | |
| `TF/unit/test_usm_registry.cpp` | `tests/test_usm_registry.cpp` | tests | |

**device** (0), `modules/platform/device/`: no tracked file lands here. Its content is cut out of `runtime_context.{hpp,cpp}` by the cycle designer.

**observability** (5), relative to `modules/engine/observability/`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `I/core/dispatch_width.hpp` | `include/SushiRuntime/observability/dispatch_width.hpp` | module | forwarder at old path |
| `I/core/event_timestamps.hpp` | `include/SushiRuntime/observability/event_timestamps.hpp` | module | forwarder at old path |
| `I/core/profiling_ledger.hpp` | `include/SushiRuntime/observability/profiling_ledger.hpp` | module | forwarder at old path |
| `I/core/run_report.hpp` | `include/SushiRuntime/observability/run_report.hpp` | module | forwarder at old path |
| `TF/unit/test_node_event_record.cpp` | `tests/test_node_event_record.cpp` | tests | |

**task** (6), relative to `modules/engine/task/`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `I/graph/late_bindings.hpp` | `include/SushiRuntime/task/late_bindings.hpp` | module | forwarder at old path |
| `I/graph/node.hpp` | `include/SushiRuntime/task/node.hpp` | module | forwarder at old path; split owned by the cycle designer |
| `I/scheduler/ready_sink.hpp` | `include/SushiRuntime/task/ready_sink.hpp` | module | forwarder at old path; `task.hpp:39` needs it |
| `I/scheduler/task.hpp` | `include/SushiRuntime/task/task.hpp` | module | forwarder at old path |
| `I/graph/task_types.hpp` | `include/SushiRuntime/task/task_types.hpp` | module | forwarder at old path |
| `TF/unit/test_range_task.cpp` | `tests/test_range_task.cpp` | tests | |

**scheduler** (13), relative to `modules/engine/scheduler/`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `I/scheduler/README.md` | `README.md` | docs | merge target |
| `src/scheduler/README.md` | `README.md` | docs | merges into the module README |
| `I/scheduler/event_polling.hpp` | `include/SushiRuntime/scheduler/event_polling.hpp` | module | rule 1 does not apply, see below |
| `I/scheduler/rebalancer_interfaces.hpp` | `include/SushiRuntime/scheduler/rebalancer_interfaces.hpp` | module | same |
| `I/scheduler/task_scheduler.hpp` | `include/SushiRuntime/scheduler/task_scheduler.hpp` | module | same; split owned by the cycle designer |
| `I/scheduler/thermal_proxy_strategy.hpp` | `include/SushiRuntime/scheduler/thermal_proxy_strategy.hpp` | module | same |
| `I/scheduler/workload_coordinator.hpp` | `include/SushiRuntime/scheduler/workload_coordinator.hpp` | module | same |
| `src/scheduler/scheduler.cpp` | `source/task_scheduler.cpp` | module | renamed after its header |
| `I/scheduler/work_stealing_deque.hpp` | `source/work_stealing_deque.hpp` | private | |
| `TF/unit/test_event_polling.cpp` | `tests/test_event_polling.cpp` | tests | |
| `TF/unit/test_thermal_rebalance.cpp` | `tests/test_thermal_rebalance.cpp` | tests | |
| `TF/unit/test_work_stealing_deque.cpp` | `tests/test_work_stealing_deque.cpp` | tests | needs the private include path |
| `TF/integration/test_workload_coordinator.cpp` | `tests/test_workload_coordinator.cpp` | tests | |

**graph** (7), relative to `modules/engine/graph/`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `I/graph/README.md` | `README.md` | docs | merge target |
| `src/graph/README.md` | `README.md` | docs | merges into the module README |
| `I/graph/task_graph.hpp` | `include/SushiRuntime/graph/task_graph.hpp` | module | |
| `src/graph/dependency_tracker.cpp` | `source/dependency_tracker.cpp` | module | |
| `I/graph/dependency_tracker.hpp` | `source/dependency_tracker.hpp` | private | |
| `src/graph/task_graph.cpp` | `source/task_graph.cpp` | module | |
| `TF/unit/test_dependency_tracker.cpp` | `tests/test_dependency_tracker.cpp` | tests | needs the private include path |

**execution** (16), relative to `modules/engine/execution/`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `I/execution/README.md` | `README.md` | docs | merge target |
| `src/execution/README.md` | `README.md` | docs | merges into the module README |
| `I/execution/chain_plan.hpp` | `include/SushiRuntime/execution/chain_plan.hpp` | module | |
| `I/execution/chain_run.hpp` | `include/SushiRuntime/execution/chain_run.hpp` | module | |
| `I/execution/execution_engine.hpp` | `include/SushiRuntime/execution/execution_engine.hpp` | module | |
| `I/execution/runtime_context.hpp` | `include/SushiRuntime/execution/runtime_context.hpp` | module | placed whole; split owned by the cycle designer |
| `I/execution/runtime_settings.hpp` | `include/SushiRuntime/execution/runtime_settings.hpp` | module | placed whole; `task_scheduler.hpp:49` includes it |
| `src/execution/chain_plan.cpp` | `source/chain_plan.cpp` | module | |
| `src/execution/chain_run.cpp` | `source/chain_run.cpp` | module | |
| `src/execution/execution_engine.cpp` | `source/execution_engine.cpp` | module | |
| `src/execution/runtime_context.cpp` | `source/runtime_context.cpp` | module | placed whole |
| `TF/integration/test_async_execution.cpp` | `tests/test_async_execution.cpp` | tests | |
| `TF/integration/test_device_pipeline.cpp` | `tests/test_device_pipeline.cpp` | tests | |
| `TF/integration/test_heterogeneous_pipeline.cpp` | `tests/test_heterogeneous_pipeline.cpp` | tests | |
| `TF/integration/test_profiling_queue.cpp` | `tests/test_profiling_queue.cpp` | tests | |
| `TF/integration/test_sycl_execution.cpp` | `tests/test_sycl_execution.cpp` | tests | |

**interop** (9), relative to `modules/surface/interop/`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `src/interop/README.md` | `README.md` | docs | becomes the module README (no include-side page exists) |
| `I/interop/device_uuid.hpp` | `include/SushiRuntime/interop/device_uuid.hpp` | module | |
| `I/interop/external_memory.hpp` | `include/SushiRuntime/interop/external_memory.hpp` | module | |
| `I/interop/host_export.hpp` | `include/SushiRuntime/interop/host_export.hpp` | module | flagged: holds `API::Buffer<T>` |
| `I/interop/interop.hpp` | `include/SushiRuntime/interop/interop.hpp` | module | flagged: declares `API::Interop` |
| `I/interop/interop_capabilities.hpp` | `include/SushiRuntime/interop/interop_capabilities.hpp` | module | |
| `I/interop/detail/bindless_detection.hpp` | `source/bindless_detection.hpp` | private | |
| `src/interop/interop.cpp` | `source/interop.cpp` | module | |
| `TF/unit/test_interop_vocabulary.cpp` | `tests/test_interop_vocabulary.cpp` | tests | |

**api** (49), relative to `modules/surface/api/`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `I/api/README.md` | `README.md` | docs | merge target |
| `I/SushiRuntime.h` | `include/SushiRuntime/SushiRuntime.h` | module | umbrella; flagged |
| `I/api/data/README.md` | `include/SushiRuntime/api/data/README.md` | docs | stays beside its headers |
| `I/api/data/buffer.hpp` | `include/SushiRuntime/api/data/buffer.hpp` | module | |
| `I/api/data/state.hpp` | `include/SushiRuntime/api/data/state.hpp` | module | |
| `I/api/detail/README.md` | `include/SushiRuntime/api/detail/README.md` | docs | stays beside its headers |
| `I/api/detail/callable_traits.hpp` | `include/SushiRuntime/api/detail/callable_traits.hpp` | module | |
| `I/api/detail/device_fault.hpp` | `include/SushiRuntime/api/detail/device_fault.hpp` | module | |
| `I/api/detail/run_report_builder.hpp` | `include/SushiRuntime/api/detail/run_report_builder.hpp` | module | |
| `I/api/detail/step_machinery.hpp` | `include/SushiRuntime/api/detail/step_machinery.hpp` | module | |
| `I/api/fluent.hpp` | `include/SushiRuntime/api/fluent.hpp` | module | |
| `I/api/graph/README.md` | `include/SushiRuntime/api/graph/README.md` | docs | stays beside its headers |
| `I/api/graph/dynamic_graph.hpp` | `include/SushiRuntime/api/graph/dynamic_graph.hpp` | module | |
| `I/api/graph/graph.hpp` | `include/SushiRuntime/api/graph/graph.hpp` | module | holds the four primitives; cannot be lifted by file |
| `I/api/graph/region.hpp` | `include/SushiRuntime/api/graph/region.hpp` | module | |
| `I/api/graph/run_handle.hpp` | `include/SushiRuntime/api/graph/run_handle.hpp` | module | |
| `I/api/launch/README.md` | `include/SushiRuntime/api/launch/README.md` | docs | stays beside its headers |
| `I/api/launch/extent.hpp` | `include/SushiRuntime/api/launch/extent.hpp` | module | |
| `I/api/launch/nd_range.hpp` | `include/SushiRuntime/api/launch/nd_range.hpp` | module | |
| `I/api/vocabulary/combine.hpp` | `include/SushiRuntime/api/numeric/combine.hpp` | module | leaves-later |
| `I/api/detail/fixed_order_reduce.hpp` | `include/SushiRuntime/api/numeric/fixed_order_reduce.hpp` | module | leaves-later |
| `I/api/detail/fixed_order_scan.hpp` | `include/SushiRuntime/api/numeric/fixed_order_scan.hpp` | module | leaves-later |
| `I/api/runtime/README.md` | `include/SushiRuntime/api/runtime/README.md` | docs | stays beside its headers |
| `I/api/runtime/cluster.hpp` | `include/SushiRuntime/api/runtime/cluster.hpp` | module | |
| `I/api/runtime/runtime.hpp` | `include/SushiRuntime/api/runtime/runtime.hpp` | module | |
| `I/api/vocabulary/README.md` | `include/SushiRuntime/api/vocabulary/README.md` | docs | stays beside its headers |
| `I/api/vocabulary/captures.hpp` | `include/SushiRuntime/api/vocabulary/captures.hpp` | module | |
| `I/api/vocabulary/dependencies.hpp` | `include/SushiRuntime/api/vocabulary/dependencies.hpp` | module | |
| `I/api/vocabulary/dynamic.hpp` | `include/SushiRuntime/api/vocabulary/dynamic.hpp` | module | |
| `TF/integration/test_counting_sort.cpp` | `tests/numeric/test_counting_sort.cpp` | tests | leaves-later |
| `TF/integration/test_device_sized_primitives.cpp` | `tests/numeric/test_device_sized_primitives.cpp` | tests | leaves-later |
| `TF/unit/test_fixed_order_reduce.cpp` | `tests/numeric/test_fixed_order_reduce.cpp` | tests | leaves-later |
| `TF/unit/test_late_count.cpp` | `tests/numeric/test_late_count.cpp` | tests | leaves-later; 6 of 10 cases are `LateCount` and stay |
| `TF/integration/test_late_count_ordering.cpp` | `tests/numeric/test_late_count_ordering.cpp` | tests | leaves-later; the ordering half is rewritten on a trivial kernel |
| `TF/integration/test_prefix_scan.cpp` | `tests/numeric/test_prefix_scan.cpp` | tests | leaves-later |
| `TF/integration/test_reduce_dispatch_width.cpp` | `tests/numeric/test_reduce_dispatch_width.cpp` | tests | leaves-later |
| `TF/integration/test_async_step.cpp` | `tests/test_async_step.cpp` | tests | |
| `TF/integration/test_device_count.cpp` | `tests/test_device_count.cpp` | tests | |
| `TF/integration/test_device_resident.cpp` | `tests/test_device_resident.cpp` | tests | |
| `TF/integration/test_dynamic_graph.cpp` | `tests/test_dynamic_graph.cpp` | tests | |
| `TF/integration/test_dynamic_graph_node_events.cpp` | `tests/test_dynamic_graph_node_events.cpp` | tests | |
| `TF/integration/test_fluent_api.cpp` | `tests/test_fluent_api.cpp` | tests | |
| `TF/integration/test_fluent_hardening.cpp` | `tests/test_fluent_hardening.cpp` | tests | |
| `TF/integration/test_fluent_lifecycle.cpp` | `tests/test_fluent_lifecycle.cpp` | tests | |
| `TF/integration/test_late_bindings.cpp` | `tests/test_late_bindings.cpp` | tests | |
| `TF/integration/test_native_cpu_path.cpp` | `tests/test_native_cpu_path.cpp` | tests | |
| `TF/integration/test_region_emitter.cpp` | `tests/test_region_emitter.cpp` | tests | |
| `TF/integration/test_rule_registration.cpp` | `tests/test_rule_registration.cpp` | tests | leaves-later (part): line 84 uses `add_prefix_scan` |
| `TF/integration/test_run_report_reuse.cpp` | `tests/test_run_report_reuse.cpp` | tests | |

**distributed** (21), relative to `modules/extension/distributed/`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `I/distributed/README.md` | `README.md` | docs | merge target |
| `src/distributed/README.md` | `README.md` | docs | merges into the module README |
| `I/distributed/buffer_registry.hpp` | `include/SushiRuntime/distributed/buffer_registry.hpp` | module | |
| `I/distributed/distributed_coordinator.hpp` | `include/SushiRuntime/distributed/distributed_coordinator.hpp` | module | |
| `I/distributed/i_offload_policy.hpp` | `include/SushiRuntime/distributed/i_offload_policy.hpp` | module | |
| `I/distributed/i_transport.hpp` | `include/SushiRuntime/distributed/i_transport.hpp` | module | |
| `I/distributed/in_process_transport.hpp` | `include/SushiRuntime/distributed/in_process_transport.hpp` | module | |
| `I/distributed/kernel_registry.hpp` | `include/SushiRuntime/distributed/kernel_registry.hpp` | module | |
| `I/distributed/protocol.hpp` | `include/SushiRuntime/distributed/protocol.hpp` | module | |
| `I/distributed/tcp_transport.hpp` | `include/SushiRuntime/distributed/tcp_transport.hpp` | module | |
| `I/distributed/worker_agent.hpp` | `include/SushiRuntime/distributed/worker_agent.hpp` | module | |
| `src/distributed/buffer_registry.cpp` | `source/buffer_registry.cpp` | module | |
| `src/distributed/distributed_coordinator.cpp` | `source/distributed_coordinator.cpp` | module | |
| `src/distributed/offload_policy.cpp` | `source/i_offload_policy.cpp` | module | renamed after its header |
| `src/distributed/in_process_transport.cpp` | `source/in_process_transport.cpp` | module | |
| `src/distributed/kernel_registry.cpp` | `source/kernel_registry.cpp` | module | |
| `src/distributed/tcp_transport.cpp` | `source/tcp_transport.cpp` | module | |
| `src/distributed/worker_agent.cpp` | `source/worker_agent.cpp` | module | |
| `TF/unit/test_distributed.cpp` | `tests/test_distributed.cpp` | tests | |
| `TF/integration/test_distributed_e2e.cpp` | `tests/test_distributed_e2e.cpp` | tests | |
| `TF/integration/test_distributed_jepsen.cpp` | `tests/test_distributed_jepsen.cpp` | tests | |

**root tests** (54)

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `tests/CMakeLists.txt` | `=` | stays | |
| `TF/README.md` | `tests/README.md` | docs | |
| `TF/integration/test_steady_state_allocation.cpp` | `tests/allocation/test_steady_state_allocation.cpp` | tests | own program (`sr_allocation_tests` replaces `operator new`) |
| `tests/benchmark/README.md` | `=` | stays | |
| `tests/benchmark/performance/test_async_overlap_perf.cpp` | `tests/benchmark/benchmark_async_overlap.cpp` | tests | renamed |
| `tests/benchmark/performance/test_chain_dispatch_perf.cpp` | `tests/benchmark/benchmark_chain_dispatch.cpp` | tests | renamed |
| `tests/benchmark/performance/test_distributed_offload_perf.cpp` | `tests/benchmark/benchmark_distributed_offload.cpp` | tests | renamed |
| `tests/benchmark/performance/test_dynamic_graph_perf.cpp` | `tests/benchmark/benchmark_dynamic_graph.cpp` | tests | renamed |
| `tests/benchmark/stress/test_epoch_stress.cpp` | `tests/benchmark/benchmark_epoch_stress.cpp` | tests | renamed; flagged |
| `tests/benchmark/performance/test_graph_compile_perf.cpp` | `tests/benchmark/benchmark_graph_compile.cpp` | tests | renamed |
| `tests/benchmark/performance/test_profiling_queue_perf.cpp` | `tests/benchmark/benchmark_profiling_queue.cpp` | tests | renamed |
| `tests/benchmark/performance/test_recompose_churn_perf.cpp` | `tests/benchmark/benchmark_recompose_churn.cpp` | tests | renamed |
| `tests/benchmark/performance/test_root_fanout_perf.cpp` | `tests/benchmark/benchmark_root_fanout.cpp` | tests | renamed |
| `tests/benchmark/performance/test_scheduler_dispatch_perf.cpp` | `tests/benchmark/benchmark_scheduler_dispatch.cpp` | tests | renamed |
| `tests/benchmark/stress/test_scheduler_stress.cpp` | `tests/benchmark/benchmark_scheduler_stress.cpp` | tests | renamed; flagged |
| `tests/benchmark/performance/test_steal_locality_perf.cpp` | `tests/benchmark/benchmark_steal_locality.cpp` | tests | renamed |
| `tests/benchmark/performance/test_steal_numa_effect_perf.cpp` | `tests/benchmark/benchmark_steal_numa_effect.cpp` | tests | renamed |
| `tests/benchmark/performance/test_wide_fanout_perf.cpp` | `tests/benchmark/benchmark_wide_fanout.cpp` | tests | renamed |
| `tests/common/README.md` | `=` | stays | |
| `tests/common/measurement.hpp` | `=` | stays | |
| `tests/common/test_barrier.hpp` | `=` | stays | no test includes it (TEST-11) |
| `tests/common/test_helpers.hpp` | `=` | stays | includes two `task` headers |
| `tests/common/test_main.cpp` | `=` | stays | |
| `TF/integration/README.md` | `tests/integration/README.md` | docs | sections of moved tests go to module `tests/README.md` |
| `TF/integration/test_cancellation.cpp` | `tests/integration/test_cancellation.cpp` | tests | |
| `TF/integration/test_chain_layer.cpp` | `tests/integration/test_chain_layer.cpp` | tests | |
| `TF/integration/test_co_tenancy.cpp` | `tests/integration/test_co_tenancy.cpp` | tests | |
| `TF/integration/test_completion_latch.cpp` | `tests/integration/test_completion_latch.cpp` | tests | |
| `TF/integration/test_cross_context_guard.cpp` | `tests/integration/test_cross_context_guard.cpp` | tests | |
| `TF/integration/test_determinism.cpp` | `tests/integration/test_determinism.cpp` | tests | leaves-later (part): lines 103, 104, 233 use `add_reduce` |
| `TF/integration/test_device_selection.cpp` | `tests/integration/test_device_selection.cpp` | tests | |
| `TF/integration/test_error_isolation.cpp` | `tests/integration/test_error_isolation.cpp` | tests | |
| `TF/integration/test_graph.cpp` | `tests/integration/test_graph.cpp` | tests | TEST-05 calls it a facade duplicate |
| `TF/integration/test_graph_profiling_switch.cpp` | `tests/integration/test_graph_profiling_switch.cpp` | tests | |
| `TF/integration/test_interop_host_export.cpp` | `tests/integration/test_interop_host_export.cpp` | tests | |
| `TF/integration/test_layer_equivalence.cpp` | `tests/integration/test_layer_equivalence.cpp` | tests | |
| `TF/integration/test_node_event_records.cpp` | `tests/integration/test_node_event_records.cpp` | tests | |
| `TF/integration/test_observability.cpp` | `tests/integration/test_observability.cpp` | tests | |
| `TF/integration/test_perf_controls.cpp` | `tests/integration/test_perf_controls.cpp` | tests | |
| `TF/integration/test_scheduler_integration.cpp` | `tests/integration/test_scheduler_integration.cpp` | tests | |
| `tests/lsan_suppressions.txt` | `=` | stays | `cli/sushiruntime/env.py:131` passes its path; a `.config/` candidate |
| `tests/package/CMakeLists.txt` | `=` | stays | |
| `tests/package/README.md` | `=` | stays | |
| `tests/package/main.cpp` | `=` | stays | |
| `TF/regression/README.md` | `tests/regression/README.md` | docs | |
| `TF/regression/test_regression.cpp` | `tests/regression/test_regression.cpp` | tests | |
| `TF/unit/README.md` | `tests/unit/README.md` | docs | sections of moved tests go to module `tests/README.md` |
| `TF/unit/test_allocation.cpp` | `tests/unit/test_allocation.cpp` | tests | |
| `TF/unit/test_node_dispatch_width.cpp` | `tests/unit/test_node_dispatch_width.cpp` | tests | |
| `TF/unit/test_numa_steal.cpp` | `tests/unit/test_numa_steal.cpp` | tests | |
| `TF/unit/test_nvidia_topology.cpp` | `tests/unit/test_nvidia_topology.cpp` | tests | |
| `TF/unit/test_registration_index.cpp` | `tests/unit/test_registration_index.cpp` | tests | leaves-later (part): 3 of 5 cases use `add_reduce` |
| `TF/unit/test_topology.cpp` | `tests/unit/test_topology.cpp` | tests | |
| `TF/unit/test_topology_abstraction.cpp` | `tests/unit/test_topology_abstraction.cpp` | tests | |

**applications** (9)

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `examples/CMakeLists.txt` | `applications/examples/CMakeLists.txt` | application | |
| `examples/README.md` | `applications/examples/README.md` | application | |
| `examples/coupled_fields_demo.cpp` | `applications/examples/coupled_fields_demo.cpp` | application | |
| `examples/distributed_demo.cpp` | `applications/examples/distributed_demo.cpp` | application | includes 8 internal headers by path |
| `examples/game_engine_demo.cpp` | `applications/examples/game_engine_demo.cpp` | application | |
| `examples/quickstart.cpp` | `applications/examples/quickstart.cpp` | application | |
| `examples/stencil_demo.cpp` | `applications/examples/stencil_demo.cpp` | application | |
| `examples/streaming_world_demo.cpp` | `applications/examples/streaming_world_demo.cpp` | application | |
| `examples/vecadd_demo.cpp` | `applications/examples/vecadd_demo.cpp` | application | |

**config** (2)

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `Dockerfile` | `.config/docker/Dockerfile` | config | |
| `Doxyfile` | `.config/doxygen/Doxyfile` | config | |

**github** (8)

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `docs/reference/CODE_OF_CONDUCT.md` | `.github/CODE_OF_CONDUCT.md` | github | |
| `docs/reference/SECURITY.md` | `.github/SECURITY.md` | github | |
| `.github/ISSUE_TEMPLATE/bug_report.yml` | `=` | stays | |
| `.github/ISSUE_TEMPLATE/feature_request.yml` | `=` | stays | |
| `.github/PULL_REQUEST_TEMPLATE.md` | `=` | stays | |
| `.github/actions/setup-oneapi/action.yml` | `=` | stays | |
| `.github/workflows/ci.yml` | `=` | stays | |
| `.github/workflows/release.yml` | `=` | stays | |

**docs** (36), all `stays`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `docs/CONTRIBUTING.md` | `=` | stays | |
| `docs/DOCUMENTATION_STYLE_GUIDE.md` | `=` | stays | |
| `docs/README.md` | `=` | stays | |
| `docs/agent/2026_10_05_ESTATE_AUDIT/REPORT.md` | `=` | stays | |
| `docs/agent/2026_10_09_USABILITY_AUDIT/PLAN.md` | `=` | stays | |
| `docs/agent/2026_10_09_USABILITY_AUDIT/REPORT.md` | `=` | stays | |
| `docs/architecture/ARCHITECTURE.md` | `=` | stays | |
| `docs/archive/agent/reports/2026-09-22-help-screen-task-26.md` | `=` | stays | |
| `docs/archive/agent/reports/2026-09-22-wave-1-task-7a.md` | `=` | stays | |
| `docs/archive/changelog/v0.2.0.md` | `=` | stays | |
| `docs/archive/changelog/v0.3.0_LONG_FORM.md` | `=` | stays | |
| `docs/archive/slop/BACKBONE_WAVE_STATUS.md` | `=` | stays | |
| `docs/archive/slop/CODE_AND_ARCHITECTURE_REVIEW.md` | `=` | stays | |
| `docs/archive/slop/PHYSICS_SUBSTRATE_IMPLEMENTATION.md` | `=` | stays | |
| `docs/archive/slop/TODO.md` | `=` | stays | |
| `docs/archive/superpowers/specs/2026-08-18-usm-allocation-seam-design.md` | `=` | stays | |
| `docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md` | `=` | stays | |
| `docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-plan.md` | `=` | stays | |
| `docs/archive/superpowers/specs/2026-09-16-scan-and-counting-sort-design.md` | `=` | stays | |
| `docs/archive/superpowers/specs/2026-09-16-scan-and-counting-sort-plan.md` | `=` | stays | |
| `docs/design/ENGINE_BACKBONE_REFACTOR.md` | `=` | stays | |
| `docs/design/GRAPH_REPLAY.md` | `=` | stays | |
| `docs/design/PHYSICS_SUBSTRATE_REQUIREMENTS.md` | `=` | stays | |
| `docs/design/README.md` | `=` | stays | |
| `docs/design/REMAINING_WORK.md` | `=` | stays | |
| `docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md` | `=` | stays | |
| `docs/getting_started/INTRODUCTION.md` | `=` | stays | |
| `docs/guides/CLI_GUIDE.md` | `=` | stays | |
| `docs/guides/DISTRIBUTED_GUIDE.md` | `=` | stays | |
| `docs/guides/FAQ.md` | `=` | stays | |
| `docs/guides/INTEGRATION.md` | `=` | stays | |
| `docs/publish.toml` | `=` | stays | |
| `docs/reference/API_MAINPAGE.md` | `=` | stays | |
| `docs/reference/CHANGELOG.md` | `=` | stays | |
| `docs/reference/GLOSSARY.md` | `=` | stays | |
| `docs/reference/KNOWN_ISSUES.md` | `=` | stays | |

**cli** (37), all `stays`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `cli/README.md` | `=` | stays | |
| `cli/config.toml` | `=` | stays | |
| `cli/pyproject.toml` | `=` | stays | |
| `cli/sushiruntime/__init__.py` | `=` | stays | |
| `cli/sushiruntime/__main__.py` | `=` | stays | |
| `cli/sushiruntime/aliases.py` | `=` | stays | |
| `cli/sushiruntime/cli.py` | `=` | stays | |
| `cli/sushiruntime/config.py` | `=` | stays | |
| `cli/sushiruntime/console.py` | `=` | stays | |
| `cli/sushiruntime/env.py` | `=` | stays | |
| `cli/sushiruntime/py.typed` | `=` | stays | |
| `cli/sushiruntime/services/__init__.py` | `=` | stays | |
| `cli/sushiruntime/services/diag.py` | `=` | stays | |
| `cli/sushiruntime/services/discovery.py` | `=` | stays | |
| `cli/sushiruntime/services/docker.py` | `=` | stays | |
| `cli/sushiruntime/services/install_hint.py` | `=` | stays | |
| `cli/sushiruntime/services/packaging.py` | `=` | stays | |
| `cli/sushiruntime/services/project.py` | `=` | stays | |
| `cli/sushiruntime/services/select.py` | `=` | stays | |
| `cli/sushiruntime/services/stage_tree.py` | `=` | stays | |
| `cli/sushistack.deps.toml` | `=` | stays | |
| `cli/tests/conftest.py` | `=` | stays | |
| `cli/tests/test_aliases.py` | `=` | stays | |
| `cli/tests/test_cli.py` | `=` | stays | |
| `cli/tests/test_config.py` | `=` | stays | |
| `cli/tests/test_console_lazy.py` | `=` | stays | |
| `cli/tests/test_docker.py` | `=` | stays | |
| `cli/tests/test_gpu_backend_delegation.py` | `=` | stays | |
| `cli/tests/test_gpu_backend_resolution.py` | `=` | stays | |
| `cli/tests/test_gpu_probe_paths.py` | `=` | stays | |
| `cli/tests/test_help_screen.py` | `=` | stays | |
| `cli/tests/test_install_hint.py` | `=` | stays | |
| `cli/tests/test_packaging.py` | `=` | stays | |
| `cli/tests/test_provision_commands.py` | `=` | stays | |
| `cli/tests/test_stage_tree.py` | `=` | stays | |
| `cli/tests/test_thread_sanitizer_applies.py` | `=` | stays | |
| `cli/tests/test_toolchain_selection.py` | `=` | stays | |

**cmake** (20), all `stays` (contents are the CMake designer's)

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `cmake/BuildPolicy.cmake` | `=` | stays | |
| `cmake/CompilerWarnings.cmake` | `=` | stays | |
| `cmake/Dependencies.cmake` | `=` | stays | |
| `cmake/Determinism.cmake` | `=` | stays | |
| `cmake/Optimization.cmake` | `=` | stays | |
| `cmake/Sanitizers.cmake` | `=` | stays | |
| `cmake/SushiRuntimeConfig.cmake.in` | `=` | stays | |
| `cmake/SushiRuntimeInstall.cmake` | `=` | stays | |
| `cmake/SushiRuntimeProjectOptions.cmake` | `=` | stays | |
| `cmake/SushiRuntimeVcpkg.cmake` | `=` | stays | |
| `cmake/Sycl.cmake` | `=` | stays | |
| `cmake/gpu/CpuFallback.cmake` | `=` | stays | |
| `cmake/gpu/Cuda.cmake` | `=` | stays | |
| `cmake/gpu/DetectGpu.cmake` | `=` | stays | |
| `cmake/gpu/GpuBackend.cmake` | `=` | stays | |
| `cmake/gpu/IntelGpu.cmake` | `=` | stays | |
| `cmake/gpu/Rocm.cmake` | `=` | stays | |
| `cmake/gpu/locate/CudaToolkit.cmake` | `=` | stays | |
| `cmake/gpu/locate/IntelToolkit.cmake` | `=` | stays | |
| `cmake/gpu/locate/RocmToolkit.cmake` | `=` | stays | |

**tools** (13), all `stays`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `tools/README.md` | `=` | stays | |
| `tools/common/__init__.py` | `=` | stays | |
| `tools/common/checker.py` | `=` | stays | |
| `tools/documentation/check_changelog.py` | `=` | stays | |
| `tools/documentation/check_docs_layout.py` | `=` | stays | |
| `tools/documentation/check_source_comments.py` | `=` | stays | |
| `tools/layering/check_layering.py` | `=` | stays | |
| `tools/licensing/write_license_block.py` | `=` | stays | |
| `tools/packaging/README.md` | `=` | stays | |
| `tools/packaging/verify_installed_package.py` | `=` | stays | |
| `tools/tests/test_check_docs_layout.py` | `=` | stays | |
| `tools/tests/test_check_source_comments.py` | `=` | stays | |
| `tools/tests/test_write_license_block.py` | `=` | stays | |

**root** (12), all `stays`

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `.dockerignore` | `=` | stays | Docker reads it from the context root; see call sites |
| `.gitattributes` | `=` | stays | |
| `.gitignore` | `=` | stays | |
| `AGENTS.md` | `=` | stays | |
| `CLAUDE.md` | `=` | stays | |
| `CMakeLists.txt` | `=` | stays | |
| `CMakePresets.json` | `=` | stays | CMake reads it only from the source root |
| `COMMERCIAL.md` | `=` | stays | |
| `LICENSE` | `=` | stays | |
| `NOTICE.md` | `=` | stays | |
| `README.md` | `=` | stays | |
| `sushi-module.toml` | `=` | stays | |

**merged or deleted** (3)

| Current | Destination | Reason | Note |
| --- | --- | --- | --- |
| `src/README.md` | none | delete-candidate | a 20-line folder table; the tier table in `cmake/` and ARCHITECTURE §2 replace it |
| `src/CMakeLists.txt` | one `CMakeLists.txt` per module | delete-candidate | CMake designer |
| `I/README.md` | `modules/foundation/core/README.md` and `modules/surface/api/README.md` | delete-candidate | its `version.hpp` section goes to core, its umbrella section to api, its folder table goes away |

#### Test contracts (TEST-05)

TEST-05 names twelve files and "8 unit files" without listing them, and its total of 23 does not add up from that. The table covers every test that names a non-umbrella header, which is a superset.

| Test | Contract of | Placed | Why there |
| --- | --- | --- | --- |
| `integration/test_graph.cpp` | graph (`TaskGraph` ordering) | root integration | needs `execution/runtime_context.hpp`, a module above graph; TEST-05 calls it a duplicate of `test_fluent_api` and `test_dependency_tracker` |
| `integration/test_sycl_execution.cpp` | execution (a compiled graph runs end to end) | execution | names execution, graph, task |
| `integration/test_device_pipeline.cpp` | execution (`get_execution_engine().execute(compiled)`, line 129) | execution | same |
| `integration/test_heterogeneous_pipeline.cpp` | execution (line 161, same call) | execution | same |
| `integration/test_scheduler_integration.cpp` | scheduler | root integration | `TaskScheduler` is built from a `RuntimeContext`; moves to `scheduler/tests` once that constructor no longer needs it |
| `integration/test_async_execution.cpp` | execution today, `device` after the split (per-device queues) | execution | names `runtime_context.hpp` only |
| `integration/test_profiling_queue.cpp` | execution today, `device` after the split (`profiling_queue()`) | execution | same |
| `integration/test_workload_coordinator.cpp` | scheduler | scheduler | names two scheduler headers, uses fakes |
| `unit/test_distributed.cpp` | distributed (protocol, transport, registries, policy) | distributed | |
| `integration/test_distributed_e2e.cpp` | distributed | distributed | extension tier may name everything below, `api/fluent.hpp` included |
| `integration/test_distributed_jepsen.cpp` | distributed | distributed | same |
| `regression/test_regression.cpp` | several (execution, scheduler, memory, api) | root regression | one sentinel per defect, across modules |
| `unit/test_node_dispatch_width.cpp` | task and graph (`Node` widths, `TaskGraph::width_last_node`) | root unit | every case builds a `RuntimeContext` |
| `unit/test_registration_index.cpp` | task (`Node::registration_index`) and api (`add_reduce` grouping) | root unit | names the umbrella, execution and graph; three cases leave with the primitives |
| `unit/test_numa_steal.cpp` | scheduler (victim order) | root unit | needs `RuntimeContext` with an injected provider |
| `unit/test_topology.cpp` | topology (2 cases), execution/memory/device (6 cases on `RuntimeContext` queues and aligned allocation) | root unit | should split: the two discovery cases to `topology/tests`, the rest to the module that receives those `RuntimeContext` members |
| `unit/test_topology_abstraction.cpp` | topology (3 cases), execution (3 cases on an injected provider) | root unit | same split |
| `unit/test_nvidia_topology.cpp` | topology (2 cases), execution/device (2 cases on `get_queue`) | root unit | same split; needs a GPU label (TEST-09) |
| `unit/test_allocation.cpp` | memory (`Unit_Allocation`) and api (`Unit_Buffer`) | root unit | two suites, two modules; splits cleanly by suite |
| `unit/test_dependency_tracker.cpp` | graph | graph | |
| `unit/test_event_polling.cpp`, `test_thermal_rebalance.cpp`, `test_work_stealing_deque.cpp` | scheduler | scheduler | |
| `unit/test_range_task.cpp` | task | task | |
| `unit/test_epoch_reclamation.cpp`, `test_usm_registry.cpp` | memory | memory | |
| `unit/test_affinity.cpp` | topology | topology | |
| `unit/test_node_event_record.cpp` | observability | observability | |
| `unit/test_interop_vocabulary.cpp` | interop | interop | |
| `unit/test_delegate.cpp`, `test_ref_counted.cpp` | core | core | |
| `unit/test_logger.cpp` | logging | logging | |

The 31 umbrella-only integration files were split by what they pin. Seventeen pin a type or function declared and implemented under `api/` (`RunHandle`, `Dynamic`, `Region`, `State`, residency, `add_host`, `rule_registration`, the report overloads, and the primitives) and go to `api/tests`. Thirteen pin behaviour that a lower module implements and the facade only exposes (chain against scheduler, cancellation, pinning, the completion latch, cross-context guard, device filter, error isolation, node event clock, report breakdown, rebalancer switch, determinism, host export, layer equivalence) and stay under root `tests/integration`. `test_steady_state_allocation.cpp` is the thirty-first: it already builds its own program and gets its own folder.

### README merges

| Module README | Made from | Also receives |
| --- | --- | --- |
| `modules/foundation/core/README.md` | `I/core/README.md` + `src/core/README.md` | the `version.hpp` section of `I/README.md` |
| `modules/foundation/logging/README.md` | new | the logger sections of `I/core/README.md` |
| `modules/foundation/sycl/README.md` | new | the `sycl_compat` section of `I/core/README.md` |
| `modules/platform/topology/README.md` | `I/topology/README.md` + `src/topology/README.md` | |
| `modules/platform/memory/README.md` | `I/memory/README.md` | the `resource_region` and `resource_liveness` sections of `I/core/README.md` |
| `modules/platform/device/README.md` | new | whatever `I/execution/README.md` and `src/execution/README.md` say about queues, once the split is known |
| `modules/engine/observability/README.md` | new | the `run_report`, `profiling_ledger`, `event_timestamps` sections of `I/core/README.md` |
| `modules/engine/task/README.md` | new | the `node`, `task_types`, `late_bindings` sections of `I/graph/README.md`; the `task` and `ready_sink` sections of `I/scheduler/README.md` |
| `modules/engine/scheduler/README.md` | `I/scheduler/README.md` + `src/scheduler/README.md` | |
| `modules/engine/graph/README.md` | `I/graph/README.md` + `src/graph/README.md` | |
| `modules/engine/execution/README.md` | `I/execution/README.md` + `src/execution/README.md` | |
| `modules/surface/interop/README.md` | `src/interop/README.md` | closes the missing page of LAYOUT-21 |
| `modules/surface/api/README.md` | `I/api/README.md` | the umbrella section of `I/README.md` |
| `modules/extension/distributed/README.md` | `I/distributed/README.md` + `src/distributed/README.md` | |

Six pairs merge (core, topology, scheduler, graph, execution, distributed). Four modules have one source page (memory, interop, api, and core's partner aside). Five READMEs are new: logging, sycl, device, observability, task. `I/core/README.md` (239 lines) is the one that has to be cut by section across five modules; I did not read it section by section.

Under `api/`, all six sub-READMEs stay beside their headers (`data`, `detail`, `graph`, `launch`, `runtime`, `vocabulary`): each header's `@see` line cites its page by path, and `graph/README.md` alone is 899 lines. Two consequences. The install rule must filter `*.md` (LAYOUT-20). The fixed-order sections of `api/detail/README.md` and the `combine` section of `api/vocabulary/README.md` belong in a new `api/numeric/README.md`, which leaves with the folder.

Every `@see include/SushiRuntime/<area>/README.md` and `src/<area>/README.md` citation in the sources changes path. I did not count them.

Test notes follow the tests: `tests/functional/unit/README.md` (462 lines) and `integration/README.md` (1630 lines) keep the sections of the tests that stay and give the rest to a new `tests/README.md` in each of the 13 modules that receive tests. `tests/allocation/README.md` is new.

### Forwarding headers

Each forwarder sits at the old include path and includes the new one. I recommend it lives in the include root of the destination module (for example `modules/foundation/logging/include/SushiRuntime/core/logger.hpp`). Placed in `core`, the forwarders for `run_report.hpp` and `logger.hpp` would themselves be upward and sideways edges. The edge table below was computed without forwarders.

| Old path | New path | Downstream includes of the old path |
| --- | --- | --- |
| `core/logger.hpp` | `logging/logger.hpp` | sushiblas 1, sushiai 1 |
| `core/run_report.hpp` | `observability/run_report.hpp` | sushiengine 1, sushiai 1 |
| `core/dispatch_width.hpp` | `observability/dispatch_width.hpp` | sushiengine 1 |
| `core/resource_region.hpp` | `memory/resource_region.hpp` | sushiblas 12 |
| `graph/task_types.hpp` | `task/task_types.hpp` | sushiblas 72, sushiai 1 |
| `graph/node.hpp` | `task/node.hpp` | sushiblas 1 |
| `core/sycl_compat.hpp` | `sycl/sycl_compat.hpp` | none |
| `core/profiling_ledger.hpp` | `observability/profiling_ledger.hpp` | none |
| `core/event_timestamps.hpp` | `observability/event_timestamps.hpp` | none |
| `core/resource_liveness.hpp` | `memory/resource_liveness.hpp` | none |
| `graph/late_bindings.hpp` | `task/late_bindings.hpp` | none |
| `scheduler/task.hpp` | `task/task.hpp` | none |
| `scheduler/ready_sink.hpp` | `task/ready_sink.hpp` | none |
| `version.hpp` | `core/version.hpp` | none by path; `CMakeLists.txt:75` reads the literal from the old path |
| `api/vocabulary/combine.hpp` | `api/numeric/combine.hpp` | none |
| `api/detail/fixed_order_reduce.hpp`, `fixed_order_scan.hpp` | `api/numeric/` | none; `detail/`, so no forwarder owed |

Downstream paths that do not move and need nothing: `core/span.hpp` (152 includes across the three), `core/error.hpp`, `core/common.hpp`, `graph/task_graph.hpp`, `execution/execution_engine.hpp`, `execution/runtime_context.hpp`, `interop/interop.hpp`, `SushiRuntime.h`. My grep of the three checkouts found two includes the brief's list did not attribute: sushiai includes `core/run_report.hpp`, `core/logger.hpp`, `core/common.hpp` and `graph/task_types.hpp`. It found no include of any header I made private, and no use of `TaskScheduler`, `EpochManager`, `DependencyTracker`, `WorkloadCoordinator`, `ThermalProxyStrategy` or `EventPolling` by name. sushiengine uses `PinPolicy` and sushiai uses `RuntimeSettings`, both through headers that stay.

The three private headers get no forwarder: a forwarder into `source/` defeats the move. The umbrella must drop `graph/dependency_tracker.hpp` (line 55).

### Call sites to update

`Doxyfile` to `.config/doxygen/Doxyfile`:

| Site | What it does |
| --- | --- |
| `cli/sushiruntime/services/project.py:538` | `doxyfile = root / "Doxyfile"`, passed to the driver with `root` as working directory |
| `cli/tests/test_cli.py:40` | `K_DOXYFILE = ...parents[2] / "Doxyfile"`; lines 155 and 180 read keys from it |
| `.github/workflows/ci.yml:114` | `doxygen Doxyfile` |
| `Doxyfile:6` | its own comment names `doxygen Doxyfile` |
| `Doxyfile:29-36` | `INPUT` and `USE_MDFILE_AS_MAINPAGE` are relative to the working directory, which stays the root, so they keep working; but `INPUT` line 33 names `include/SushiRuntime/api`, which becomes `modules/surface/api/include/SushiRuntime/api` |
| prose | `docs/CONTRIBUTING.md:274`, `docs/guides/CLI_GUIDE.md:252`, `docs/DOCUMENTATION_STYLE_GUIDE.md:31`, `docs/reference/KNOWN_ISSUES.md:26,29`, `docs/design/REMAINING_WORK.md:102,135`, `README.md:552` |

`Dockerfile` to `.config/docker/Dockerfile`:

| Site | What it does |
| --- | --- |
| `cli/sushiruntime/services/docker.py:23-30` | `["docker", "build", "-t", IMAGE, ..., "."]` with no `-f`; it needs `-f .config/docker/Dockerfile` and keeps `.` as context |
| `cli/tests/test_docker.py:34,42` | asserts that exact argv |
| `Dockerfile:108` | `COPY . /workspace/sushiruntime/` assumes the context is the root; unchanged if the context stays `.` |
| `.dockerignore` | read from the context root, so it stays; BuildKit also accepts `.config/docker/Dockerfile.dockerignore`, which I did not test |
| prose | `docs/CONTRIBUTING.md:269,400,401`, `docs/guides/CLI_GUIDE.md:304`, `docs/guides/FAQ.md:60`, `README.md:168,554` |

No workflow under `.github/` names the Dockerfile. `cli/build/` holds stale untracked copies that also name both files.

`SECURITY.md` and `CODE_OF_CONDUCT.md` to `.github/`: `docs/README.md:34-35`, `docs/CONTRIBUTING.md:426,432,436`, `docs/guides/DISTRIBUTED_GUIDE.md:369,381`, `docs/guides/FAQ.md:178`, `docs/reference/KNOWN_ISSUES.md:20`, `docs/publish.toml:7-8` (publishes both from `reference/`), `docs/design/REMAINING_WORK.md:111`. `.github/ISSUE_TEMPLATE/bug_report.yml:10` names the file without a path.

Other path-bearing sites I met while grepping, not a complete list: `CMakeLists.txt:75,82` (version literal read from `include/SushiRuntime/version.hpp`), `CMakeLists.txt:91` (`add_subdirectory(examples)`), `tests/CMakeLists.txt:95-168,215,227-246` (every test path), `cli/sushiruntime/env.py:131` (`tests/lsan_suppressions.txt`), `tools/packaging/verify_installed_package.py:24` (`tests/package`, unchanged).

### Edge table after the move

Include lines in `include/` and `source/` of each module, module tests excluded, forwarders not counted.

```
 21 api -> core            14 api -> execution        3 api -> graph
  1 api -> interop          1 api -> logging         11 api -> memory
  2 api -> observability    3 api -> scheduler        6 api -> task
  5 api -> topology
  1 core -> logging         1 core -> observability
 18 distributed -> core     1 distributed -> execution  1 distributed -> graph
  3 distributed -> logging  4 distributed -> task
  9 execution -> core       6 execution -> distributed  3 execution -> logging
  1 execution -> memory     3 execution -> observability  4 execution -> scheduler
  1 execution -> sycl       6 execution -> task        6 execution -> topology
  4 graph -> core           2 graph -> execution       2 graph -> logging
  3 graph -> memory         1 graph -> scheduler       4 graph -> task
  2 interop -> api          2 interop -> core          1 interop -> execution
  2 interop -> memory
  2 memory -> core          1 memory -> execution
  7 scheduler -> core       1 scheduler -> distributed  3 scheduler -> execution
  3 scheduler -> logging    1 scheduler -> memory      2 scheduler -> observability
  1 scheduler -> sycl       6 scheduler -> task        1 scheduler -> topology
  5 task -> core            1 task -> distributed      1 task -> memory
  1 task -> observability
  5 topology -> core        5 topology -> logging
```

`logging`, `sycl` and `observability` include no other module. `execution -> graph` has no include line today: execution reaches graph only through `task` headers, and the edge that exists is the reverse one. `api -> scheduler` (3) and `api -> logging` (1) come from the umbrella alone.

### Offending edges

Sixteen edges, 34 include lines. Paths are today's.

| Edge | Kind | Site | Included | Declaration that causes it |
| --- | --- | --- | --- | --- |
| core -> observability | up | `include/SushiRuntime/core/error.hpp:35` | `core/run_report.hpp` | none in the file; line 128 is a comment saying where `RunReport` lives |
| core -> logging | sideways | `include/SushiRuntime/core/ref_counted.hpp:33` | `core/logger.hpp` | none; no `SR_LOG` or `Logger` use in the file (CORE-19) |
| memory -> execution | up | `include/SushiRuntime/memory/usm_allocation_owner.hpp:33` | `execution/runtime_context.hpp` | `context_->free_usm(address)` at line 63, on a `shared_ptr<Execution::RuntimeContext>` member |
| task -> distributed | up | `include/SushiRuntime/graph/node.hpp:47` (`#ifdef SR_DISTRIBUTED`) | `distributed/protocol.hpp` | `std::vector<Distributed::BufferHandle> in_handles, out_handles` at lines 254-255 |
| task -> observability | sideways | `include/SushiRuntime/graph/node.hpp:40` | `core/dispatch_width.hpp` | `Core::WIDTH_UNKNOWN` at lines 384 and 567 |
| scheduler -> execution | sideways | `include/SushiRuntime/scheduler/task_scheduler.hpp:48` | `execution/runtime_context.hpp` | member `Execution::RuntimeContext& context` at line 199, constructor at line 317 |
| | | `include/SushiRuntime/scheduler/task_scheduler.hpp:49` | `execution/runtime_settings.hpp` | `Execution::PinPolicy pinning_` at lines 109 and 205 |
| | | `src/scheduler/scheduler.cpp:50` | `execution/runtime_context.hpp` | the same context member, used for queues and topology; not traced call by call |
| scheduler -> observability | sideways | `include/SushiRuntime/scheduler/event_polling.hpp:42` | `core/event_timestamps.hpp` | `Core::EventTimestamps` and `read_event_timestamps` at lines 363-364 |
| | | `include/SushiRuntime/scheduler/event_polling.hpp:45` | `core/profiling_ledger.hpp` | `std::atomic<Core::ProfilingLedger*> ledger_` at line 249 |
| scheduler -> distributed | up | `src/scheduler/scheduler.cpp:53` (`#ifdef SR_DISTRIBUTED`) | `distributed/distributed_coordinator.hpp` | `coordinator_->try_offload(gnode)` at line 589, `set_coordinator` at line 1150 |
| graph -> execution | sideways (reverse of an approved edge) | `include/SushiRuntime/graph/task_graph.hpp:43` | `execution/execution_engine.hpp` | return type `Execution::CompiledGraph compile()` at line 231 |
| | | `src/graph/task_graph.cpp:42` | `execution/runtime_context.hpp` | `Impl::context` at line 87; `ctx.distributed_role()` at line 195 |
| graph -> scheduler | sideways | `src/graph/task_graph.cpp:41` | `scheduler/task_scheduler.hpp` | not found: a grep of the file for `TaskScheduler`, `scheduler` and `get_scheduler` returned no use in the lines I saw; possibly unused |
| execution -> task | sideways, reachable through `execution -> graph -> task` | `chain_plan.hpp:36`, `execution_engine.hpp:39`, `src/execution/chain_plan.cpp:31`, `chain_run.cpp:33`, `execution_engine.cpp:38` | `graph/node.hpp` | `Graph::Node*` and `Graph::NodeWidths` in `ChainPlan` (lines 59, 62, 77) and `CompiledGraph` (lines 68-76) |
| | | `include/SushiRuntime/execution/runtime_context.hpp:46` (`#ifdef SR_DISTRIBUTED`) | `graph/task_types.hpp` | not traced |
| execution -> observability | sideways | `include/SushiRuntime/execution/execution_engine.hpp:41` | `core/run_report.hpp` | `Core::RunReport&` parameters at lines 292 and 397 |
| | | `include/SushiRuntime/execution/execution_engine.hpp:42` | `core/profiling_ledger.hpp` | member `Core::ProfilingLedger profiling_ledger` at line 110 |
| | | `src/execution/chain_run.cpp:31` | `core/event_timestamps.hpp` | not traced |
| execution -> distributed | up | `include/SushiRuntime/execution/runtime_context.hpp:47-52` (`#ifdef SR_DISTRIBUTED`), six lines | `protocol`, `i_transport`, `kernel_registry`, `buffer_registry`, `i_offload_policy`, `distributed_coordinator` | `enable_distributed_master(...)` at lines 367-369, `register_distributed_buffer` at 372, `distributed_buffer` at 381 |
| interop -> api | sideways | `include/SushiRuntime/interop/host_export.hpp:32` | `api/data/buffer.hpp` | member `API::Buffer<T> buffer` of `HostExport<T>` |
| | | `include/SushiRuntime/interop/interop.hpp:34` | `api/data/buffer.hpp` | class `API::Interop` (lines 93-156), whose `export_host` and `import_device` construct `Buffer<T>` |
| api -> interop | sideways | `include/SushiRuntime/api/runtime/runtime.hpp:39` | `interop/interop.hpp` | `Interop interop() const noexcept` at line 208 |

The observability rows are a table question, not a code defect: the approved table puts `observability` in the engine tier and lists no edge into it, yet `task`, `scheduler` and `execution` all name its types. Either three edges are added to the list or the module sits lower. The three sideways edges into it and the one upward edge from `core` are the new facts this move creates; the rest were in LAYOUT-01.

### Files I could not place cleanly, and why

1. **Rule 1 fails for six of CORE-12's eight headers.** CORE-12 counted `src/` as one unit. With modules, `src/execution/execution_engine.cpp` and `runtime_context.cpp` include `scheduler/task_scheduler.hpp`, `workload_coordinator.hpp` and `thermal_proxy_strategy.hpp`, and `src/graph/dependency_tracker.cpp` and `src/scheduler/scheduler.cpp` include `memory/epoch_reclamation.hpp`. `event_polling.hpp` and `rebalancer_interfaces.hpp` are included by `task_scheduler.hpp`, so they cannot sit under `source/` while it sits under `include/`. Only `work_stealing_deque.hpp` and `dependency_tracker.hpp` are private by the rule; `interop/detail/bindless_detection.hpp` is a third that CORE-12 did not list (one includer, `src/interop/interop.cpp`). The five scheduler headers are what the skill calls an internal module: no downstream repository and no `api/` header includes them, so `scheduler` can be marked internal and not installed. That is a public-surface decision for the owner and a target property for the CMake designer. `epoch_reclamation.hpp` stays installed with `memory` unless memory is split in two.
2. **Tests of private headers.** `test_work_stealing_deque.cpp` and `test_dependency_tracker.cpp` need their module's `source/` on the include path, and the `<SushiRuntime/...>` spelling they use today stops resolving.
3. **`runtime_context.hpp`, `runtime_context.cpp`, `node.hpp`, `task_scheduler.hpp`, `runtime_settings.hpp`** are placed whole and wait on the cycle designer. `device` receives no file from this map.
4. **`interop.hpp` and `host_export.hpp`** are placed whole in `interop` because `interop/interop.hpp` is on the downstream list. `interop.hpp` holds two things: the exported `Interop::Detail` functions (interop) and the `API::Interop` facade class (api). The api/interop pair is in neither designer's list as I read the brief; someone has to own it.
5. **The umbrella.** `SushiRuntime.h` includes eleven modules, so it can only live in the top surface module; I put it in `api`. It uses 22 quoted relative includes that break on the move, includes headers that become private or internal (lines 55, 58, 60, 61), and omits `interop/`. LAYOUT-12's proposal of a new `sushiruntime.hpp` is not decided here.
6. **The four primitives live inside `api/graph/graph.hpp`** and cannot be lifted by moving a file. I grouped the three files that can (`api/numeric/`) and their seven tests (`api/tests/numeric/`). The test subfolder is a deviation from a flat module `tests/`; the CMake designer's source list has to accept it. Four more tests use a primitive in part: `test_determinism.cpp`, `test_registration_index.cpp`, `test_rule_registration.cpp`, and the mixed `test_late_count.cpp` and `test_late_count_ordering.cpp`, whose orchestration half stays by the PLAN.
7. **Stress tests.** `test_epoch_stress.cpp` and `test_scheduler_stress.cpp` pass or fail, which the `testing` skill says a benchmark never does. I kept them in `tests/benchmark/` under `benchmark_*_stress.cpp` because they build into the nightly program today. The alternative is a `tests/stress/` program with its own README. `test_dynamic_graph_perf.cpp` has the same mix (its `@brief` starts "Tests that").
8. **`tests/allocation/`** is a new folder. The skill allows it (a test kind that builds a different program), and it needs a README.
9. **Seven root unit tests** are there only because the contract they test cannot be built without `RuntimeContext`. Four of them split by case, as the contract table says. I placed files, not cases.
10. **Platform checks outside a platform module.** `core/bit.hpp` (3), `core/logger.hpp` (3), `core/export.hpp` (1), `topology/affinity.hpp` (2) and `src/distributed/tcp_transport.cpp` (6) carry `_WIN32` or `_MSC_VER` branches. `platform-portability` wants those in adapter files; this map moves the files as they are.
11. **`tests/lsan_suppressions.txt`** is configuration a flag points at and fits `.config/`; the brief did not list it, so it stays.

### Not checked

- No build, configure or test ran. No file was compiled against the new layout; the edge table is arithmetic over include lines, not a compile.
- Bodies of the 31 umbrella-only integration tests: classified by `@brief`, suite names and a grep for primitive names, not read in full.
- `I/core/README.md`, the unit and integration test READMEs: measured, not read by section.
- Whether any header reaches another module's symbol without including its header (through a transitive include). The script sees include lines only.
- Symbol use in downstream repositories was grepped for 24 names; a use through a type alias would not show.
- The `@see` path citations in sources and the links in `docs/` that the move breaks were not counted.
- `Doxyfile` beyond `INPUT`, `OUTPUT_DIRECTORY` and the exclude patterns; `Dockerfile` beyond its `COPY` and `WORKDIR` lines.
- Three offending edges have no traced declaration (marked in the table).

### Delegation block

1. **SOLID.** The map gives each of the 14 modules one responsibility and moves a header only where its `@brief` and includes put it. Where one file holds two responsibilities (`interop.hpp`, `runtime_context.hpp`, four test files) I placed it whole and named the split instead of inventing one. Sibling modules are shaped the same: `README.md`, `include/SushiRuntime/<module>/`, `source/`, `tests/`.
2. **Humanizer.** Loaded with `generic_english.md` and applied to this report: findings stated with file and line, no evaluative adjectives, no closing summary.
3. No `sr`, `se`, `cmake`, `ninja` or `ctest` process was started. No build file was written.
4. Nothing was written in the repository, `docs/agent/` included.
5. Done: the 362-row map, README merges, forwarders, call sites, edge table, offending edges. Not done: per-case test splits, the section-level cut of `I/core/README.md`, three untraced declarations.
6. No source written; nothing to syntax-check.
7. **Scratchpad.** I created `b1/move_map.py` and `b1/table.md` (text, under 60 KB together) and deleted the `b1` folder. Size now: `du -sh` reports 4.0K, the folder is empty. `baseline.log` and `wave_a.log` were in the scratchpad when I started and are gone now; my only delete was `rm -rf <scratchpad>/b1`.

## Part 2: making the tier table true

Read at head `9956845` (phase A was committed while I worked; the line numbers below are from that tree). Nothing was written to the repository. Every claim about speed below is a prediction for phase E's benchmarks, none was measured.

Two policies hold through the whole design:

- A declaration that moves keeps its namespace (`Scheduler::Task`, `Graph::Node`, `Core::RunReport`). A new declaration takes the namespace of its module, except the offload seam, which goes in `Graph` beside `Graph::Node` because a namespace `Task` next to the class `Scheduler::Task` reads badly. Renaming namespaces to match modules is a separate, mechanical pass and the owner's call.
- A forwarder is added without `[[deprecated]]` first. The attribute goes on only after the in-repo callers have moved, because the tree builds with `-Werror`.

### 1. The bricks that leave `RuntimeContext`

#### 1.1 `Device::DeviceQueueSet` (module `device`)

Header `device/device_queue_set.hpp`, source `device_queue_set.cpp`. Smaller headers beside it: `device/device_info.hpp` (`DeviceKind`, `DeviceInfo`, `DeviceDescriptor`), `device/queue_flavour.hpp`.

Takes from `runtime_context.hpp` / `.cpp`: `get_queue` (133 / 385), `profiling_queue` (140 / 426), `has_profiling_queue` (147 / 436), `chain_queue` (155 / 474), `device_count` (161 / 498), `device_is_gpu` (170 / 487), `get_numa_local_queue` (177 / 508), `wait_all` (351 / 834), the private `build_profiling_queue` and `build_chain_queue` (431, 439 / 394, 449), the queue-building half of `probe_hardware` (cpp 294-343, 360-369) and the destructor drain (cpp 168-188).

Owns: `nodes` (cpp 68, as `std::vector<DeviceInfo>` plus the `sycl::device`s), `all_queues` (77), `numa_aware_queues` (74), and the four lazy families (82-112) as one table of `LazyQueue` per device and flavour (EXEC-10). The async handler lambda written three times (cpp 320, 411, 461) becomes one private builder.

Constructed from a list of `DeviceDescriptor` and the profiling flag. `RuntimeContext` converts `Topology::HardwareNode` to `DeviceDescriptor` in a private function of `execution`, so `device` does not include `topology` and the platform tier needs no same-tier edge. The "no SYCL device satisfied the filter" error (cpp 346-358) stays in `RuntimeContext`, which is the unit that holds the filter.

```cpp
namespace SushiRuntime::Device
{
    enum class DeviceKind : std::uint8_t { Cpu, Gpu, Accelerator, Other };

    struct DeviceInfo
    {
        std::size_t index = 0;
        std::string name;
        DeviceKind kind = DeviceKind::Other;
        sycl::backend backend = sycl::backend::opencl;
        int numa_node = 0;
        std::uint64_t memory_bytes = 0;
        std::uint32_t compute_units = 0;
    };

    struct DeviceDescriptor { sycl::device device; DeviceInfo info; };

    struct QueueFlavour { bool in_order = false; bool profiling = false; };

    class SUSHIRUNTIME_API DeviceQueueSet final
    {
        public:
            DeviceQueueSet(std::vector<DeviceDescriptor> devices, bool profiling);
            ~DeviceQueueSet();                       // drains every built queue, logs, never throws
            DeviceQueueSet(const DeviceQueueSet&) = delete;
            DeviceQueueSet& operator=(const DeviceQueueSet&) = delete;

            [[nodiscard]] std::size_t count() const noexcept;
            [[nodiscard]] const std::vector<DeviceInfo>& devices() const noexcept;   // EXEC-05
            [[nodiscard]] bool is_gpu(std::size_t index) const;
            [[nodiscard]] sycl::queue& queue(std::size_t index);                     // plain, eager
            [[nodiscard]] sycl::queue& queue(std::size_t index, QueueFlavour flavour); // built once
            [[nodiscard]] bool has_profiling_queue(std::size_t index) const noexcept;
            [[nodiscard]] std::size_t device_for_numa(int numa_node) const;          // EXEC-04 helper
            [[nodiscard]] sycl::queue& queue_or_numa_local(std::optional<std::size_t> device,
                                                           int fallback_numa);
            [[nodiscard]] std::vector<sycl::queue> plain_queues() const;             // copies
            void wait_all();                         // every built queue (EXEC-10)
    };
}
```

`probe_hardware` (hpp 95) has no caller outside the constructor and is removed (EXEC-06); the set is immutable after construction, which is what makes the references it hands out safe.

#### 1.2 `Memory::UsmAllocator` (module `memory`)

Headers `memory/usm_allocator.hpp`, `memory/usm_request.hpp`, `memory/usm_release_observer.hpp`; source `usm_allocator.cpp`. `memory/usm_registry.hpp` stays and becomes a member.

Takes: `malloc_shared` (199 / 596), `malloc_device` (205 / 618), `malloc_shared_on` (213 / 540), `malloc_device_on` (221 / 559), `malloc_host_on` (229 / 578), the three `aligned_malloc_*_on` (244, 254, 264 / 640, 657, 674), `free_usm` (235 / 745), `adopt_usm` (275 / 690), `containing_usm_base` (285 / 702), `forget_usm` (291 / 712), `live_usm_count` (298 / 721), `context_index_for_ptr` (191 / 729) and the first two thirds of `get_queue_for_ptr` (184 / 767-802: registry hit, then SYCL introspection). The last third, the NUMA fallback at cpp 804, is `DeviceQueueSet::device_for_numa`.

Owns: `usm_registry` (cpp 128) and its own copies of the plain queues. `sycl::queue` has reference semantics and the context already stores copies (cpp 336), so the allocator is constructed from `devices.plain_queues()` and `memory` includes neither `device` nor `execution`.

```cpp
namespace SushiRuntime::Memory
{
    enum class UsmKind : std::uint8_t { Host, Shared, Device };

    struct UsmRequest
    {
        UsmKind kind = UsmKind::Shared;
        std::size_t bytes = 0;
        std::size_t alignment = 0;       // 0: no alignment request
        std::size_t device_index = 0;
    };

    class UsmReleaseObserver
    {
        public:
            virtual void on_release(void* address) noexcept = 0;
        protected:
            ~UsmReleaseObserver() = default;
    };

    class SUSHIRUNTIME_API UsmAllocator final
    {
        public:
            explicit UsmAllocator(std::vector<sycl::queue> device_queues);
            UsmAllocator(const UsmAllocator&) = delete;
            UsmAllocator& operator=(const UsmAllocator&) = delete;

            [[nodiscard]] void* allocate(const UsmRequest& request);   // the one entry point
            void release(void* address);
            void adopt(void* address, std::size_t device_index, std::optional<std::size_t> bytes);
            void forget(void* address) noexcept;
            [[nodiscard]] std::optional<std::size_t> device_of(void* address) const;      // registry only
            [[nodiscard]] std::optional<std::size_t> owning_device(void* address) const;  // then introspection
            [[nodiscard]] std::optional<void*> containing_base(void* address) const;
            [[nodiscard]] std::size_t live_count() const;
            void set_release_observer(UsmReleaseObserver* observer) noexcept;
    };
}
```

`allocate` returns null when SYCL returns null and throws `Core::SushiException` with `Errc::invalid_access`, the index and the device count on a bad index (EXEC-07). `device_of` returns an empty optional where `context_index_for_ptr` returned -1.

`memory/usm_allocation_owner.hpp` changes its constructor to `explicit UsmAllocationOwner(std::shared_ptr<UsmAllocator> allocator) noexcept` and calls `allocator_->release(address)`. `RuntimeContext` therefore holds the allocator in a `shared_ptr`. No forwarder for the old constructor is possible, since it named `RuntimeContext`; no downstream repository includes a `memory/` header.

The release observer replaces the `#ifdef` call at cpp 749-751. It is one atomic pointer load per free.

#### 1.3 `Observability::TransferCounters` (module `observability`)

Header `observability/transfer_counters.hpp`, header-only. Takes hpp 114, 120, 123, 126 (cpp 250-277) and the two atomics (cpp 122-123).

```cpp
namespace SushiRuntime::Observability
{
    class TransferCounters final
    {
        public:
            void add_device_to_host(std::size_t bytes) noexcept;
            void add_host_to_device(std::size_t bytes) noexcept;
            [[nodiscard]] std::size_t device_to_host() const noexcept;
            [[nodiscard]] std::size_t host_to_device() const noexcept;
    };
}
```

#### 1.4 `Topology::TopologyView` (module `topology`)

Header `topology/topology_view.hpp`. Takes `numa_distance` (316 / 812) and `cpu_core_to_numa` (326 / 822), and the state `distance_model` (cpp 65) and `core_to_numa` (cpp 71). The provider (cpp 62) is used only during construction and is no longer stored.

```cpp
namespace SushiRuntime::Topology
{
    class SUSHIRUNTIME_API TopologyView final
    {
        public:
            TopologyView(const ITopologyProvider& provider,
                         std::shared_ptr<const INumaDistanceModel> distance_model);
            [[nodiscard]] const std::vector<int>& core_to_numa() const noexcept;
            [[nodiscard]] int numa_distance(int numa_a, int numa_b) const;
    };
}
```

#### 1.5 The distributed session

The thirteen members at hpp 359-418 (cpp 867-1047) and the state at cpp 142-146 leave for `distributed`; section 4 says where each goes.

#### 1.6 What `RuntimeContext` still is

It builds the bricks in order, keeps the settings, owns the engine, holds the installed offload coordinator and tears down in order. Member order in `Impl`: `settings`, `topology`, `devices`, `memory` (shared), `transfers`, `offload` (shared), `engine`. Teardown: `offload->stop()`, clear the allocator's release observer, `engine->shutdown()`, engine destroyed, then the device set drains in its own destructor.

```cpp
namespace SushiRuntime::Execution
{
    class SUSHIRUNTIME_API RuntimeContext
    {
        public:
            explicit RuntimeContext(const Topology::HardwareFilterConfig& config = {},
                                    std::shared_ptr<Topology::ITopologyProvider> provider = nullptr,
                                    std::shared_ptr<Topology::INumaDistanceModel> distance_model = nullptr,
                                    const RuntimeSettings& settings = {});
            ~RuntimeContext();
            RuntimeContext(const RuntimeContext&) = delete;
            RuntimeContext& operator=(const RuntimeContext&) = delete;

            [[nodiscard]] const RuntimeSettings& settings() const noexcept;
            [[nodiscard]] const Topology::TopologyView& topology() const noexcept;
            [[nodiscard]] Device::DeviceQueueSet& devices() noexcept;
            [[nodiscard]] Memory::UsmAllocator& memory() noexcept;
            [[nodiscard]] const std::shared_ptr<Memory::UsmAllocator>& shared_memory() const noexcept;
            [[nodiscard]] Observability::TransferCounters& transfers() noexcept;
            [[nodiscard]] IExecutionEngine& get_execution_engine();

            void install_offload(std::shared_ptr<Graph::OffloadCoordinator> coordinator);
            [[nodiscard]] Graph::OffloadCoordinator* offload() const noexcept;

            void shutdown();

            // Deprecated forwarders, one MINOR; table below.

        private:
            struct Impl;
            sushi_ptr<Impl> impl_;
    };
}
```

`RuntimeSettings` keeps its fields. `PinPolicy` and `WorkerPoolInfo` move to `scheduler` (section 3) and `runtime_settings.hpp` keeps `using PinPolicy = Scheduler::PinPolicy;` and the same for `WorkerPoolInfo`, so `API::PinPolicy::Float` in SushiEngine compiles unchanged.

#### 1.7 Downstream spellings

Checked by grep in the three repositories (SushiBLAS: `get_queue` 124 hits, `live_usm_count` 53, `take_nodes` 51; SushiAI: `Execution::CompiledGraph` as a member type in `compiled_step.hpp`; SushiEngine: `free_usm`, `settings`, `shutdown`, `unsafe_context`, `Interop::HostExport<T>`).

| Today | New spelling | Kept for one MINOR as |
| --- | --- | --- |
| `ctx.get_queue(i)` | `ctx.devices().queue(i)` | forwarder |
| `ctx.wait_all()` | `ctx.devices().wait_all()` | forwarder |
| `ctx.device_count()` | `ctx.devices().count()` | forwarder |
| `ctx.live_usm_count()` | `ctx.memory().live_count()` | forwarder |
| `ctx.free_usm(p)` | `ctx.memory().release(p)` | forwarder |
| `ctx.aligned_malloc_host_on(a, n, i)` | `ctx.memory().allocate({UsmKind::Host, n, a, i})` | forwarder |
| `ctx.aligned_malloc_shared_on(a, n, i)` | `ctx.memory().allocate({UsmKind::Shared, n, a, i})` | forwarder |
| `ctx.aligned_malloc_device_on(a, n, i)` | `ctx.memory().allocate({UsmKind::Device, n, a, i})` | forwarder |
| `ctx.get_execution_engine()`, `settings()`, `shutdown()` | unchanged | no forwarder needed |
| `engine.execute(plan)` | unchanged | |
| `Graph::TaskGraph graph(ctx)` | `Graph::TaskGraph graph;` | deprecated constructor, see 3.2 |
| `add_task`, `add_node`, `compile`, `take_nodes` | unchanged | |
| `Execution::CompiledGraph` | `Graph::CompiledGraph` | `using` in `execution_engine.hpp` |
| `Graph::Node`, `Graph::TaskMetadata`, `Graph::OpID`, `_op` | unchanged names; headers move to `task/` | forwarding headers `graph/node.hpp`, `graph/task_types.hpp` |
| `Interop::HostExport<T>`, `rt.interop()` | `API::HostExport<T>`; `rt.interop()` unchanged | alias template in the `api` header |

The remaining members get forwarders of the same shape: `malloc_*_on` and the two NUMA forms (`allocate` with `devices().device_for_numa(numa)`), `adopt_usm`, `forget_usm`, `containing_usm_base`, `context_index_for_ptr` (maps the empty optional back to -1), `get_queue_for_ptr` (`devices().queue_or_numa_local(memory().owning_device(p), numa)`), `profiling_queue`, `chain_queue`, `has_profiling_queue`, `device_is_gpu`, `get_numa_local_queue`, `numa_distance`, `cpu_core_to_numa`, the four byte counters and `profiling_enabled`. Each is a one-line body in `runtime_context.cpp`; none holds logic.

### 2. `task`

| From | Declarations | Lands in |
| --- | --- | --- |
| `scheduler/task.hpp` 48-134 | `Scheduler::Task` | `task/task.hpp` |
| `scheduler/task.hpp` 140-197 | `Scheduler::RangeTask` | stays in `scheduler`, as `scheduler/range_task.hpp`; it needs only `Task` and only a unit test builds one |
| `scheduler/ready_sink.hpp` | `Scheduler::ReadySink` | `task/ready_sink.hpp` (`Task::on_complete` takes it) |
| `graph/task_types.hpp` | `TaskType`, `OpID`, `Literals::_op`, `TaskMetadata` | `task/task_types.hpp` |
| `graph/late_bindings.hpp` | `LateBindings` | `task/late_bindings.hpp` |
| `graph/node.hpp` 55-58 | `TaskWork`, `HostWork` | `task/task_work.hpp`; the duplicate at `task_graph.hpp:59-64` is deleted |
| `graph/node.hpp` 67-146 | `SmallEventVector` | `task/small_event_vector.hpp` |
| `graph/node.hpp` 166-226 | `RegionLink` | `task/region_link.hpp` |
| `graph/node.hpp` 232-237 | `NodeProfiling` | `task/node_profiling.hpp` |
| `graph/node.hpp` 239-290 | `OffloadState`, `NodeExtensions` | replaced by `task/offload_state.hpp` (section 4); `NodeExtensions` is deleted |
| `graph/node.hpp` 156-160, 296-562 | `SuccessorNode`, `Node` | `task/node.hpp` |
| `graph/node.hpp` 565-576 | `NodeWidths`, `snapshot_widths` | `task/node_widths.hpp`; the body moves from `src/graph/task_graph.cpp:529` to the `task` source, otherwise `scheduler` would link against `graph` |
| new | `OffloadCoordinator`, `OffloadSink`, `OffloadStatistics` | `task/offload_coordinator.hpp`, `task/offload_sink.hpp` |

Directions checked against the code:

- `Node` derives from `Task` and overrides `on_complete(ReadySink&)`: all three are in `task`.
- `Node` holds `sycl::event`, `std::function`, `Core::ResourceRegion` (to `memory`), `LateBindings`, `TaskMetadata`, `Core::WIDTH_UNKNOWN` (`core/dispatch_width.hpp`, to `observability`). So `task` includes `core`, `sycl`, `memory`, `observability`.
- `scheduler/event_polling.hpp:46` needs `Node` (`registration_index`, the downcast at 375) and `NodeWidths` (81): both now in `task`.
- `scheduler/work_stealing_deque.hpp` includes `scheduler/task.hpp`: now `task`.
- `scheduler.cpp` reads `Node` fields at 648-857 and calls `snapshot_widths` at 855: `task`.
- `graph/dependency_tracker.hpp` includes `graph/node.hpp`: `task`.
- `task_graph.cpp` names `Scheduler::Task::ExecutionMode` (363, 367, 441, 576): `task`.

After this, `scheduler` and `graph` each include `task` and neither includes the other. The one remaining line, `task_graph.cpp:41` (`scheduler/task_scheduler.hpp`), is unused and is deleted (CORE-14).

`Task` and `Node` in one module also makes CORE-08 local: marking `Node` `final` and passing `Node*` down touches `task` and `scheduler` only. This design does not do it.

### 3. The upward edges, one by one

#### 3.1 `scheduler -> execution`

Sites: `task_scheduler.hpp:48-49`, `scheduler.cpp:50`. The header needs `RuntimeContext` for one reference member (199) and `PinPolicy` for `pinning()` (109) and `pinning_` (205).

| `scheduler.cpp` | Today | After |
| --- | --- | --- |
| 151-162 | `context.settings()`: `worker_count`, `pinning`, `first_core` | constructor argument `WorkerPoolSettings` |
| 203 | `context.cpu_core_to_numa()` | `topology_.core_to_numa()` |
| 233 | `context.numa_distance(a, b)` | `topology_.numa_distance(a, b)` |
| 280-281 | `context.profiling_queue(i)` / `context.get_queue(i)` | `devices_.queue(i, {false, true})` / `devices_.queue(i)` |
| 289-291 | `device_count()` and a scan comparing `get_queue(i) == q` | deleted; routing is by index throughout |
| 755, 764 | `context.context_index_for_ptr(p)` | `memory_.device_of(p)` |
| 790 | `context.get_queue_for_ptr(anchor, numa)` | `dispatch_queue(memory_.owning_device(anchor).value_or(devices_.device_for_numa(numa)))` |
| 791 | `context.get_numa_local_queue(numa)` | `dispatch_queue(devices_.device_for_numa(numa))` |
| 1164, 1194 | `context.bump_distributed_buffer(p)` | moves into the coordinator (section 4) |

New in `scheduler`: `scheduler/pin_policy.hpp` (`PinPolicy`, moved from `runtime_settings.hpp:41`), `scheduler/worker_pool_info.hpp` (`WorkerPoolInfo`, from 99), `scheduler/worker_pool_settings.hpp`:

```cpp
namespace SushiRuntime::Scheduler
{
    struct WorkerPoolSettings
    {
        std::size_t worker_count = 0;
        std::size_t first_core = 0;
        PinPolicy pinning = PinPolicy::Pin;
    };

    // task_scheduler.hpp
    TaskScheduler(const WorkerPoolSettings& pool, const Topology::TopologyView& topology,
                  Device::DeviceQueueSet& devices, const Memory::UsmAllocator& memory);
}
```

`ExecutionEngine::Impl` (`execution_engine.cpp:140`) builds the settings from `ctx.settings()` and passes `ctx.topology()`, `ctx.devices()`, `ctx.memory()`. Fourteen test sites construct `TaskScheduler x(ctx)`; they change, with one helper in `tests/common`. No forwarder: the old constructor named a type above the scheduler.

Hot path: `dispatch_queue` today calls into `RuntimeContext` and dereferences `impl_` before indexing `all_queues`. After, it indexes a member of the set it holds by reference: one pointer load fewer per device-node dispatch, and the same on the first-touch registry lookup. The scan at 287-294 on the profiled fallback path is gone. No virtual call, indirection or allocation is added.

#### 3.2 `graph -> execution`

Sites: `task_graph.hpp:43` includes `execution_engine.hpp` because `compile()` returns `Execution::CompiledGraph`; `task_graph.cpp:42` includes `runtime_context.hpp`. The context is used in one place, `stamp_offloadable` (193-246, called at 718); the member at 87 is otherwise untouched.

Changes:

- `CompiledGraph` (`execution_engine.hpp:65-132`) moves to `graph/compiled_graph.hpp` as `Graph::CompiledGraph`; its three bodies (`execution_engine.cpp:538-597`) move to `compiled_graph.cpp` in `graph`. `execution_engine.hpp` keeps `using CompiledGraph = Graph::CompiledGraph;`. The forward declaration at `chain_plan.hpp:47` becomes an include.
- `CompiledGraph` holds a `ChainPlan` by value, so the two plain structs `PendingEvent` and `ChainPlan` (`chain_plan.hpp:57-86`) move to `graph/chain_plan.hpp`. `classify_chain` (95) and `ChainRun` stay in `execution`; `execution/chain_plan.hpp` keeps declaring `classify_chain`, includes the `graph` header and aliases the two names, so its include path needs no forwarder.
- `TaskGraph` takes the seam and nothing else:

```cpp
explicit TaskGraph(OffloadCoordinator* offload = nullptr);

[[deprecated("TaskGraph no longer takes a context; use TaskGraph()")]]
explicit TaskGraph(Execution::RuntimeContext& context);   // delegates to TaskGraph(nullptr)
```

  The deprecated constructor names `RuntimeContext` through a forward declaration, with no include; it is the one upward name left in `graph` and it goes with the forwarder. `compile()` calls `offload->prepare(...)` where it calls `stamp_offloadable` today, so the stamp happens at the same moment as now. The fluent `Graph` passes `ctx_->offload()`.

#### 3.3 `memory -> execution`

Site: `usm_allocation_owner.hpp:33`, for `context_->free_usm(address)` at 63. Removed by 1.2: the owner holds the allocator. Callers: `api/data/buffer.hpp:157` and `src/interop/interop.cpp:229` pass `ctx->shared_memory()`.

#### 3.4 `api <-> interop`

Sites: `interop/interop.hpp:34` and `interop/host_export.hpp:32` include `api/data/buffer.hpp`; back edge `api/runtime/runtime.hpp:39`. The cause is two `Buffer`-typed declarations sitting in `interop`: the class `API::Interop` (`interop.hpp:86-156`) and `Interop::HostExport<T>` (`host_export.hpp:45-51`).

Change: both move to `api`, as `api/interop/interop.hpp` and `api/interop/host_export.hpp` (`API::HostExport<T>`, with `namespace Interop { template <typename T> using HostExport = API::HostExport<T>; }` in the same header so SushiEngine's spelling compiles). `interop/interop.hpp` keeps the three `Detail` functions (58-81) and the vocabulary includes; SushiEngine's `#include <SushiRuntime/interop/interop.hpp>` still resolves and it reaches `API::Interop` through the runtime header as it does today. The edge left is `api -> interop`, inside the surface tier.

#### 3.5 `scheduler -> graph`

Sites: `event_polling.hpp:46`, `scheduler.cpp:52`. Removed by section 2.

#### 3.6 `scheduler -> topology`, `scheduler -> memory`

Sites: `scheduler.cpp:46` (`topology/affinity.hpp`, thread pinning) and `:47` (`memory/epoch_reclamation.hpp`, the worker's epoch guard). Both point down in the approved tiers and stay. The scheduler adds `topology/topology_view.hpp`, `memory/usm_allocator.hpp` and `device/device_queue_set.hpp`, all platform.

#### 3.7 `graph`, `scheduler`, `execution -> distributed`

Sites: `graph/node.hpp:47`, `scheduler.cpp:53`, `runtime_context.hpp:47-52`. Removed by section 4.

#### 3.8 Two edges LAYOUT-01 did not list, because both ends were in `core/`

- `core/error.hpp:35` includes `core/run_report.hpp`, which goes to `observability`. The only trace of a use is a comment at 128. Delete the include and add it where a build asks.
- `core/ref_counted.hpp:33` includes `core/logger.hpp`, which goes to `logging`. A grep for `SR_` in that header finds nothing. Delete the include.

### 4. The offload seam

Declared in `task`, three headers, no `#ifdef` anywhere below `distributed`.

```cpp
namespace SushiRuntime::Graph
{
    struct Node;

    // task/offload_state.hpp
    class OffloadPayload                       // what a coordinator stores on a node
    {
        public:
            virtual ~OffloadPayload() = default;
        protected:
            OffloadPayload() = default;
    };

    struct OffloadState
    {
        bool offloadable = false;
        std::atomic<std::uint32_t> attempts{0};
        std::shared_ptr<std::vector<void*>> live_reads;    // set by add_offload_dynamic
        std::shared_ptr<std::vector<void*>> live_writes;
        std::unique_ptr<OffloadPayload> payload;           // set by prepare()
    };

    // task/offload_sink.hpp: implemented by TaskScheduler
    class OffloadSink
    {
        public:
            virtual void resubmit(Node& node) = 0;          // consumes the coordinator's reference
            virtual void complete_remote(Node& node) = 0;   // releases successors, consumes the reference
        protected:
            ~OffloadSink() = default;
    };

    // task/offload_coordinator.hpp: implemented by Distributed::DistributedCoordinator
    struct OffloadStatistics
    {
        std::uint64_t offloaded = 0;
        std::uint64_t declined = 0;
        std::uint64_t redispatched = 0;
        std::uint64_t stale_writebacks = 0;
        std::size_t alive_workers = 0;
    };

    class OffloadCoordinator
    {
        public:
            virtual ~OffloadCoordinator() = default;
            virtual void bind(OffloadSink& sink) = 0;
            virtual void prepare(span<const sushi_ptr<Node>> nodes) = 0;      // once per compile()
            [[nodiscard]] virtual bool try_offload(Node& node) = 0;           // true: coordinator owns the node
            virtual void on_local_completion(Node& node) noexcept = 0;
            virtual void share(void* address, std::size_t bytes) = 0;
            [[nodiscard]] virtual OffloadStatistics statistics() const noexcept = 0;
            virtual void stop() noexcept = 0;
    };
}
```

`Node::ext_` (`node.hpp:404`) becomes `std::unique_ptr<OffloadState> offload_state_`, with `offload()` and `offload_state()` (451-471) kept and unconditional. `sizeof(Node)` does not change: it is one pointer before and after. `in_handles`, `out_handles` and `remote_op` (252-255) leave the node: the handles become a private `Distributed::OffloadHandles : Graph::OffloadPayload` under `distributed`'s `source/`, and `remote_op` was always `metadata.op_id`. `reads` and `writes` (257-258) are deleted: they copy `io_reads_` and `io_writes_` bases (`task_graph.cpp:323-329`, 453-459).

Where each hook goes:

| Today | After |
| --- | --- |
| `task_graph.cpp:326-330`, 391-395, 589-592: fill `offload().reads/writes` on every tracked node | deleted; `prepare()` reads `io_reads_` / `io_writes_` |
| `task_graph.cpp:456-463`: live lists | unconditional, same two assignments |
| `task_graph.cpp:193-246`, 717-719: `stamp_offloadable` | body moves to `DistributedCoordinator::prepare`; `compile()` runs `if (offload) offload->prepare(graph.all_nodes)` |
| `scheduler.cpp:581-610`: dispatch branch | `if (offload_ != nullptr && node has offload_state_ && offloadable) offloaded = offload_->try_offload(*node);` then the existing two arms, written once |
| `event_polling.hpp:162-173`, 242-245, 306-310: `std::function` observer | a plain `OffloadCoordinator*` member; `if (offload_ != nullptr) offload_->on_local_completion(node)` |
| `scheduler.cpp:1150-1170` `set_coordinator` and its two lambdas | `set_offload_coordinator(OffloadCoordinator*)`, which stores the pointer in the scheduler and the poller and calls `bind(*this)` |
| `scheduler.cpp:1177-1224` `complete_offloaded_node` | `complete_remote(Node&)` and `resubmit(Node&)`, the `OffloadSink` overrides; the hand-written successor walk at 1197-1220 calls `Node::for_each_successor` (CORE-11) |
| version bump of written buffers (`scheduler.cpp:1161-1164`, 1192-1194) | inside the coordinator, from `node.io_writes_`, in `on_local_completion` and before it calls `complete_remote` |
| `execution_engine.cpp:592-596`, `dynamic_graph.hpp:927-932`: reset `attempts` in `rearm` | deleted; the coordinator resets `attempts` when the node completes, locally or remotely. Every node completes once per run, so the counter still counts per run |
| `execution_engine.hpp:399-402` `scheduler_ref()` | deleted; `ExecutionEngine::install_offload(OffloadCoordinator*)` forwards to the scheduler |
| `runtime_context.cpp:749-751` unregister on free | `UsmReleaseObserver`, implemented by the coordinator |
| `RuntimeContext` role, registry, coordinator, 13 members | `install_offload` and `offload()`; the rest lives on `Distributed::Cluster` and the coordinator |
| `api/runtime/cluster.hpp`, `Runtime::cluster` (`runtime.hpp:217-233`) | `distributed/cluster.hpp`: `static Cluster Cluster::attach(API::Runtime& runtime, transport, kernels, policy = nullptr)`; it builds the coordinator (which now owns its `BufferRegistry`), calls `install_offload` and sets the release observer. Same knobs and counters as today, read from the coordinator |
| `Buffer::distributed()` (`buffer.hpp:382-403`), `State::distributed()` (`state.hpp:184-204`) | unconditional: `if (auto* offload = ctx_->offload()) offload->share(data(), bytes);`, a no-op with no coordinator (DIST-23) |
| `run_report_builder.hpp:80-85` | unconditional: fills from `ctx.offload()->statistics()` when a coordinator is installed |

Result: no header of `task`, `scheduler`, `graph`, `execution` or `api` changes shape with the flag, so a consumer cannot mismatch layouts and API-09's check has nothing left to detect. `SR_DISTRIBUTED` survives only as a definition the `distributed` target gives its consumers. `distributed` includes `core`, `logging`, `sycl`, `memory`, `device`, `task`, `graph`, `execution`, `api`, and nothing includes it.

One spelling breaks without a forwarder: `rt.cluster(...)` becomes `Distributed::Cluster::attach(rt, ...)`, because `api` can no longer name a transport. No downstream repository uses the distributed layer (grep for `SR_DISTRIBUTED` and `Distributed::` in all three found nothing). `register_distributed_buffer` on the context becomes `ctx.offload()->share(p, n)`; `bump_distributed_buffer` and `distributed_buffer(p)`, used by `test_distributed_e2e.cpp:463-776`, move to the cluster handle.

**Per-node cost with no coordinator installed.** One load of `TaskScheduler::offload_` and one never-taken branch at dispatch; one load of `EventPolling::offload_` and one never-taken branch at completion. No virtual call, no allocation, no touch of the node's offload pointer. Against today's flag-off build that is two loads and two branches added per node. Against today's flag-on build it is the same at dispatch and replaces a `std::function` test at completion. `offload_` must sit with the scheduler's read-mostly fields, not beside `pending_count_`. The pointer is written only while the pool is disarmed, so it needs no atomic. `test_scheduler_dispatch_perf` before and after will say whether two branches show; I expect them inside the spread, and that is a guess until measured.

With a coordinator installed: one virtual call per locally completed node (was a `std::function` call) and one per offloadable node at dispatch (was a direct call).

Removed in flag-on builds, on the construction path: one `NodeExtensions` allocation and two vector growths per tracked node, which today happen in Standalone role too.

### 5. Dependencies per module

| Tier | Module | Includes |
| --- | --- | --- |
| foundation | `core` | nothing |
| foundation | `logging` | nothing |
| foundation | `sycl` | nothing |
| platform | `topology` | `core`, `logging`, `sycl` |
| platform | `memory` | `core`, `logging`, `sycl` |
| platform | `device` | `core`, `logging`, `sycl` |
| platform | `observability` | `core`, `sycl` |
| engine | `task` | `core`, `sycl`, `memory`, `observability` |
| engine | `scheduler` | `core`, `logging`, `sycl`, `topology`, `memory`, `device`, `observability`, `task` |
| engine | `graph` | `core`, `logging`, `sycl`, `memory`, `observability`, `task` |
| engine | `execution` | `core`, `logging`, `sycl`, `topology`, `memory`, `device`, `observability`, `task`, `scheduler`, `graph` |
| surface | `interop` | `core`, `sycl`, `memory`, `device`, `execution` |
| surface | `api` | `core`, `logging`, `sycl`, `topology`, `memory`, `device`, `observability`, `task`, `graph`, `execution`, `interop` |
| extension | `distributed` | `core`, `logging`, `sycl`, `memory`, `device`, `task`, `graph`, `execution`, `api` |

Acyclic: the row order above is a topological order. Inside the engine tier the order is `task`, then `scheduler` and `graph`, then `execution`. No edge points up a tier.

Deviations from the approved table, each for the owner to rule on:

1. **`observability` moves from the engine tier to the platform tier.** `task` (`dispatch_width`), `scheduler` (`profiling_ledger`, `event_timestamps`), `graph` (`CompiledGraph` holds a `ProfilingLedger`) and `execution` all include it, and it includes only `core` and `sycl`. Left in the engine tier it needs four extra listed edges; moved, it needs none.
2. **One extra engine edge, `execution -> task`.** `execution_engine.cpp`, `chain_run.cpp` and `chain_plan.cpp` name `Node` fields directly. Whether an include that is reachable transitively through `graph` must be declared is the CMake designer's rule; I list it so the table is true either way.
3. **One surface edge, `api -> interop`** (`Runtime::interop()` and the `API::Interop` facade). The approved text lists edges for the engine tier only.
4. **Contents differ from the "Holds" column in three places.** `CompiledGraph` and the `ChainPlan` struct go to `graph`, not `execution` (3.2). `PinPolicy` and `WorkerPoolInfo` go to `scheduler` (3.1). `RangeTask` stays in `scheduler`.
5. **`Cluster` leaves `api` for `distributed`**, and `rt.cluster()` goes with no forwarder (section 4).
6. **Namespaces do not follow modules in this pass** (top of the report).

One constraint for the other two designers. A forwarding header for a header that moved up a tier cannot live in the module it left: `core/run_report.hpp` and `core/dispatch_width.hpp` (SushiEngine includes both) forward to `observability`; `core/resource_region.hpp` (12 SushiBLAS files) forwards to `memory`; `core/logger.hpp` and `core/sycl_compat.hpp` likewise. Inside `core` each would be an upward include. They need an include root that consumers see and no module is compiled with. Forwarders for headers that moved down (`graph/task_types.hpp`, `graph/node.hpp`, `scheduler/task.hpp`) are legal where they are.

### 6. Order of work

Every step ends with `sr build --distributed` and `sr test`. All of it can run in today's single-library tree with forwarding headers at the old paths, so the physical move into `modules/` that follows is a pure move.

| Step | Kind | What | Files | Waits on |
| --- | --- | --- | --- | --- |
| 1 | code | Delete dead includes and the unused friend | `core/error.hpp:35`, `core/ref_counted.hpp:33`, `task_graph.cpp:41`, `task_scheduler.hpp:92`, plus any file the build names | nothing |
| 2a | pure move | `task` headers of section 2 except the offload ones; `snapshot_widths` body | new `task/*.hpp`, `src/task/node.cpp`; old paths become forwarders | 1 |
| 2b | pure move | `PinPolicy`, `WorkerPoolInfo` to `scheduler`; aliases | `scheduler/pin_policy.hpp`, `worker_pool_info.hpp`, `runtime_settings.hpp` | 1 |
| 2c | pure move | `CompiledGraph`, `ChainPlan`, `PendingEvent` to `graph`; aliases | `graph/compiled_graph.hpp`, `graph/chain_plan.hpp`, `src/graph/compiled_graph.cpp`, `execution_engine.{hpp,cpp}`, `execution/chain_plan.hpp` | 2a |
| 2d | pure move | `API::Interop`, `HostExport` to `api`; alias | `api/interop/*.hpp`, `interop/interop.hpp`, `interop/host_export.hpp`, `runtime.hpp:39` | 1 |
| 3a-3e | code, new files only | The four bricks and the seam headers, each with a unit test | 3a `device/*`, 3b `memory/usm_allocator*`, `usm_request.hpp`, `usm_release_observer.hpp`, 3c `observability/transfer_counters.hpp`, 3d `topology/topology_view.hpp`, 3e `task/offload_*.hpp` | 1 (3e: 2a) |
| 4 | code | `RuntimeContext` rebuilt on the bricks; forwarders without the attribute; `probe_hardware` removed; `UsmAllocationOwner` on the allocator | `runtime_context.{hpp,cpp}`, `usm_allocation_owner.hpp`, `buffer.hpp:157`, `interop.cpp:229` | 3a-3d |
| 5 | code | Scheduler off the context | `task_scheduler.hpp`, `scheduler.cpp`, `worker_pool_settings.hpp`, `execution_engine.cpp:140`, the 14 test sites, a helper in `tests/common` | 2b, 4 |
| 6 | code | Per-node offload state on the seam type; `prepare` in the coordinator, still reached through the context under the flag; add-time fills and `rearm` blocks deleted | `task/node.hpp`, `task_graph.cpp`, `distributed_coordinator.{hpp,cpp}`, `scheduler.cpp:1150-1224`, `execution_engine.cpp:592-596` (by then in `compiled_graph.cpp`), `dynamic_graph.hpp:927-932` | 2c, 3e, 5 |
| 7 | code | Control path unconditional: coordinator derives from the interface, scheduler implements `OffloadSink`, `TaskGraph` constructor, `install_offload`, release observer; every `#ifdef` below `api` gone | `task_scheduler.hpp`, `scheduler.cpp`, `event_polling.hpp`, `execution_engine.{hpp,cpp}`, `runtime_context.{hpp,cpp}`, `task_graph.{hpp,cpp}`, `distributed_coordinator.{hpp,cpp}`, `worker_agent.cpp:397`, `graph.hpp` (backing graph construction) | 6 |
| 8 | code | Surface: `Cluster` to `distributed`, `distributed()` and the report unconditional, the 13 context members removed | `distributed/cluster.hpp`, `api/runtime/cluster.hpp` (deleted), `runtime.hpp`, `buffer.hpp`, `state.hpp`, `run_report_builder.hpp`, `runtime_context.{hpp,cpp}`, three distributed tests, `test_distributed_offload_perf.cpp`, `examples/distributed_demo.cpp` | 7 |
| 9 | code, mechanical | In-repo callers to the new spellings, then `[[deprecated]]` on the forwarders | `tests/**` (about 110 call sites by my count), `api/**`, `interop.cpp`, `worker_agent.cpp`, `chain_plan.cpp:74`, `chain_run.cpp:60` | 4; disjoint from 6-8 except `worker_agent.cpp` and the distributed tests, so run it after 8 |
| 10 | pure move | Into `modules/<tier>/<module>/`, tier table in `cmake/`, checker switched on | the move map | 9 |

Parallel sets: {2a, 2b, 2d}; {3a, 3b, 3c, 3d} at once and 3e after 2a; then 4 through 9 in sequence, since 4-8 all pass through `runtime_context.cpp` or `scheduler.cpp`. Step 9 can be split by folder across workers.

Benchmarks: run `test_scheduler_dispatch_perf`, `test_chain_dispatch_perf` and `test_graph_compile_perf` with phase A's five-run harness before step 4 and after steps 5 and 7, the two steps that touch the dispatch loop.

### Risks

- **Transitive includes.** Removing `error.hpp`'s and `ref_counted.hpp`'s includes (step 1) will break files that leaned on them, here and possibly downstream (SushiAI includes `core/error.hpp`). A build lists them.
- **Offload equivalence.** `prepare()` reading `io_reads_` / `io_writes_` instead of the copied lists is equal by the code I read at `task_graph.cpp:316-330` and 444-463. I did not read the bodies at 381-395 and 579-592; grep shows the same two lines there. The three distributed test files decide.
- **`attempts` reset moved from `rearm` to completion.** Equal if every node reaches `poll_events` once per run, which holds for skipped and failed nodes too (`scheduler.cpp:651`, 681). A node still pending remotely when a run is cancelled is the case I could not reason through from the code.
- **Forward declarations of `Execution::CompiledGraph` in downstream code** would clash with the alias. Grep found only uses by value and by reference, but I did not search every file of the three repositories for `struct CompiledGraph;`.
- **Exported classes with standard-library members** (`DeviceQueueSet`, `UsmAllocator`) on Windows with `WINDOWS_EXPORT_ALL_SYMBOLS`: the tree already does this (`TaskScheduler`), so I expect no warning; the build confirms.
- **The allocator keeps SYCL contexts alive** through its queue copies for as long as a `Buffer` lives. Today the `Buffer` keeps the whole context alive, so this is narrower, not wider; the ASan run is the check.
- **`wait_all()` waiting every built queue** is a small behaviour change (EXEC-10): today it waits the plain family only.
- **`Buffer::distributed()` before a cluster is attached** becomes a silent no-op where it throws today ("requires Master mode", cpp 916). DIST-23 asks for the no-op; a test may pin the throw.
- **The two added branches per node** are a prediction until phase E measures them.
- **AdaptiveCpp and oneAPI** were not considered beyond SYCL 2020 semantics; the probe ran on intel-llvm only.

### Not read

`api/graph/graph.hpp` and `dynamic_graph.hpp` beyond the grep hits and the ranges cited; `chain_plan.cpp` and `chain_run.cpp` bodies beyond the hits; `dependency_tracker.{hpp,cpp}`; `epoch_reclamation.hpp`; `work_stealing_deque.hpp`, `workload_coordinator.hpp`, `thermal_proxy_strategy.hpp`; all of `src/topology`; the transports, `buffer_registry`, `kernel_registry`, `protocol.hpp` past `BufferHandle`, and `worker_agent.cpp` outside 380-455; `scheduler.cpp` 300-540 and 908-1015; `interop.cpp` beyond the grep; every test file (counts only); the module READMEs; CMake files. In `REPORT.md` I read LAYOUT facts and LAYOUT-01 to 03, EXEC facts and EXEC-01 to 11, CORE-08 to 16, DIST-23, API-09; the prior-audit notes on the weave only as those findings cite them.

### Evidence for the dispatch block

1. **SOLID.** Each brick has one job and one file pair, is constructed from values or lower-tier types only (`DeviceQueueSet` from descriptors, `UsmAllocator` from queue copies, `TopologyView` from the two topology interfaces), and can be rebuilt without `RuntimeContext`. Siblings match: the four bricks are `final` classes with deleted copies and noun accessors on the context; the two seam interfaces are abstract with no data. Module boundaries I changed from the approved table are listed in section 5 as the owner's decisions, not taken silently.
2. **Humanizer.** Loaded with `generic_english.md` and applied to this report: facts and line numbers in place of evaluative words, no closing summary, uncertainty named once in Risks.
3. No `sr`, `se`, `cmake`, `ninja` or `ctest` process started; no build file touched.
4. Nothing written in the repository, including `docs/agent/`.
5. Done: the six deliverables. Not done: no benchmark run, no downstream build, files under Not read.
6. **Syntax check.** No source written to the repository; nothing there to check. I did check the declaration sketches of sections 1, 3.2, 3.4 and 4 as one scratch file against the real headers (it included `graph/node.hpp`, `memory/usm_registry.hpp`, `memory/allocation.hpp`, `topology/i_topology_provider.hpp`, `core/span.hpp`), with `SR_DISTRIBUTED` not defined:

   ```
   $ D:/Projects/sushistack/dependencies/toolchains/llvm-sycl/bin/clang++.exe -fsyntax-only -DNOMINMAX -DSR_DETERMINISTIC_FP=1 -DWIN32_LEAN_AND_MEAN -ID:/Projects/sushiruntime/include -isystem D:/Projects/sushistack/dependencies/vcpkg/installed/x64-windows/include -fsycl -std=gnu++17 -D_DLL -D_MT -Wall -Wextra -Werror -pedantic -ffp-contract=off -fno-fast-math probe.cpp; echo "exit=$?"
   exit=0
   ```

   The flags are those of `build/compile_commands.json` minus the export defines, the GPU target and `-O3`. The probe shows the sketches parse and type-check in C++17, including the deprecated constructor over a forward-declared type and the `Interop::HostExport` alias template. It says nothing about the bodies still to be written.
7. **Scratch.** One file, `scratchpad/b2/probe.cpp` (about 10 KB), no binary, deleted with its folder. Scratchpad after: `du -sh` reports 4.0K, empty. A folder `b1` from another agent was there when I started and was gone when I finished; I did not touch it.

## Part 3: build design

Read-only. Nothing was written in the repository or in the scratchpad. No `sr`, `cmake`, `ninja` or `ctest` process was started. Every statement about what CMake does comes from reading files, including the installed `GoogleTest.cmake` of CMake 4.3.3 at `D:/Projects/sushistack/dependencies/tools/cmake/share/cmake-4.3/Modules/`.

Three findings change what the orchestrator must plan around:

1. **`SR_INSTALL=OFF` when embedded breaks SushiBLAS and SushiAI at generate time** unless they opt back in. Both export a target that links `SushiRuntime::SushiRuntime`, and CMake refuses an export whose target requires a target that is in no export set (the rule `src/CMakeLists.txt:160-169` already cites). The fix is one line in each parent, and it can land before the runtime changes (section 5).
2. **Hidden visibility cannot be set on the shared library alone.** Visibility and `dllexport` are compile-time facts, so they go on every OBJECT library. `VISIBILITY_INLINES_HIDDEN` also gives Linux the per-image inline statics Windows already has. `memory` is header-only and holds the epoch and USM registries, so this needs its own step and a build of the three consumers (section 4).
3. **Forwarding headers cannot live in the module of the old path.** `core/logger.hpp` forwarding to `logging/` is an upward include from `core` and fails the module's own compile. They are generated from one table into a separate include root that only the product target carries (section 4).

Two items need the owner's ruling: the presets question (section 6; I recommend deleting them) and whether forwarders stay silent by default (section 4).

### 1. One module function

#### Targets

Each module becomes up to three targets. None is exported or linked by the product.

| Target | Kind | Carries |
| --- | --- | --- |
| `sushiruntime_module_<name>` | INTERFACE | The module's `include/` root (`BUILD_INTERFACE`), INTERFACE links to `sushiruntime_module_<dep>` for each `DEPENDS`, and the SYCL flag targets when the module is marked `SYCL` |
| `sushiruntime_module_<name>_objects` | OBJECT | The sources; absent for `HEADER_ONLY` |
| `sushiruntime_module_<name>_header_check` | OBJECT | One generated translation unit per public header; only under `SR_BUILD_TESTS` |

The `module_` infix keeps a module name from colliding with `sushiruntime_warnings`, `sushiruntime_perf`, `sushiruntime_sanitizers`, `sushiruntime_gpu_offload` and `sushiruntime_deterministic_fp`.

The product stays `add_library(sushiruntime SHARED)` with alias `SushiRuntime::SushiRuntime`. It is composed from `$<TARGET_OBJECTS:...>` of the recorded object libraries. It reads global properties and links no module target, so the export set names nothing outside itself. SushiBLAS uses this shape (`D:\Projects\sushiblas\cmake\SushiBLASModule.cmake`, `modules/library/sushiblas/CMakeLists.txt:36-43`). The lowercase name `sushiruntime` must stay: SushiEngine links it (`engine/foundation/execution/CMakeLists.txt:30`, `cmake/Install.cmake:316`).

Global properties the function appends to:

- `SUSHIRUNTIME_MODULE_OBJECT_TARGETS`
- `SUSHIRUNTIME_INSTALLED_INCLUDE_ROOTS`
- `SUSHIRUNTIME_INTERNAL_INCLUDE_ROOTS`
- `SUSHIRUNTIME_MODULE_EXTERNAL_LIBRARIES` (the link of an OBJECT library's third-party dependency does not travel through `TARGET_OBJECTS`, so the product links these itself)
- `SUSHIRUNTIME_MODULE_SYCL_<name>`

#### Include roots: transitive, with the checker closing the gap

A module compiles with its own `include/`, its own `source/`, and the `include/` roots of its declared dependencies and of theirs. Non-transitive roots cannot work: if `graph` includes a `task` header that includes a `core` header, the compiler needs `core`'s root to compile `graph`. So an undeclared include fails to compile unless the module is already in the transitive closure. A new checker rule, `rule_declared_dependencies` (section 2), catches that remaining case.

`PRIVATE_DEPENDS` names modules only `source/` includes. They reach the object library and not the interface target, so they do not widen the closure of dependants. The header check compiles with `DEPENDS` only, which makes "a public header includes only public dependencies" a compile error.

#### SYCL

Today `cmake/Sycl.cmake:140-141` and `cmake/gpu/GpuBackend.cmake:246-247` set flags at directory scope, and `src/CMakeLists.txt:119-137` keeps a hand list for the oneAPI path. All three go.

- `sushiruntime_sycl_flags` (alias and export name `SushiRuntime::sycl`): `-fsycl` compile and link options on intel-llvm; empty on oneapi and adaptivecpp.
- `sushiruntime_gpu_offload` (exists; alias `SushiRuntime::gpu_offload`): the `-fsycl-targets` list and backend options. Toolkit paths such as `--cuda-path=` move to a second accumulator rendered under `$<BUILD_INTERFACE:...>`, so they never reach an installed consumer.
- `sushiruntime_apply_sycl(<target> [SOURCES ...])`: links both targets PRIVATE under `BUILD_INTERFACE` and calls `add_sycl_to_target`, which stays a no-op shim on intel-llvm.

`SYCL` is declared once, on the `sycl` module. Every module with `sycl` in its `DEPENDS` chain inherits the flags through the INTERFACE links, and the function calls `sushiruntime_apply_sycl` on its object library with its sources. A module that does not depend on `sycl` compiles without `-fsycl`.

`--gcc-toolchain=${SR_GCC_TOOLCHAIN}` is not a SYCL flag. Every translation unit must see the same libstdc++, so it moves to a policy target, `sushiruntime_toolchain`, applied by the build policy to every in-tree target.

#### Build policy, attached once

`sushiruntime_apply_build_policy(<target>)` stays the single entry. It links `sushiruntime_warnings`, `sushiruntime_perf`, `sushiruntime_sanitizers` and `sushiruntime_toolchain` PRIVATE under `BUILD_INTERFACE`, and sets `CXX_STANDARD 17`, `CXX_STANDARD_REQUIRED ON`, `POSITION_INDEPENDENT_CODE ON`. From the visibility step on it also sets `CXX_VISIBILITY_PRESET hidden` and `VISIBILITY_INLINES_HIDDEN ON`. Properties are used instead of `target_compile_features` because the intel/llvm nightly is newer than CMake's feature database (`tests/CMakeLists.txt:29-42`). The module function, the program function and the product call it; nothing else does. The deterministic FP target is linked PRIVATE by each object library and PUBLIC by the product.

#### `cmake/SushiRuntimeModule.cmake`

```cmake
include("${CMAKE_CURRENT_LIST_DIR}/SushiRuntimeTiers.cmake")

# Refuses a dependency the tier table does not allow.
function(sushiruntime_check_module_edge consumer dependency)
    sushiruntime_module_tier("${dependency}" dependency_tier)
    if(dependency_tier STREQUAL "")
        message(FATAL_ERROR
            "sushiruntime_add_module(${consumer}): depends on '${dependency}', which "
            "SUSHIRUNTIME_MODULE_TIERS (cmake/SushiRuntimeTiers.cmake) does not list.")
    endif()
    sushiruntime_module_tier("${consumer}" consumer_tier)
    sushiruntime_tier_index("${consumer_tier}" consumer_index)
    sushiruntime_tier_index("${dependency_tier}" dependency_index)
    if(dependency_index GREATER consumer_index)
        message(FATAL_ERROR
            "sushiruntime_add_module(${consumer}): upward dependency ${consumer} "
            "(${consumer_tier}) -> ${dependency} (${dependency_tier}). Move the shared "
            "code down a tier.")
    endif()
    if(dependency_index EQUAL consumer_index)
        sushiruntime_same_tier_edge_allowed("${consumer}" "${dependency}" allowed)
        if(NOT allowed)
            message(FATAL_ERROR
                "sushiruntime_add_module(${consumer}): undeclared edge ${consumer} -> "
                "${dependency} inside the ${consumer_tier} tier. List it in "
                "SUSHIRUNTIME_SAME_TIER_EDGES (cmake/SushiRuntimeTiers.cmake) or move "
                "the shared code down a tier.")
        endif()
    endif()
    if(NOT TARGET sushiruntime_module_${dependency})
        message(FATAL_ERROR
            "sushiruntime_add_module(${consumer}): '${dependency}' is not declared yet. "
            "It is switched off, or it comes after ${consumer} in "
            "SUSHIRUNTIME_MODULE_TIERS; a dependency is listed before its consumer.")
    endif()
endfunction()

# sushiruntime_add_module(
#     NAME <module> TIER <tier>
#     [HEADER_ONLY] [INTERNAL] [SYCL]
#     [SOURCES <file under source/>...]
#     [SOURCES_WINDOWS <file>...] [SOURCES_LINUX <file>...]
#     [DEPENDS <module>...] [PRIVATE_DEPENDS <module>...]
#     [EXTERNAL <third-party target>...]
#     [HEADER_CHECK_EXCLUDE <header path under include/>...])
function(sushiruntime_add_module)
    cmake_parse_arguments(ARG "HEADER_ONLY;INTERNAL;SYCL" "NAME;TIER"
        "SOURCES;SOURCES_WINDOWS;SOURCES_LINUX;DEPENDS;PRIVATE_DEPENDS;EXTERNAL;HEADER_CHECK_EXCLUDE"
        ${ARGN})
    if("${ARG_NAME}" STREQUAL "" OR "${ARG_TIER}" STREQUAL "")
        message(FATAL_ERROR "sushiruntime_add_module(): NAME and TIER are required.")
    endif()
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "sushiruntime_add_module(${ARG_NAME}): unexpected '${ARG_UNPARSED_ARGUMENTS}'.")
    endif()
    sushiruntime_module_tier("${ARG_NAME}" table_tier)
    if(NOT "${ARG_TIER}" STREQUAL "${table_tier}")
        message(FATAL_ERROR
            "sushiruntime_add_module(${ARG_NAME}): declares TIER '${ARG_TIER}'; "
            "cmake/SushiRuntimeTiers.cmake says '${table_tier}'.")
    endif()

    set(sources "")
    foreach(source IN LISTS ARG_SOURCES)
        list(APPEND sources "${CMAKE_CURRENT_SOURCE_DIR}/source/${source}")
    endforeach()
    if(WIN32)
        foreach(source IN LISTS ARG_SOURCES_WINDOWS)
            list(APPEND sources "${CMAKE_CURRENT_SOURCE_DIR}/source/${source}")
        endforeach()
    else()
        foreach(source IN LISTS ARG_SOURCES_LINUX)
            list(APPEND sources "${CMAKE_CURRENT_SOURCE_DIR}/source/${source}")
        endforeach()
    endif()
    if(ARG_HEADER_ONLY AND sources)
        message(FATAL_ERROR "sushiruntime_add_module(${ARG_NAME}): HEADER_ONLY and SOURCES exclude each other.")
    endif()
    if(NOT ARG_HEADER_ONLY AND NOT sources)
        message(FATAL_ERROR "sushiruntime_add_module(${ARG_NAME}): no SOURCES; mark it HEADER_ONLY.")
    endif()

    foreach(dependency IN LISTS ARG_DEPENDS ARG_PRIVATE_DEPENDS)
        sushiruntime_check_module_edge("${ARG_NAME}" "${dependency}")
    endforeach()

    set(target "sushiruntime_module_${ARG_NAME}")
    set(include_root "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_library(${target} INTERFACE)
    target_include_directories(${target} INTERFACE "$<BUILD_INTERFACE:${include_root}>")

    set(interface_sycl ${ARG_SYCL})
    foreach(dependency IN LISTS ARG_DEPENDS)
        target_link_libraries(${target} INTERFACE "sushiruntime_module_${dependency}")
        get_property(dependency_sycl GLOBAL PROPERTY SUSHIRUNTIME_MODULE_SYCL_${dependency})
        if(dependency_sycl)
            set(interface_sycl TRUE)
        endif()
    endforeach()
    if(ARG_SYCL)
        target_link_libraries(${target} INTERFACE
            sushiruntime_sycl_flags sushiruntime_gpu_offload IntelSYCL::SYCL_CXX)
    endif()
    set_property(GLOBAL PROPERTY SUSHIRUNTIME_MODULE_SYCL_${ARG_NAME} ${interface_sycl})

    if(ARG_INTERNAL)
        set_property(GLOBAL APPEND PROPERTY SUSHIRUNTIME_INTERNAL_INCLUDE_ROOTS "${include_root}")
        if(NOT TARGET sushiruntime_internal_headers)
            add_library(sushiruntime_internal_headers INTERFACE)
        endif()
        target_link_libraries(sushiruntime_internal_headers INTERFACE ${target})
    else()
        set_property(GLOBAL APPEND PROPERTY SUSHIRUNTIME_INSTALLED_INCLUDE_ROOTS "${include_root}")
    endif()

    if(sources)
        set(objects_sycl ${interface_sycl})
        set(objects "${target}_objects")
        add_library(${objects} OBJECT ${sources})
        target_link_libraries(${objects} PRIVATE ${target} sushiruntime_deterministic_fp ${ARG_EXTERNAL})
        foreach(dependency IN LISTS ARG_PRIVATE_DEPENDS)
            target_link_libraries(${objects} PRIVATE "sushiruntime_module_${dependency}")
            get_property(dependency_sycl GLOBAL PROPERTY SUSHIRUNTIME_MODULE_SYCL_${dependency})
            if(dependency_sycl)
                set(objects_sycl TRUE)
            endif()
        endforeach()
        target_include_directories(${objects} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")
        target_compile_definitions(${objects} PRIVATE
            SR_BUILDING_LIBRARY SUSHIRUNTIME_EXPORTS NOMINMAX WIN32_LEAN_AND_MEAN
            $<$<BOOL:${SR_ENABLE_DISTRIBUTED}>:SR_DISTRIBUTED>)
        sushiruntime_apply_build_policy(${objects})
        if(objects_sycl)
            sushiruntime_apply_sycl(${objects} SOURCES ${sources})
        endif()
        set_property(GLOBAL APPEND PROPERTY SUSHIRUNTIME_MODULE_OBJECT_TARGETS ${objects})
        set_property(GLOBAL APPEND PROPERTY SUSHIRUNTIME_MODULE_EXTERNAL_LIBRARIES ${ARG_EXTERNAL})
    endif()

    if(SR_BUILD_TESTS)
        sushiruntime_add_header_check(
            MODULE ${ARG_NAME} INCLUDE_ROOT "${include_root}"
            SYCL ${interface_sycl} EXCLUDE ${ARG_HEADER_CHECK_EXCLUDE})
    endif()
endfunction()
```

`sushiruntime_add_header_check` lives in the same file. It:

1. globs `*.hpp` and `*.h` under the include root with `CONFIGURE_DEPENDS`, minus `EXCLUDE`;
2. writes one `${CMAKE_CURRENT_BINARY_DIR}/header_check/<id>.cpp` per header with `file(CONFIGURE ...)`, each holding the single line `#include <SushiRuntime/<module>/<name>.hpp>`;
3. builds them as the OBJECT library `sushiruntime_module_<name>_header_check`, linked PRIVATE to the module's interface target and `sushiruntime_deterministic_fp`, with the build policy, the `SR_DISTRIBUTED` definition, and `sushiruntime_apply_sycl` when `SYCL` is true.

It runs for every module, header-only or not: `scheduler` has eight headers and one source, so its sources do not prove each header self-contained. The glob is deliberate here, because the check must not depend on someone remembering to list a header. Sources stay explicit lists (LAYOUT-15).

Two module files as they would read:

```cmake
sushiruntime_add_module(
    NAME sycl TIER foundation
    HEADER_ONLY SYCL
    DEPENDS core)
```

```cmake
sushiruntime_add_module(
    NAME topology TIER platform
    SOURCES
        hw_discovery.cpp
        hwloc_topology_provider.cpp
        numa_mapper.cpp
        remapping_topology_provider.cpp
    SOURCES_WINDOWS affinity_windows.cpp
    SOURCES_LINUX   affinity_linux.cpp
    DEPENDS core logging
    EXTERNAL PkgConfig::HWLOC)

sushiruntime_add_module_tests(
    MODULE topology
    SOURCES
        test_affinity.cpp
        test_topology.cpp)
```

The `DEPENDS` lists above are examples. The real ones come from the include-cycle design.

#### `cmake/SushiRuntimeLibrary.cmake`

One function, `sushiruntime_compose_library()`, called at the end of `modules/CMakeLists.txt`:

```cmake
function(sushiruntime_compose_library)
    add_library(sushiruntime SHARED)
    add_library(SushiRuntime::SushiRuntime ALIAS sushiruntime)

    get_property(object_targets GLOBAL PROPERTY SUSHIRUNTIME_MODULE_OBJECT_TARGETS)
    get_property(include_roots  GLOBAL PROPERTY SUSHIRUNTIME_INSTALLED_INCLUDE_ROOTS)
    get_property(externals      GLOBAL PROPERTY SUSHIRUNTIME_MODULE_EXTERNAL_LIBRARIES)
    foreach(objects IN LISTS object_targets)
        target_sources(sushiruntime PRIVATE "$<TARGET_OBJECTS:${objects}>")
    endforeach()
    foreach(include_root IN LISTS include_roots)
        target_include_directories(sushiruntime PUBLIC "$<BUILD_INTERFACE:${include_root}>")
    endforeach()
    target_include_directories(sushiruntime PUBLIC
        "$<BUILD_INTERFACE:${SR_FORWARDING_INCLUDE_ROOT}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

    target_link_libraries(sushiruntime PUBLIC
        sushiruntime_deterministic_fp IntelSYCL::SYCL_CXX Threads::Threads)
    list(REMOVE_DUPLICATES externals)
    foreach(external IN LISTS externals)
        target_link_libraries(sushiruntime PRIVATE "$<BUILD_INTERFACE:${external}>")
    endforeach()
    if(SR_ENABLE_DISTRIBUTED)
        target_compile_definitions(sushiruntime PUBLIC SR_DISTRIBUTED)
    endif()

    sushiruntime_apply_build_policy(sushiruntime)
    sushiruntime_apply_sycl(sushiruntime)

    if(PROJECT_VERSION_MAJOR EQUAL 0)
        set(soversion "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
    else()
        set(soversion "${PROJECT_VERSION_MAJOR}")
    endif()
    set_target_properties(sushiruntime PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${soversion}
        LINKER_LANGUAGE CXX
        OUTPUT_NAME "sushiruntime"
        EXPORT_NAME SushiRuntime
        RUNTIME_OUTPUT_DIRECTORY "${SR_RUNTIME_OUTPUT_DIR}")
endfunction()
```

The long comment at `src/CMakeLists.txt:83-107` on why `SR_DISTRIBUTED` is PUBLIC moves to the `distributed` module's README. The distributed module passes `EXTERNAL "$<$<PLATFORM_ID:Windows>:ws2_32>"`.

### 2. The tier table

#### `cmake/SushiRuntimeTiers.cmake` (full)

```cmake
# Lowest tier first. A module may depend on a module of a lower tier.
set(SUSHIRUNTIME_TIER_ORDER
    foundation
    platform
    engine
    surface
    extension)

# Module, then its tier. Inside a tier a dependency is listed before its consumer:
# modules/CMakeLists.txt adds the folders in this order.
set(SUSHIRUNTIME_MODULE_TIERS
    core            foundation
    logging         foundation
    sycl            foundation
    topology        platform
    memory          platform
    device          platform
    observability   engine
    task            engine
    scheduler       engine
    graph           engine
    execution       engine
    interop         surface
    api             surface
    distributed     extension)

# Consumer, then dependency: the edges allowed between two modules of one tier.
set(SUSHIRUNTIME_SAME_TIER_EDGES
    scheduler       task
    graph           task
    execution       scheduler
    execution       graph)

# Module, then the option that switches it on. A module not listed is always built.
set(SUSHIRUNTIME_OPTIONAL_MODULES
    distributed     SR_ENABLE_DISTRIBUTED)
```

Below the tables are four functions shaped like those in `SushiBLASLayers.cmake:59-98`:

- `sushiruntime_tier_index(tier out)`
- `sushiruntime_module_tier(module out)`
- `sushiruntime_same_tier_edge_allowed(consumer dependency out)`
- `sushiruntime_enabled_module_folders(out)`, which returns `tier/module` for each module whose option is on or absent, in table order.

The four engine edges are the ones `PLAN.md` approves. The cycle design will add further same-tier edges (likely `logging -> core`, `sycl -> core`, edges into `observability`, `api -> interop`); each is one line here. I did not decide them.

#### `modules/CMakeLists.txt` (full)

```cmake
sushiruntime_enabled_module_folders(module_folders)
foreach(module_folder IN LISTS module_folders)
    add_subdirectory("${module_folder}")
endforeach()
sushiruntime_compose_library()
```

There are no per-tier `CMakeLists.txt` files. Adding a module is its folder plus one line in `SUSHIRUNTIME_MODULE_TIERS` (module rule 4).

#### The checker

`project-tools` says a checker is configured through `K_` constants and no configuration file, and `tools/README.md` says the checkers are copied whole from SushiSkills. So `check_layering.py` keeps copies and a test compares them, as SushiBLAS does in `tools/tests/test_tier_table.py`.

Changes to `tools/layering/check_layering.py`:

- `K_TIER_ORDER = ("foundation", "platform", "engine", "surface", "extension")`.
- `K_SAME_TIER_EDGES: frozenset[tuple[str, str]]` with the same pairs as the CMake table.
- `rule_same_tier_includes`: an include of a same-tier module whose edge is not listed.
- `rule_declared_dependencies`: parses `DEPENDS` and `PRIVATE_DEPENDS` from each module's `CMakeLists.txt` and reports an include of a module outside them. A file under `include/` may use `DEPENDS` only.
- OPS-19: `collect` returns the repository even without `modules/`, and `rule_declared_tiers` reports "no modules/ folder found" instead of passing silently.

The two new rules are local, as SushiBLAS's eight are. If the owner wants them in SushiSkills first, that precedes this step.

New `tools/tests/test_tier_table.py`, three tests:

1. `K_TIER_ORDER` equals `SUSHIRUNTIME_TIER_ORDER`.
2. `K_SAME_TIER_EDGES` equals the pairs of `SUSHIRUNTIME_SAME_TIER_EDGES`.
3. The folders `modules/*/*` equal `SUSHIRUNTIME_MODULE_TIERS`, each under its tier.

The third can only pass once every module has moved.

### 3. Tests

#### Labels

`tests/CMakeLists.txt:178-185` records that a two-element `LABELS` passed through `gtest_discover_tests(PROPERTIES ...)` is flattened. The installed module shows why: in PRE_TEST mode the properties are re-expanded from one bracket argument (`GoogleTest.cmake:739`).

The route the module documents for finer control is a CTest script on `TEST_INCLUDE_FILES` that reads `<target>_TESTS` (`GoogleTest.cmake:199-203`, `TEST_LIST` at `:259-263`). The function writes that script itself, so the label list never passes through the module. If a configure shows the list variable is not visible there, the fallback is one composite label such as `unit.scheduler`, with the CLI matching by regex.

Kind is a property of the program, not of a name prefix. That closes LAYOUT-16: a suite named without `Unit_` still runs.

| Label | Assigned to | CLI |
| --- | --- | --- |
| `unit`, `integration`, `regression`, `stress`, `performance` | Every test of the program, from `KIND` | `sr test --suite <kind>`; `functional` and `benchmark` stay regex umbrellas |
| `<module>` | Every test of a module's program | `sr test --module scheduler` (new, TEST-10) |
| `needs_device` | Tests matched by `DEVICE_FILTER` | `sr test --suite device` (new, TEST-09) |
| `package` | The two packaging tests | unchanged |

The device label is `needs_device`, not `device`, because `device` is a module name. `DEVICE_FILTER` is a GoogleTest filter stated in the build file, so no test is renamed. A device test still self-skips without hardware and CTest reports it as skipped, because the module sets `SKIP_REGULAR_EXPRESSION "\[  SKIPPED \]"` (`GoogleTestAddTests.cmake:133`).

#### `cmake/SushiRuntimeProgram.cmake` (full)

```cmake
# One folder for the library's runtime file and every program, so on Windows a program
# finds sushiruntime.dll beside it without a copy step.
if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(SR_RUNTIME_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
else()
    set(SR_RUNTIME_OUTPUT_DIR "${PROJECT_BINARY_DIR}/bin")
endif()

# sushiruntime_add_program(NAME <target> SOURCES <file>...)
function(sushiruntime_add_program)
    cmake_parse_arguments(ARG "" "NAME" "SOURCES" ${ARGN})
    if("${ARG_NAME}" STREQUAL "" OR NOT ARG_SOURCES)
        message(FATAL_ERROR "sushiruntime_add_program(): NAME and SOURCES are required.")
    endif()
    add_executable(${ARG_NAME} ${ARG_SOURCES})
    target_link_libraries(${ARG_NAME} PRIVATE sushiruntime)
    sushiruntime_apply_build_policy(${ARG_NAME})
    sushiruntime_apply_sycl(${ARG_NAME} SOURCES ${ARG_SOURCES})
    set_target_properties(${ARG_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${SR_RUNTIME_OUTPUT_DIR}")
endfunction()
```

This closes LAYOUT-24 by removing the staging. The two `add_custom_target(... copy_if_different ...)` blocks (`tests/CMakeLists.txt:52-67`, `examples/CMakeLists.txt:23-40`) disappear, and with them the race they describe. Examples call `sushiruntime_add_program` directly; `sushiruntime_add_example` is deleted.

#### `cmake/SushiRuntimeTests.cmake` (full, without the packaging function)

```cmake
if(NOT SR_BUILD_TESTS)
    function(sushiruntime_add_test_program)
    endfunction()
    function(sushiruntime_add_module_tests)
    endfunction()
    return()
endif()

enable_testing()
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

# The imported GTest targets ask for cxx_std_14, which a compiler newer than CMake's
# feature table cannot answer; the tree pins C++17 itself.
foreach(gtest_target GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)
    if(TARGET ${gtest_target})
        set_target_properties(${gtest_target} PROPERTIES INTERFACE_COMPILE_FEATURES "")
    endif()
endforeach()

set(SR_TEST_COMMON_DIR "${PROJECT_SOURCE_DIR}/tests/common")
set(SR_TEST_KINDS unit integration regression stress performance)
set(SR_TEST_DEVICE_LABEL needs_device)

# sushiruntime_add_test_program(
#     NAME <target> KIND <kind> [MODULE <module>]
#     [DEVICE_FILTER <gtest filter>] [TIMEOUT <seconds>]
#     SOURCES <file>...)
function(sushiruntime_add_test_program)
    cmake_parse_arguments(ARG "" "NAME;KIND;MODULE;DEVICE_FILTER;TIMEOUT" "SOURCES" ${ARGN})
    if("${ARG_NAME}" STREQUAL "" OR NOT ARG_SOURCES)
        message(FATAL_ERROR "sushiruntime_add_test_program(): NAME and SOURCES are required.")
    endif()
    if(NOT "${ARG_KIND}" IN_LIST SR_TEST_KINDS)
        message(FATAL_ERROR "sushiruntime_add_test_program(${ARG_NAME}): KIND '${ARG_KIND}' is not one of ${SR_TEST_KINDS}.")
    endif()
    set(labels "${ARG_KIND}")
    set(serial FALSE)
    if(ARG_KIND STREQUAL "stress" OR ARG_KIND STREQUAL "performance")
        set(default_timeout 900)
        set(serial TRUE)
    else()
        set(default_timeout 120)
    endif()
    if("${ARG_TIMEOUT}" STREQUAL "")
        set(ARG_TIMEOUT ${default_timeout})
    endif()
    if(NOT "${ARG_MODULE}" STREQUAL "")
        sushiruntime_module_tier("${ARG_MODULE}" module_tier)
        if(module_tier STREQUAL "")
            message(FATAL_ERROR "sushiruntime_add_test_program(${ARG_NAME}): MODULE '${ARG_MODULE}' is not in cmake/SushiRuntimeTiers.cmake.")
        endif()
        list(APPEND labels "${ARG_MODULE}")
    endif()

    sushiruntime_add_program(NAME ${ARG_NAME}
        SOURCES "${SR_TEST_COMMON_DIR}/test_main.cpp" ${ARG_SOURCES})
    target_include_directories(${ARG_NAME} PRIVATE "${SR_TEST_COMMON_DIR}")
    target_link_libraries(${ARG_NAME} PRIVATE GTest::gtest)
    if(TARGET sushiruntime_internal_headers)
        target_link_libraries(${ARG_NAME} PRIVATE sushiruntime_internal_headers)
    endif()

    if("${ARG_DEVICE_FILTER}" STREQUAL "")
        gtest_discover_tests(${ARG_NAME}
            DISCOVERY_MODE PRE_TEST
            WORKING_DIRECTORY "${SR_RUNTIME_OUTPUT_DIR}"
            TEST_LIST ${ARG_NAME}_TESTS)
    else()
        gtest_discover_tests(${ARG_NAME}
            DISCOVERY_MODE PRE_TEST
            WORKING_DIRECTORY "${SR_RUNTIME_OUTPUT_DIR}"
            TEST_FILTER "-${ARG_DEVICE_FILTER}"
            TEST_LIST ${ARG_NAME}_TESTS)
        gtest_discover_tests(${ARG_NAME}
            DISCOVERY_MODE PRE_TEST
            WORKING_DIRECTORY "${SR_RUNTIME_OUTPUT_DIR}"
            TEST_FILTER "${ARG_DEVICE_FILTER}"
            TEST_LIST ${ARG_NAME}_DEVICE_TESTS)
    endif()

    # Read by ctest after the discovery files above; see the GoogleTest module's
    # note on TEST_INCLUDE_FILES and <target>_TESTS.
    set(properties_file "${CMAKE_CURRENT_BINARY_DIR}/${ARG_NAME}_properties.cmake")
    file(WRITE "${properties_file}"
        "if(${ARG_NAME}_TESTS)\n"
        "  set_tests_properties(\${${ARG_NAME}_TESTS} PROPERTIES\n"
        "    LABELS \"${labels}\" TIMEOUT ${ARG_TIMEOUT} RUN_SERIAL ${serial})\n"
        "endif()\n"
        "if(${ARG_NAME}_DEVICE_TESTS)\n"
        "  set_tests_properties(\${${ARG_NAME}_DEVICE_TESTS} PROPERTIES\n"
        "    LABELS \"${labels};${SR_TEST_DEVICE_LABEL}\" TIMEOUT ${ARG_TIMEOUT} RUN_SERIAL ${serial})\n"
        "endif()\n")
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${properties_file}")
endfunction()

# sushiruntime_add_module_tests(MODULE <module> [DEVICE_FILTER <filter>] SOURCES <file under tests/>...)
function(sushiruntime_add_module_tests)
    cmake_parse_arguments(ARG "" "MODULE;DEVICE_FILTER" "SOURCES" ${ARGN})
    set(test_sources "")
    foreach(source IN LISTS ARG_SOURCES)
        list(APPEND test_sources "tests/${source}")
    endforeach()
    sushiruntime_add_test_program(
        NAME "sr_${ARG_MODULE}_tests" KIND unit MODULE ${ARG_MODULE}
        DEVICE_FILTER "${ARG_DEVICE_FILTER}"
        SOURCES ${test_sources})
endfunction()
```

`RUN_SERIAL` on stress and performance tests records what TEST-10 notes: the benchmarks assume they own every core.

The same file holds `sushiruntime_add_package_tests()`: the two `Package.*` tests of `tests/CMakeLists.txt:281-312`, with `${CMAKE_BINARY_DIR}` replaced by `${PROJECT_BINARY_DIR}` at both sites, registered only when `SR_INSTALL` is on.

#### Programs

| Program | Declared in | Kind | Notes |
| --- | --- | --- | --- |
| `sr_<module>_tests` | `modules/<tier>/<module>/CMakeLists.txt` | unit, label `<module>` | One per module with tests |
| `sr_unit_tests` | `tests/CMakeLists.txt` | unit | Only unit tests that cross modules; absent if none remain |
| `sr_integration_tests` | `tests/CMakeLists.txt` | integration | Distributed sources appended under `SR_ENABLE_DISTRIBUTED` |
| `sr_regression_tests` | `tests/CMakeLists.txt` | regression | |
| `sr_allocation_tests` | `tests/allocation/CMakeLists.txt` | integration | Stays its own program: it replaces global `operator new`. The folder names the program and gets a README |
| `sr_stress_tests`, `sr_performance_tests` | `tests/benchmark/CMakeLists.txt` | stress, performance | Under `SR_BUILD_BENCHMARKS`; replace `sr_benchmark_tests` |
| header checks | by the module function | none (compile only) | A header that is not self-contained fails the build |

`sr_functional_tests` disappears; an executable cannot be aliased. The CLI default target becomes `sr_integration_tests` (section 8).

### 4. Install and package

#### Where install logic lives (LAYOUT-19)

| File | Owns |
| --- | --- |
| `cmake/SushiRuntimeInstall.cmake` | `install(TARGETS)`, headers, the forwarding root, `install(EXPORT)`, `export(EXPORT)`, the config and version files |
| `cmake/SushiRuntimeRedistributables.cmake` | The third-party DLL staging now at `SushiRuntimeInstall.cmake:23-165`. The four `SUSHIRUNTIME_*` cache lists become plain variables (LAYOUT-04) |
| `cmake/SushiRuntimePackaging.cmake` | CPack, top level only |
| `cmake/SushiRuntimeVersionCheck.cmake` | The `version.hpp` check from `CMakeLists.txt:75-84`, pointed at the header's new path |

The root includes `SushiRuntimeInstall.cmake` after `add_subdirectory(modules)`. `install(TARGETS)` for a target of another directory has been allowed since CMake 3.13.

#### `SushiRuntimeInstall.cmake` outline

1. `if(NOT SR_INSTALL) return()`.
2. `include(GNUInstallDirs)`; resolve `SR_INSTALL_CMAKEDIR` (empty means `${CMAKE_INSTALL_LIBDIR}/cmake/SushiRuntime`).
3. `install(TARGETS sushiruntime sushiruntime_deterministic_fp sushiruntime_sycl_flags sushiruntime_gpu_offload EXPORT SushiRuntimeTargets ...)` with `RUNTIME` and `LIBRARY` under component `sushiruntime_runtime`, and `ARCHIVE` and `INCLUDES` under `sushiruntime_development`. `EXPORT_NAME` is `sycl` and `gpu_offload` on the two flag targets.
4. For each root in `SUSHIRUNTIME_INSTALLED_INCLUDE_ROOTS`, and for `SR_FORWARDING_INCLUDE_ROOT`:
   `install(DIRECTORY "${root}/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" COMPONENT sushiruntime_development FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")`.
5. `install(EXPORT ...)`, `export(EXPORT ...)`, `configure_package_config_file`, `write_basic_package_version_file(... SameMinorVersion)`, `install(FILES ...)`.
6. `include(SushiRuntimeRedistributables.cmake)` and call its function.

LAYOUT-20 and DIST-23 need no exclusion pattern. An internal module's root is never in the installed list. The distributed module is added only when `SR_ENABLE_DISTRIBUTED` is on, so its root is never recorded in an OFF build. `source/` is never installed, and the 16 READMEs fail the pattern. The components let a parent install only the shared library.

#### Forwarding headers

The move map is one table, `cmake/SushiRuntimeMovedHeaders.cmake`, filled by the file-move designer:

```cmake
# Old include path, then new include path. Removed whole in v0.5.0.
set(SUSHIRUNTIME_MOVED_HEADERS
    SushiRuntime/core/logger.hpp        SushiRuntime/logging/logger.hpp
    SushiRuntime/graph/task_types.hpp   SushiRuntime/task/task_types.hpp)
```

The same file sets `SR_FORWARDING_INCLUDE_ROOT` to `${PROJECT_BINARY_DIR}/forwarding/include`. For each pair it runs `configure_file` on `cmake/SushiRuntimeForwardingHeader.hpp.in` (license block, `@file`, `@deprecated`, `#pragma once`, one `#include <@NEW@>`). The root is on the product's `BUILD_INTERFACE` and is installed. No module target carries it, so nothing inside `modules/` can include an old path.

Marking: the template emits `#pragma message` naming the new path only under `#if defined(SR_WARN_DEPRECATED_HEADERS)`. The build policy defines that macro for in-tree programs, so with `-Werror` a test or example that uses an old path fails on the clang lanes. It is off for consumers because clang reports `#pragma message` as a warning, and SushiBLAS builds 72 files that include `graph/task_types.hpp` under its own `-Werror`. On by default would break exactly the consumers the forwarders exist for. If the owner prefers default-on, the template flips one line.

#### SOVERSION (LAYOUT-07)

`MAJOR.MINOR` while the major is 0, `MAJOR` afterwards, as in `sushiruntime_compose_library` above. The Linux file becomes `libsushiruntime.so.0.3`. Every parent reaches the file through `$<TARGET_FILE:...>`, so none names it.

#### Hidden visibility (LAYOUT-08)

Build side:

- `CXX_VISIBILITY_PRESET hidden` and `VISIBILITY_INLINES_HIDDEN ON` in `sushiruntime_apply_build_policy`.
- `SR_BUILDING_LIBRARY` on every object library (already in the module function).
- `WINDOWS_EXPORT_ALL_SYMBOLS` is not carried into `sushiruntime_compose_library`.

Header side, in `core/export.hpp`: define `SR_API` in the form `platform-portability` gives, keyed on `SR_BUILDING_LIBRARY`, and keep `#define SUSHIRUNTIME_API SR_API` as the deprecated spelling (41 uses in 28 files today).

What it implies:

1. Every non-inline function, class with out-of-line members, and static data member that a consumer translation unit references needs `SR_API`. Consumer code includes the header-inline API in `api/`, which calls into the library.
2. Every type thrown by the library and caught outside it, and every polymorphic type used with `dynamic_cast` across the boundary, needs the macro on the class so its typeinfo is exported on Linux.
3. With hidden inlines, a function-local static in an inline function exists once per image. Windows behaves this way today; Linux does not. `memory` is header-only and holds the epoch reclamation and USM registries, and `logger.hpp` is a header. Each header singleton must be reached through an exported function, or be proven per-image safe.

How to find the missing annotations:

1. Link errors: build the tests and examples; each unresolved symbol is a missing `SR_API`.
2. Before and after lists: `nm -D --defined-only libsushiruntime.so | c++filt` on Linux and `dumpbin /exports sushiruntime.dll` on Windows, diffed against `nm -u` of each test program.
3. Singletons: search `modules/*/*/include` for function-local `static`, `inline` variables and `thread_local`.
4. Exceptions: `throw` under `source/` against `catch` in tests and in the three consumers.
5. Build SushiBLAS, SushiAI and SushiEngine against the result. SushiBLAS calls `live_usm_count`, `get_queue`, `wait_all`, `device_count`, `free_usm` and the three `aligned_malloc_*_on` on `RuntimeContext` (`PLAN.md`, constraints table).

I did not count the missing annotations.

#### Exported SYCL facts (LAYOUT-02)

`SushiRuntimeConfig.cmake.in` afterwards:

```cmake
@PACKAGE_INIT@
include(CMakeFindDependencyMacro)
find_dependency(Threads)

if(NOT TARGET IntelSYCL::SYCL_CXX)
    find_package(IntelSYCL QUIET)
endif()
if(NOT TARGET IntelSYCL::SYCL_CXX)
    add_library(IntelSYCL::SYCL_CXX INTERFACE IMPORTED)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/SushiRuntimeTargets.cmake")

set(SushiRuntime_DISTRIBUTED      "@SR_ENABLE_DISTRIBUTED@")
set(SushiRuntime_DETERMINISTIC_FP "@SR_DETERMINISTIC_FP@")
set(SushiRuntime_SYCL_TOOLCHAIN   "@SR_SYCL_TOOLCHAIN@")
set(SushiRuntime_GPU_BACKEND      "@SR_GPU_BACKEND_RESOLVED@")
set(SushiRuntime_SYCL_FLAGS       "@SR_EXPORTED_SYCL_FLAGS@")
set(SushiRuntime_ACPP_TARGETS     "@ACPP_TARGETS@")

check_required_components(SushiRuntime)
```

The package gives four targets:

- `SushiRuntime::SushiRuntime`: the narrow contract, unchanged.
- `SushiRuntime::sushiruntime_deterministic_fp`: unchanged.
- `SushiRuntime::sycl`: opt-in, `-fsycl` on intel-llvm.
- `SushiRuntime::gpu_offload`: opt-in, the device targets the package was built for, without toolkit paths.

The two SYCL targets are separate because a consumer on a machine without the CUDA toolkit can still build host and JIT code against a CUDA-built package.

The package must find Threads and, on oneapi, IntelSYCL. hwloc stays out of the install interface. The oracle drops `-DCMAKE_CXX_FLAGS=-fsycl` (`verify_installed_package.py:112`) and `tests/package/CMakeLists.txt:89-91` links `SushiRuntime::sycl`, so the packaging test proves the flag contract.

API-07 and API-09 are header work. The build already gives them what they need: `SR_DISTRIBUTED` PUBLIC on the product and `SushiRuntime_DISTRIBUTED` in the config.

### 5. Embedding

#### Guards

- `SR_IS_TOP_LEVEL` is computed in the options file as `CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR`. It is the value `PROJECT_IS_TOP_LEVEL` takes, and it is needed before `project()`, where that variable does not exist yet.
- `SR_BUILD_EXAMPLES` and `SR_INSTALL` default to `${SR_IS_TOP_LEVEL}`.
- CPack is included only when `SR_IS_TOP_LEVEL AND SR_INSTALL`.
- `CMAKE_BINARY_DIR` becomes `PROJECT_BINARY_DIR` at `tests/CMakeLists.txt:283,286` and `CMakeLists.txt:132`.
- `SushiRuntimeVcpkg.cmake` and the compiler defaulting and oneAPI probe fix-ups of the toolchain file run only at top level. In a parent, `project()` has already run and they do nothing. The toolchain inference from the compiler name still runs, because parents read `SR_SYCL_TOOLCHAIN`.

#### File names (LAYOUT-22)

The root stops appending to `CMAKE_MODULE_PATH`. Every include is an absolute path through `SR_CMAKE_DIR`, and no file under `cmake/` is found by module-mode `find_package`. A parent's `Dependencies.cmake` then cannot shadow ours, and ours cannot shadow a parent's. Every file directly under `cmake/` is still named `SushiRuntime<Thing>.cmake`, so the folder follows one rule. Files under `cmake/gpu/` keep their vendor names.

#### What a parent gets

After `add_subdirectory(sushiruntime)`:

| Kind | Names |
| --- | --- |
| Targets | `sushiruntime`, `SushiRuntime::SushiRuntime`, `SushiRuntime::sycl`, `sushiruntime_gpu_offload` / `SushiRuntime::gpu_offload`, `sushiruntime_deterministic_fp` |
| Cache | `SR_SYCL_TOOLCHAIN`, `SR_GPU_BACKEND_RESOLVED`, `SR_CUDA_ARCH`, `SR_AMD_ARCH` |
| Commands | `add_sycl_to_target` (a no-op shim on intel-llvm), `sushiruntime_apply_sycl(<target> [SOURCES ...])` |
| Not by default | examples, tests, install rules, CPack |

For SYCL flags a parent does one of two things per target that compiles kernels. It calls `sushiruntime_apply_sycl(<target> SOURCES ...)`, which applies the flags PRIVATE. Or, when its own consumers must inherit them, it links `SushiRuntime::sycl` and `SushiRuntime::gpu_offload` PUBLIC and calls `add_sycl_to_target`. The runtime's `--gcc-toolchain` does not reach a parent's targets, as today.

#### Against SushiBLAS today

`D:\Projects\sushiblas\cmake\Runtime.cmake` calls `add_subdirectory("${SUSHIRUNTIME_DIR}" "${CMAKE_BINARY_DIR}/sushiruntime")`.

What SushiBLAS reads and whether it survives:

| SushiBLAS use | Site | After |
| --- | --- | --- |
| `SushiRuntime::SushiRuntime` | `SushiBLASModule.cmake:123`, library module `:80` | Kept |
| `sushiruntime_gpu_offload` INTERFACE options | `SyclTarget.cmake:55-86` | Kept, same target name |
| `SR_SYCL_TOOLCHAIN` | `SyclTarget.cmake:112` | Kept |
| `add_sycl_to_target` | `SyclTarget.cmake:106-110` | Kept |
| `$<TARGET_FILE:SushiRuntime::SushiRuntime>` | `SushiBLASTests.cmake:61` | Kept; independent of the output folder |
| Include paths | 72 files on `graph/task_types.hpp`, others | Kept through the forwarding root on `BUILD_INTERFACE` |

What SushiBLAS must change:

1. **Required: opt back in to install rules.** Add `set(SR_INSTALL ON)` before `add_subdirectory` in `cmake/Runtime.cmake`. Without it, `install(EXPORT SushiBLASTargets)` (`modules/library/sushiblas/CMakeLists.txt:120`) names a target that requires `sushiruntime`, which is then in no export set. Its `tests/package` also relies on the runtime being installed into the same prefix. The line is harmless against today's runtime, so it can land first.
2. Nothing else is required. Its builds stop compiling the runtime's six examples, which it gets today.
3. Stale comments only: `.github/workflows/ci.yml:161` (the RelWithDebInfo auto-gate, gone with `SR_SANITIZER`), `cmake/SushiBLASBuildPolicy.cmake:15-26` (shadowing through `CMAKE_MODULE_PATH`), and the references to `src/CMakeLists.txt`.

The other two parents:

- **SushiAI** (`cmake/Runtime.cmake:56`): the same one-line change, for the same reason (`SushiAIConfig.cmake.in` and its export name the runtime).
- **SushiEngine** (`cmake/Runtime.cmake:75-79`): on Linux it relies on the runtime's install rules to place the shared library (its own comment, lines 65-71), so it sets `SR_INSTALL ON` there. On Windows `EXCLUDE_FROM_ALL` becomes redundant. It links the lowercase `sushiruntime`, which stays.

A second consumer, `tests/embed/`, closes the gap LAYOUT-03 names: a small project that calls `add_subdirectory` on the source tree and links `SushiRuntime::SushiRuntime` and `SushiRuntime::sycl`, registered under label `package` and driven by a sibling of the oracle.

### 6. Options

#### `cmake/SushiRuntimeOptions.cmake` (full)

```cmake
# Every option a person or the CLI sets. The logic that reads each one lives in the
# file named beside it. Included before project().

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(SR_IS_TOP_LEVEL ON)
else()
    set(SR_IS_TOP_LEVEL OFF)
endif()

# Toolchain (SushiRuntimeToolchain.cmake, SushiRuntimeSycl.cmake).
# SR_SYCL_TOOLCHAIN is declared in SushiRuntimeToolchain.cmake: its default is planted
# only after that file has asked whether the caller stated one.
if(NOT CMAKE_HOST_WIN32)
    set(SR_GCC_TOOLCHAIN "/usr" CACHE STRING
        "GCC install prefix passed to clang as --gcc-toolchain; empty to leave discovery to clang")
endif()

# GPU backend (cmake/gpu/).
set(SR_GPU_BACKEND "auto" CACHE STRING
    "GPU backends: auto, a list of cuda/rocm/intel, or cpu / none")
set_property(CACHE SR_GPU_BACKEND PROPERTY STRINGS auto cuda rocm intel cpu none)
set(SR_CUDA_ARCH "" CACHE STRING
    "CUDA compute capabilities, digits only (86 or 61;86); empty: read from nvidia-smi")
set(SR_AMD_ARCH "" CACHE STRING
    "AMD GPU ISAs (gfx1030 or gfx1030;gfx1100); empty: read from rocm_agent_enumerator")
set(SR_INTEL_GPU_DEVICE "" CACHE STRING
    "Intel GPU device for ahead-of-time spir64_gen (pvc, dg2); empty: JIT spir64")
option(SR_ENABLE_CUDA "Deprecated: use SR_GPU_BACKEND=cuda" OFF)

# What is built.
option(SR_ENABLE_DISTRIBUTED "Build the distributed module" OFF)
option(SR_BUILD_TESTS "Build the test programs and the header checks" OFF)
option(SR_BUILD_BENCHMARKS "Build the stress and performance programs; read only with SR_BUILD_TESTS" ON)
option(SR_BUILD_EXAMPLES "Build applications/examples" ${SR_IS_TOP_LEVEL})

# How it is compiled (SushiRuntimeDeterminism, Optimization, Sanitizers).
option(SR_DETERMINISTIC_FP "Compile under the reproducible floating-point contract" ON)
option(SR_NATIVE_ARCH "Target the build machine's ISA; never for a binary that leaves it" OFF)
set(SR_SANITIZER "none" CACHE STRING "Sanitizer for every in-tree target: none, thread, address or undefined")
set_property(CACHE SR_SANITIZER PROPERTY STRINGS none thread address undefined)

# Install (SushiRuntimeInstall.cmake).
option(SR_INSTALL "Add the install rules and the package files" ${SR_IS_TOP_LEVEL})
set(SR_INSTALL_CMAKEDIR "" CACHE STRING
    "Where the CMake package files install; empty: <libdir>/cmake/SushiRuntime")
```

Notes:

- No option is renamed. Three are added: `SR_BUILD_EXAMPLES`, `SR_INSTALL`, `SR_SANITIZER`.
- `SR_ENABLE_CUDA` keeps working and `DetectGpu.cmake` prints a `DEPRECATION` message when it is on.
- `SR_SYCL_TOOLCHAIN` is the one option declared elsewhere, because `Sycl.cmake:29-33` must test `DEFINED CACHE{SR_SYCL_TOOLCHAIN}` before any default exists. The options file names it in a comment.
- `SR_CUDA_ARCH` and `SR_AMD_ARCH` get an empty declaration. `Cuda.cmake:34` and `Rocm.cmake:22` already treat empty as "probe", and the probe still runs only when the vendor is selected.
- `VCPKG_ROOT`, `SR_NVCC` and `SR_HIPCC` are discovery results, not options, and stay in their files.
- `docs/reference/BUILD_OPTIONS.md` becomes the one reference table (LAYOUT-04); that is documentation work.

#### `cmake/SushiRuntimeSanitizers.cmake` (full, LAYOUT-05)

```cmake
add_library(sushiruntime_sanitizers INTERFACE)

set(SR_SANITIZER_VALUES none thread address undefined)
if(NOT SR_SANITIZER IN_LIST SR_SANITIZER_VALUES)
    message(FATAL_ERROR "SR_SANITIZER='${SR_SANITIZER}' is not one of ${SR_SANITIZER_VALUES}.")
endif()
if(SR_SANITIZER STREQUAL "none")
    return()
endif()

if(WIN32 AND NOT SR_SANITIZER STREQUAL "address")
    message(FATAL_ERROR "SR_SANITIZER=${SR_SANITIZER} is not available on Windows; only 'address' is.")
endif()
if(MSVC)
    message(FATAL_ERROR "SR_SANITIZER needs a clang-based compiler.")
endif()

if(SR_SANITIZER STREQUAL "address" AND WIN32)
    # -Xarch_host keeps the device pass uninstrumented; see cli/README.md.
    target_compile_options(sushiruntime_sanitizers INTERFACE
        -Xarch_host -fsanitize=address -fsanitize-address-use-after-return=never
        -fno-omit-frame-pointer -g)
    target_compile_definitions(sushiruntime_sanitizers INTERFACE _DISABLE_STL_ANNOTATION=1)
    target_link_options(sushiruntime_sanitizers INTERFACE -fsanitize=address)
else()
    target_compile_options(sushiruntime_sanitizers INTERFACE
        -fsanitize=${SR_SANITIZER} -fno-omit-frame-pointer -g)
    target_link_options(sushiruntime_sanitizers INTERFACE -fsanitize=${SR_SANITIZER})
endif()
message(STATUS "SR_SANITIZER=${SR_SANITIZER}")
```

The flags are the ones in `project.py:33-42`, without `-O1`. Today `-O1` arrives in `CMAKE_CXX_FLAGS`, which CMake places before `CMAKE_CXX_FLAGS_RELEASE`, so `-O3` wins. As a target option it would come last and change the ASan build from `-O3` to `-O1`. Leaving it out keeps today's behaviour.

A Debug build on Linux is no longer a TSan build. That needs a changelog line.

#### Presets (LAYOUT-06)

| | A. Presets are the statement | B. No presets |
| --- | --- | --- |
| Shape | Hidden toolchain presets (3) crossed with lanes (`dev`, `dev-distributed`, `tsan`, `asan`, `ci-fast`, `ci-nightly`): 18 concrete presets, schema 3, `binaryDir` `build/${presetName}` | `CMakePresets.json` deleted; the README gives `sr build` and one raw `cmake` line |
| Cost in `cli/` | Rewrite `_configure_args_linux` and `_configure_args_windows` (`project.py:262-385`) into "select preset, add machine paths"; `_build_dir`, `clean`, `packaging.py`'s `build/dist`, `docs`'s `build/docs` and `discovery` follow the new binary folders; the configure-argument tests are rewritten. Test presets cannot take a module or a filter, so the CLI still builds its own `-L` | None beyond what `SR_SANITIZER` already requires |
| Cost in CI | Workflow paths (`build`, `build-tsan`, artefact folders) change | None |
| What still lives twice | Machine paths (compiler full path, vcpkg, ninja, pkgconf, rc) stay in the CLI, since a preset cannot hold them portably; label regexes sit in both test presets and `project.py` | Nothing |
| Gain | An IDE or a user without `sr` can pick a lane | One description of each lane, in the CLI |

I recommend B. `AGENTS.md` sends every build through `sr`, and `continuous-integration` sends CI through it too, so presets would be a description nothing runs. That is how the current three went stale without anyone noticing (LAYOUT-06's evidence). An IDE cannot use a preset here anyway without the environment `load_build_env` builds (setvars, vcpkg, the compiler's full path on Windows). With `SR_SANITIZER` each lane is a short `-D` list, and the CLI can hold the lanes as one table. The ruling is the owner's.

### 7. The root `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.22)

set(SR_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}/cmake")

include("${SR_CMAKE_DIR}/SushiRuntimeOptions.cmake")
include("${SR_CMAKE_DIR}/SushiRuntimeVcpkg.cmake")
include("${SR_CMAKE_DIR}/SushiRuntimeToolchain.cmake")

project(SushiRuntime VERSION 0.3.0 LANGUAGES CXX)

include("${SR_CMAKE_DIR}/SushiRuntimeVersionCheck.cmake")
include("${SR_CMAKE_DIR}/SushiRuntimeDependencies.cmake")
include("${SR_CMAKE_DIR}/SushiRuntimeSycl.cmake")
include("${SR_CMAKE_DIR}/SushiRuntimeGpu.cmake")
include("${SR_CMAKE_DIR}/SushiRuntimeBuildPolicy.cmake")
include("${SR_CMAKE_DIR}/SushiRuntimeMovedHeaders.cmake")
include("${SR_CMAKE_DIR}/SushiRuntimeModule.cmake")
include("${SR_CMAKE_DIR}/SushiRuntimeLibrary.cmake")
include("${SR_CMAKE_DIR}/SushiRuntimeProgram.cmake")
include("${SR_CMAKE_DIR}/SushiRuntimeTests.cmake")

add_subdirectory(modules)
if(SR_BUILD_EXAMPLES)
    add_subdirectory(applications/examples)
endif()
if(SR_BUILD_TESTS)
    add_subdirectory(tests)
endif()

include("${SR_CMAKE_DIR}/SushiRuntimeInstall.cmake")
include("${SR_CMAKE_DIR}/SushiRuntimePackaging.cmake")
```

It holds the floor, `project()`, includes and `add_subdirectory`. The 17-line comment on include order (`CMakeLists.txt:11-27`) moves to `cmake/README.md`. The version stays 0.3.0 until the release is cut.

#### CMake floor (LAYOUT-28)

3.22, stated once in the root, plus the two standalone projects `tests/package/` and `tests/embed/`. `cmake_minimum_required` comes out of `src/`, `tests/` and `examples/`. 3.22 is what `gtest_discover_tests(TEST_FILTER)` needs (`tests/CMakeLists.txt:7`), and it covers `PROJECT_IS_TOP_LEVEL` (3.21) and `cmake_language(CALL)` (3.18). The 3.30 path in `Determinism.cmake:52` and the 3.24 registry query in `CudaToolkit.cmake:25` stay conditional.

#### `cmake/` after the change

| File | One line |
| --- | --- |
| `README.md` | The include order and what must run before `project()` (LAYOUT-21) |
| `SushiRuntimeOptions.cmake` | Every user-facing option and `SR_IS_TOP_LEVEL` |
| `SushiRuntimeVcpkg.cmake` | vcpkg root discovery at top level; `sushiruntime_vcpkg_installed_dir()` for LAYOUT-23 |
| `SushiRuntimeToolchain.cmake` | Before `project()`: `SR_SYCL_TOOLCHAIN` and its inference, the default compiler, the oneAPI Windows probe fix-ups (phase 1 of `Sycl.cmake`) |
| `SushiRuntimeSycl.cmake` | After `project()`: `find_package` dispatch, the `add_sycl_to_target` shim, `sushiruntime_sycl_flags`, `sushiruntime_toolchain`, `sushiruntime_apply_sycl()` |
| `SushiRuntimeGpu.cmake` | Includes `gpu/GpuBackend.cmake` and `gpu/DetectGpu.cmake`, then calls `sushiruntime_gpu_finalize()` |
| `gpu/GpuBackend.cmake` | Accumulators and primitives, now with a local-option accumulator; finalize renders onto `sushiruntime_gpu_offload` only |
| `gpu/DetectGpu.cmake` | Resolves the vendor set; includes `${Vendor}.cmake` by rule (LAYOUT-18) |
| `gpu/Cuda.cmake`, `gpu/Rocm.cmake`, `gpu/Intel.cmake`, `gpu/CpuFallback.cmake` | One strategy each; `IntelGpu.cmake` renamed |
| `gpu/locate/{Cuda,Rocm,Intel}Toolkit.cmake` | Unchanged |
| `SushiRuntimeDependencies.cmake` | hwloc through pkg-config, Threads |
| `SushiRuntimeVersionCheck.cmake` | `version.hpp` against `project()` |
| `SushiRuntimeBuildPolicy.cmake` | Composes the policy targets; `sushiruntime_apply_build_policy()` |
| `SushiRuntimeCompilerWarnings.cmake` | `sushiruntime_warnings` |
| `SushiRuntimeOptimization.cmake` | `sushiruntime_perf` |
| `SushiRuntimeDeterminism.cmake` | `sushiruntime_deterministic_fp` |
| `SushiRuntimeSanitizers.cmake` | `sushiruntime_sanitizers` from `SR_SANITIZER` |
| `SushiRuntimeTiers.cmake` | The tier order, module tiers, same-tier edges, optional modules |
| `SushiRuntimeModule.cmake` | `sushiruntime_add_module()`, the edge check, the header check |
| `SushiRuntimeLibrary.cmake` | `sushiruntime_compose_library()` |
| `SushiRuntimeProgram.cmake` | `sushiruntime_add_program()` and the one output folder |
| `SushiRuntimeTests.cmake` | GTest setup, `sushiruntime_add_test_program()`, `sushiruntime_add_module_tests()`, `sushiruntime_add_package_tests()` |
| `SushiRuntimeMovedHeaders.cmake` | The move table; generates the forwarding root |
| `SushiRuntimeForwardingHeader.hpp.in` | The forwarding header template |
| `SushiRuntimeInstall.cmake` | Install rules, export, package files |
| `SushiRuntimeRedistributables.cmake` | Third-party runtime libraries of the installed package |
| `SushiRuntimePackaging.cmake` | CPack, top level only |
| `SushiRuntimeConfig.cmake.in` | The package config |

`SushiRuntimeProjectOptions.cmake` is deleted. Its two options go to the options file, its language settings go to the build policy as target properties, and its CMP0135 block goes away (the 3.22 floor does not set that policy, and nothing in the tree downloads an archive).

### 8. What the CLI and CI must change

#### `cli/sushiruntime/services/project.py`

| Site | Change |
| --- | --- |
| `:33-45`, `:146-162` | `ASAN_FLAGS*` and `_flags_fingerprint` go; the flags live in `SushiRuntimeSanitizers.cmake`. The stamp keys on the sanitizer value instead |
| `:49-53`, `:89-94`, `:110-111` | `BuildType` gains `tsan` (RelWithDebInfo, `SR_SANITIZER=thread`, tree `build-tsan`); `asan` passes `SR_SANITIZER=address` |
| `:97-107`, `:414-415` | `thread_sanitizer_applies` is deleted; Debug no longer implies TSan |
| `:276-283`, `:375-385` | The five `CMAKE_*_FLAGS` lines for ASan and the `-DCMAKE_CXX_FLAGS=-fsycl` line go; `-DSR_SANITIZER=...` replaces them. `SR_BUILD_BENCHMARKS=OFF` under ASan stays |
| `:441`, `:449` | Prior L16: add `-S <root>` to the configure arguments and run configure, compile and ctest with `cwd=build_dir`, created first. Configure runs with the checkout as its working directory today, which is where the `CXX-DetectStdlib-*.h` files land. The ignore rule at `.gitignore:46` can go afterwards |
| `:63-86` | `Suite` gains `device` (`^needs_device$`); the comment at `:64` is rewritten since labels no longer come from name prefixes. New `--module NAME` on `sr test` passes `-L ^NAME$` and excludes `--suite`, so `sushicore.cmake_driver.ctest_run` (one `label_regex`) needs no change |
| `:538` | `root / ".config" / "doxygen" / "Doxyfile"`; `cwd=root` stays, since `sushicore` requires the file under `cwd` (`cmake_driver.py:178-193`) |

#### Other CLI files

- `cli/sushiruntime/config.py:30,103`, `cli/config.toml:14,29`, `cli/sushiruntime/cli.py:147-157`: the default target and the help examples name `sr_functional_tests` and `sr_benchmark_tests`. They become `sr_integration_tests` and `sr_performance_tests`. `.github/ISSUE_TEMPLATE/bug_report.yml:25` too.
- `cli/sushiruntime/services/discovery.py`: confirm it finds programs under `build/bin/`. I grepped it and did not read it.
- `cli/sushiruntime/services/docker.py:21-30`: `docker build -f .config/docker/Dockerfile -t sushiruntime .`; the context stays the root. `.dockerignore` moves beside it as `Dockerfile.dockerignore`, which BuildKit reads. I did not verify that on this Docker.
- `cli/tests/test_cli.py:40`: `K_DOXYFILE` path.
- `.config/doxygen/Doxyfile`: `INPUT` (`Doxyfile:33`) becomes `modules/surface/api/include/SushiRuntime/api`.
- `tools/packaging/verify_installed_package.py:112`: drop `-DCMAKE_CXX_FLAGS=-fsycl`.

#### `.github/workflows/ci.yml`

- `:56-62`: the TSan lane must pass `-DSR_SANITIZER=thread`. Without it the job stays green and stops sanitising. This lands in the same commit as the option.
- `:31`, `:59`, `:149`: `-DCMAKE_CXX_FLAGS=-fsycl` becomes redundant once flags are on targets. Remove it after a oneAPI build confirms.
- `:114`: `doxygen Doxyfile` becomes `sr docs`.
- `:36`, `:71`, `:155`: the label regexes still match, since the kind labels keep their names.
- `:169-178`: add nothing; `tools/tests` already runs and picks up `test_tier_table.py`.
- Moving the build jobs from raw `cmake` and `ctest` to `sr` is phase F (OPS).

`release.yml:36-37` reads the version from the root `CMakeLists.txt`, which still holds it.

### 9. Order of work

Each step leaves the tree building and the tests passing. Steps 1 to 7 need no file move.

| Step | Work | Before moves |
| --- | --- | --- |
| 0 | In SushiBLAS, SushiAI and SushiEngine (Linux branch): `set(SR_INSTALL ON)` before `add_subdirectory`. Harmless against today's runtime | yes |
| 1 | Floor 3.22 in the root only. Rename `cmake/` files to the prefix, include by absolute path, stop appending `CMAKE_MODULE_PATH`. Split `Sycl.cmake` into Toolchain and Sycl. Add the options file. No behaviour change | yes |
| 2 | `SR_SANITIZER`, with the CLI lanes and the CI TSan line in the same commit | yes |
| 3 | SYCL flags onto targets: `sushiruntime_sycl_flags`, `sushiruntime_toolchain`, `sushiruntime_apply_sycl`, the local-option accumulator; delete the directory-scope options and the hand list. Then drop `-fsycl` from the CLI, the oracle and CI | yes |
| 4 | Install logic into `cmake/` (Install, Redistributables, Packaging, VersionCheck); `FILES_MATCHING`; components; SOVERSION; Threads; the exported SYCL facts; `SR_INSTALL`, `SR_BUILD_EXAMPLES`, the CPack guard, `PROJECT_BINARY_DIR` | yes |
| 5 | `SushiRuntimeProgram.cmake` and `SushiRuntimeTests.cmake` on the current tree: three programs from `tests/functional/{unit,integration,regression}`, two benchmark programs, labels through the properties script, `DEVICE_FILTER`, the shared output folder. CLI target names, `--module`, `--suite device` | yes |
| 6 | `SushiRuntimeTiers.cmake`, `SushiRuntimeModule.cmake`, `SushiRuntimeLibrary.cmake`. `src/CMakeLists.txt` becomes one legacy OBJECT library and the product is composed by `sushiruntime_compose_library()`. Checker `K_TIER_ORDER`, `K_SAME_TIER_EDGES`, tests 1 and 2 of `test_tier_table.py` | yes |
| 7 | `SushiRuntimeMovedHeaders.cmake` with an empty table and the template | yes |
| 8 | Modules move, lowest tier first, one module per commit: its folder, its `CMakeLists.txt`, one table line, its tests into `sr_<module>_tests`, a forwarder line per header whose path changes. The legacy object library shrinks by the same sources and keeps every include root until it is empty. A cycle is broken before either of its modules moves. The header check starts for each module as it lands | no |
| 9 | Delete `src/`, `include/` and the legacy library. Add `rule_same_tier_includes`, `rule_declared_dependencies` and test 3 | no |
| 10 | Hidden visibility: `SR_API`, the properties in the build policy, the annotation pass, and a build of the three consumers | after 9 |
| 11 | `examples/` to `applications/examples/`, `Doxyfile` and `Dockerfile` to `.config/`, with the CLI paths | any time |
| 12 | `tests/embed/`; presets deleted or rewritten per the ruling; `cmake/README.md` | any time |

Step 6 is what makes step 8 incremental: the product is composed from recorded object libraries, so a module leaving the legacy library and arriving as its own changes two lists and nothing else.

### Risks: what only a configure or a build can confirm

1. **`add_sycl_to_target` on an OBJECT library** under oneapi and adaptivecpp, and whether the link options reach a shared library that only consumes `TARGET_OBJECTS`. SushiBLAS does this for a static library; the shared case is unproven. `sushiruntime_apply_sycl(sushiruntime)` in the compose function is the intended cover.
2. **Removing directory-scope `-fsycl`.** A module that reaches `<sycl/sycl.hpp>` without depending on `sycl` fails to compile on intel-llvm. That is the intended outcome, but the first build will list how many there are.
3. **The properties script.** That `<target>_TESTS` is visible in a later `TEST_INCLUDE_FILES` entry in PRE_TEST mode follows from the module's documentation and from `GoogleTest.cmake:711-779`. I did not run it. The fallback is the composite label of section 3.
4. **One output folder and vcpkg applocal.** Up to twenty programs link into `build/bin`, and each one's applocal pass copies `hwloc-15.dll` there. The existing comment describes a sharing violation between that pass and a DLL copy. The copy is gone, but applocal against applocal on a clean parallel build is untested.
5. **Windows ASan link line.** The flags reach the link today through `CMAKE_CXX_FLAGS`. The target passes `-fsanitize=address` at link and the full set at compile. Whether the intel/llvm driver needs `-Xarch_host` at link is unknown.
6. **`SR_INSTALL=OFF` in a parent.** The generate-time failure in SushiBLAS and SushiAI is my reading of CMake's export rule; step 0 removes the question either way.
7. **Hidden visibility.** The number of missing annotations, and which header singletons change behaviour on Linux, are unknown until it is built.
8. **Header checks cost.** One translation unit per public header, about 110, most of them reaching SYCL. The added build time is unmeasured.
9. **`install(TARGETS)` from the root for a target created in `modules/`**, and exporting `sushiruntime_sycl_flags` when its in-tree alias is `SushiRuntime::sycl`.
10. **An installed package on oneapi or adaptivecpp**: what `SushiRuntime::sycl` must carry there. The existing packaging test covers intel-llvm only.
11. **`Dockerfile.dockerignore`** beside a Dockerfile under `.config/docker/`.
12. **SushiEngine**: I read its `Runtime.cmake` and grepped the rest, so an export or install rule that names the runtime elsewhere may exist.

### Not read

- `docs/CONTRIBUTING.md` and the `.md` files under `docs/`, which `AGENTS.md` asks for first. I read `PLAN.md` and, in `REPORT.md`, the LAYOUT section in full plus OPS-19, TEST-09 to TEST-12, API-07, API-09, DIST-23 and EXEC-13. EXEC-14 only by its heading.
- `cli/sushiruntime/cli.py`, `config.py`, `env.py`, `services/discovery.py`, `stage_tree.py`: grepped, not read. `sushicore/cmake_driver.py`: signatures only.
- `Dockerfile`, `Doxyfile` (three grep hits), `.dockerignore`, `.github/actions/setup-oneapi`, `tests/package/main.cpp`, `tests/common/test_helpers.hpp` beyond the device probe.
- The C++ headers, except `core/export.hpp` and the include list of `SushiRuntime.h`. Which modules reach SYCL, and every `DEPENDS` list, belong to the other two designers.
- In SushiBLAS: the kernel and operation module files, `tests/benchmark`, `tests/package`, the CLI. In SushiEngine and SushiAI: everything except `cmake/Runtime.cmake` and grep hits.
- IntelSYCL's and AdaptiveCpp's own `add_sycl_to_target` implementations.

### Evidence for the delegation block

- **Item 1, SOLID shape.** Each `cmake/` file in section 7 has one responsibility and one line saying so. Three functions build every target: `sushiruntime_add_module`, `sushiruntime_add_program` and `sushiruntime_add_test_program`, and the test function is built on the program function. The tier order is stated once and `modules/CMakeLists.txt` derives the folder order from it. The product reads recorded lists and links no module target, so a module is added or removed with its folder and one table line. Two existing duplications are removed: the DLL staging, and the SYCL source list beside the source glob.
- **Item 2, humanizer.** The `humanizer` skill was invoked and `generic_english.md` read before drafting. The report was then checked against its list: claims carry a file and line or a named unknown, uncertainty is collected under Risks, and there is no closing summary.
- **Item 6, syntax check.** No source written; nothing to syntax-check. The CMake and Python in this report are proposals and have not been parsed by any tool.
- **Item 7, scratchpad.** Nothing was written. `du -sh` on the session scratchpad printed `4.0K`, an empty directory; the `b3` subfolder was never created. Nothing to delete.
- **Items 3 and 4.** Commands run were `ls`, `cat`, `sed`, `grep`, `git ls-files`, `cmake --version` and one failed `python -c` import probe. No configure, build or test started, and no file was created, touched or deleted in any repository.
- **Skills loaded:** `humanizer`, `repository-layout`, `project-tools`, `dependencies`, `platform-portability`, `versioning-and-release`, `continuous-integration`, `testing`.
