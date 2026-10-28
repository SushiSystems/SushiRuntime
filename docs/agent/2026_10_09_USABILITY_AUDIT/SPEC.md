# Phase B: many-module layout

**Status:** Open — the layout steps of part 2, RW-58.

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
| the estate audit report in the version history | `=` | stays | |
| `docs/agent/2026_10_09_USABILITY_AUDIT/PLAN.md` | `=` | stays | |
| `docs/agent/2026_10_09_USABILITY_AUDIT/REPORT.md` | `=` | stays | |
| `docs/architecture/ARCHITECTURE.md` | `=` | stays | |
| the help screen task 26 report in the version history | `=` | stays | |
| the wave 1 task 7a report in the version history | `=` | stays | |
| the v0.2.0 document in the version history | `=` | stays | |
| the v0.3.0 long form document in the version history | `=` | stays | |
| the backbone wave status document in the version history | `=` | stays | |
| the code and architecture review document in the version history | `=` | stays | |
| the physics substrate implementation document in the version history | `=` | stays | |
| the todo document in the version history | `=` | stays | |
| the usm allocation seam design spec in the version history | `=` | stays | |
| the g1 node event records design spec in the version history | `=` | stays | |
| the g1 node event records plan spec in the version history | `=` | stays | |
| the scan and counting sort design spec in the version history | `=` | stays | |
| the scan and counting sort plan spec in the version history | `=` | stays | |
| `docs/design/ENGINE_BACKBONE_REFACTOR/README.md` | `=` | stays | |
| `docs/design/GRAPH_REPLAY/README.md` | `=` | stays | |
| `docs/design/LAYERED_EXECUTION/PHYSICS_SUBSTRATE_REQUIREMENTS.md` | `=` | stays | |
| `docs/design/README.md` | `=` | stays | |
| `docs/backlog/README.md` | `=` | stays | |
| `docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN/README.md` | `=` | stays | |
| `docs/getting_started/INTRODUCTION.md` | `=` | stays | |
| `docs/guides/CLI_GUIDE.md` | `=` | stays | |
| `docs/guides/DISTRIBUTED_GUIDE.md` | `=` | stays | |
| `docs/guides/FAQ.md` | `=` | stays | |
| `docs/guides/INTEGRATION.md` | `=` | stays | |
| `docs/publish.toml` | `=` | stays | |
| `docs/reference/API_MAINPAGE.md` | `=` | stays | |
| `docs/reference/CHANGELOG.md` | `=` | stays | |
| `docs/reference/GLOSSARY.md` | `=` | stays | |
| `docs/backlog/README.md` | `=` | stays | |

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
| prose | `docs/CONTRIBUTING.md:274`, `docs/guides/CLI_GUIDE.md:252`, `docs/DOCUMENTATION_STYLE_GUIDE.md:31`, `docs/backlog/README.md:26,29`, `docs/backlog/README.md:102,135`, `README.md:552` |

`Dockerfile` to `.config/docker/Dockerfile`:

| Site | What it does |
| --- | --- |
| `cli/sushiruntime/services/docker.py:23-30` | `["docker", "build", "-t", IMAGE, ..., "."]` with no `-f`; it needs `-f .config/docker/Dockerfile` and keeps `.` as context |
| `cli/tests/test_docker.py:34,42` | asserts that exact argv |
| `Dockerfile:108` | `COPY . /workspace/sushiruntime/` assumes the context is the root; unchanged if the context stays `.` |
| `.dockerignore` | read from the context root, so it stays; BuildKit also accepts `.config/docker/Dockerfile.dockerignore`, which I did not test |
| prose | `docs/CONTRIBUTING.md:269,400,401`, `docs/guides/CLI_GUIDE.md:304`, `docs/guides/FAQ.md:60`, `README.md:168,554` |

No workflow under `.github/` names the Dockerfile. `cli/build/` holds stale untracked copies that also name both files.

`SECURITY.md` and `CODE_OF_CONDUCT.md` to `.github/`: `docs/README.md:34-35`, `docs/CONTRIBUTING.md:426,432,436`, `docs/guides/DISTRIBUTED_GUIDE.md:369,381`, `docs/guides/FAQ.md:178`, `docs/backlog/README.md:20`, `docs/publish.toml:7-8` (publishes both from `reference/`), `docs/backlog/README.md:111`. `.github/ISSUE_TEMPLATE/bug_report.yml:10` names the file without a path.

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

# Phase C: public API

Read-only task. I wrote no file, ran no build, and started no `se`, `cmake`, `ninja`, `ctest` or `sr` process. Everything below is read from the tree at `703eed9` (the working tree also carries uncommitted edits by other sessions under `docs/`, `.config/`, `.github/`, `cli/`; I did not touch them).

Path shorthand: `API/` is `D:\Projects\sushiruntime\modules\surface\api\include\SushiRuntime\api\`. Other paths are relative to `D:\Projects\sushiruntime\`.

### 1. Findings table

Status: **closed** (today's code already does it), **changed** (partly done, or the evidence moved), **stands**. Tasks are defined in section 5.

#### API

| ID | Status today | Decision | Task |
| --- | --- | --- | --- |
| API-01 | Stands. `create(const Topology::HardwareFilterConfig&, const RuntimeConfig&)`, `API/runtime/runtime.hpp:146-151`; `RuntimeConfig` is an alias of `Execution::RuntimeSettings`, `:52` | Fix now. One `RuntimeConfig` owned by the api module; two-argument `create` kept as deprecated forwarder | C12 |
| API-02 | Closed. `DeviceQueueSet::require_index` throws `SushiException{invalid_access}` with index and count (`modules/platform/device/source/device_queue_set.cpp:286-295`); the allocator throws `bad_device_index(device_index, queues_.size())` (`modules/platform/memory/source/usm_allocator.cpp:215`) | Add the `@throws` lines only | C12 |
| API-03 | Stands. `AdvancedControls` holds `Execution::RuntimeContext&` (`runtime.hpp:127`); the doc still says the Runtime must outlive graphs (`:133-135`, `:167`, `:179`) | Fix now, no break: `AdvancedControls` holds the `shared_ptr`; one lifetime rule in the docs | C12 |
| API-04 | Stands. Public `Buffer(std::shared_ptr<Execution::RuntimeContext>, ...)` at `API/data/buffer.hpp:168,197`; `State` at `API/data/state.hpp:77`; `Core::ResourceRegion` from `region()` `buffer.hpp:392`; `Execution::WorkerPoolInfo` `runtime.hpp:101` | Fix now. Aliases in `API` (`DeviceFilter`, `WorkerPool`, `BufferRegion`); context constructors become private. Break only for code that constructs a `Buffer` from a context | C10, C12 |
| API-05 | Stands. Clamping at `buffer.hpp:334-337`, `358-361`, `394-397`; null `src` skipped at `:363` | Fix now with a behaviour break: a window that does not fit throws `invalid_access` | C10 |
| API-06 | Stands. Level is `inline constexpr ACTIVE_LOG_LEVEL` (`modules/foundation/logging/include/SushiRuntime/logging/logger.hpp:66-71`); INFO goes to stdout (`:109`, `:181`); one INFO line per device (`device_queue_set.cpp:169`) | Fix now, no break in macros | C2 |
| API-07 | Stands. `version_matches()` at `modules/foundation/core/include/SushiRuntime/core/version.hpp:86-91`; `runtime.hpp` does not include it | Fix now, additive | C1, C12 |
| API-08 | Stands. `dependency_key` `buffer.hpp:377`, `liveness` `:384`; `State::read_key/write_key/liveness/advance` `state.hpp:113-126,178`; no `State::device_index()` | Fix now. Plumbing moves behind `detail::HandleAccess`; public spellings deprecated for v0.4.x | C10 |
| API-09 | Closed by phase B (stated in the brief; `buffer.hpp:410-430` has no `#ifdef`) | None | none |
| API-10 | Stands. `API/../SushiRuntime.h` includes scheduler, EBR, tracker and topology headers (17 engine includes, listed in section 2.8) | Fix now with a break | C13 |
| API-11 | Stands. `fluent.hpp` promotes 13 names; `buffer.hpp:462-465`, `state.hpp:211`, `dynamic_graph.hpp:1133` promote their own; `Dynamic`, `Region`, `PinPolicy` are not promoted | Fix now, additive, plus the namespace repair of the nine moved headers | C13 |
| API-12 | Stands. `interop()` is `const` (`runtime.hpp:210`), every factory is not (`:157-204`); copy is implicit | Fix now. Factories become `const`; copy deleted (break, question 7) | C12, C10 |
| API-13 | Changed. `distributed_demo.cpp` now calls `Distributed::Cluster::attach` (`applications/examples/distributed_demo.cpp:132`) but still builds `RuntimeContext` and `TaskGraph` by hand (`:103,125,161`) | Fix now, no break | C14 |
| API-14 | Stands. No example calls `try_run`, passes a `RuntimeConfig`, or uses `read_range`, `advanced()` or `Dynamic` (grep over `applications/examples/*.cpp`) | Fix now: one operating-path example | C14 |
| API-15 | Stands. Public fields `dynamic.hpp:89-115`; unchecked raw-pointer setters `:170,186,229,245` | Fix now. Fields private; setters take the `Buffer`; break for code that assigns the fields | C11 |
| API-16 | Stands. `count * sizeof(T)` unchecked at `buffer.hpp:177`; message "(out of USM memory)" at `:184-185` | Fix now, no break | C10 |
| API-17 | Not re-checked line by line | Defer to phase G (PLAN.md:113) | G |
| API-18 | Stands for the headers I read (`runtime.hpp:140-145` has no `@return`) | Each task documents what it touches; the sweep stays in phase G | G, per task |
| API-19 | Stands | Fix the rows that are public names now (table in 2.9); `Cluster` counts go to phase D | C12, C2, D |
| API-20 | Changed. `try_run`, `try_wait` and the bricks carry `[[nodiscard]]` (`graph.hpp:2000-2035`, `run_handle.hpp:182,198`); `Runtime`, `Buffer`, `State` carry none | Fix now; policy in 2.7 | C10, C12, C1 |
| API-21 | Stands | Ruled: leaves for SushiBLAS | C5, C6 |
| API-22 | Not re-checked; `cluster.hpp` moved to the distributed module | Defer to phase D, with the `int` counts of API-19 | D |

#### GRAPHAPI

| ID | Status today | Decision | Task |
| --- | --- | --- | --- |
| GRAPHAPI-01 | Stands. Capture form only for `Extent` (`graph.hpp:517-520`); coupled States have no launch form (`:607-609`); `NdRange` only untracked (`:373-375`) | Fix now. Launch and access become two descriptor families over one `Recorder::record`; old orders forward | C8 |
| GRAPHAPI-02 | Stands. `enable_if` only; class comment still promises "concept-constrained" errors (`graph.hpp:246-248`) | Fix now, no break | C8 |
| GRAPHAPI-03 | Stands. `BackingGraph = ::SushiRuntime::Graph::TaskGraph` (`graph.hpp:82`); `Graph` not promoted | Owner question 1 | C13 |
| GRAPHAPI-04 | Stands. `class Region : private Graph` (`API/graph/region.hpp:42`); `friend class Region; friend class DynamicGraph;` (`graph.hpp:2051-2052`); `static_cast<const Graph&>` at `dynamic_graph.hpp:175`, `as_graph` at `:436` | Fix now, no break. The recorder brick, section 3 | C5 |
| GRAPHAPI-05 | Stands (graph.hpp is 3 059 lines, dynamic_graph.hpp 1 135) | Do it first, as its own task | C5 |
| GRAPHAPI-06 | Stands. `compile_failed` for any `std::exception` at `graph.hpp:2179-2187`; `kernel_failed` at `run_handle.hpp:214-222` | Fix now, behaviour break | C9 |
| GRAPHAPI-07 | Stands. `size()` returns `task_count_` (`graph.hpp:1813`); `rule_registration` indexes `specs_` (`:1825-1828`) | Fix now, value break for graphs that record multi-node rules | C7 |
| GRAPHAPI-08 | Stands. `LateCount` aggregate at `graph.hpp:104-121`, `std::function` provider `:109` | Fix now with a break (aggregate becomes a class) | C11 |
| GRAPHAPI-09 | Stands | Fix now; old orders are deprecated forwarders | C8 |
| GRAPHAPI-10 | Stands. `DynamicGraph` has no `try_run()` without arguments (`dynamic_graph.hpp:263-286` lists three); `cancel()` not `noexcept` (`graph.hpp:1994`, `dynamic_graph.hpp:297`) | Fix now, additive | C9 |
| GRAPHAPI-11 | Stands. `Graph(Graph&&) = default` (`graph.hpp:258`); raw `Owner*` in `RunHandle` (`run_handle.hpp:251`) | Fix now; deleting `Region`'s move is a break that matches its README | C7 |
| GRAPHAPI-12 | Stands. `name_last(const char*)` stores the pointer (`graph.hpp:727-732`, `:3013`) | Fix now, no break | C7 |
| GRAPHAPI-13 | Stands. `dirty_regions_.insert(key)` on every access (`dynamic_graph.hpp:102`) | PLAN puts it in phase E, but it is a contract (what `region()` means), so fix now; the cost claim stays a phase E measurement | C7 |
| GRAPHAPI-14 | Stands | Defer. Moving bodies to `source/` needs a build-file change and a compile-time benchmark; phase E | E |
| GRAPHAPI-15 | Stands. `<cassert>` `graph.hpp:32`, `topology/hw_discovery.hpp` `:75`; `Runtime::dynamic_graph()` defined in `dynamic_graph.hpp:1125-1128` | Fix in the split | C5 |
| GRAPHAPI-16 | Stands (`graph.hpp:2327-2356`) | Fix with the recorder (one observer per allocation); number comes in phase E | C7 |
| GRAPHAPI-17 | Closed. `return std::move(*r);` (`graph.hpp:2115`) | None | none |
| GRAPHAPI-18 | Stands | Defer to phase E (needs a before and after) | E |
| GRAPHAPI-19 | Not re-checked | Defer to phase E | E |
| GRAPHAPI-20 | Stands for the items checked (`graph.hpp:101` "once per run", `:246-248`, `:256`) | Each task corrects the lines in its files; remainder in phase G | per task, G |
| GRAPHAPI-21 | Not re-checked | Phase G; C8 writes the "which add do I call" table because it defines it | C8, G |
| GRAPHAPI-22 | Stands. `add(State<T>&, Extent<N>, fn)` at `graph.hpp:573-594` compares nothing | Fix now, behaviour break for graphs that are already wrong | C8 |
| GRAPHAPI-23 | Stands. `BackingGraph` `graph.hpp:82`; `no_registration` `:1816`; `set_profiling_requested` `:1844` | Fix now with deprecated forwarders | C8, C9 |

#### CORE

| ID | Status today | Decision | Task |
| --- | --- | --- | --- |
| CORE-01 | Closed in phase A (brief) | None | none |
| CORE-02 | Not re-checked | Defer to phase E. The settings field it needs is additive, so it does not have to land before the freeze | E |
| CORE-03 to 08, 15, 16, 21 | Not re-checked | Phase E (PLAN.md:112) | E |
| CORE-09 | Stands. No `source/` folder in `modules/foundation/logging`; level constexpr (`logger.hpp:66-71`) | Fix now | C2 |
| CORE-10 | Closed by ruling (construction stays lock-free). The "track touched shards" variant is a phase E measurement | None here | E |
| CORE-11, 19 | Assigned to phase B; not re-checked | None | none |
| CORE-12 | Changed. The eight headers are still under `include/` (for example `modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp`) and the umbrella still includes four of them | C13 removes them from the umbrella. Leaving the installed set is build-file work, listed in PLAN.md:249-252 | C13, B follow-up |
| CORE-13 | Stands. Every `add_*` returns `void` (`modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp:98-222`); `name_last_node`, `width_last_node`, `last_node_width`, `last_registration_index` at `:133-169` | Fix now. `add_*` return `Node&`; accessors deprecated once the api module stops calling them | C3, C13 |
| CORE-14 | Stands. Public `num_workers` (`task_scheduler.hpp:98`), public `dispatch_loop` (`:340`), `get_completion_rate` (`:453`), `migrate_tasks` three-argument form (`:455`) | Fix now with deprecated forwarders | C3 |
| CORE-17 | Stands. `throw std::runtime_error` at `modules/engine/graph/source/task_graph.cpp:547,596`; `epoch_reclamation.hpp:183` | Fix now, no break (`SushiException` is a `std::runtime_error`) | C3, C1 |
| CORE-18 | Not re-checked | Phase G | G |
| CORE-20 | Not re-checked | Fix with CORE-14, same module | C3 |

#### EXEC

| ID | Status today | Decision | Task |
| --- | --- | --- | --- |
| EXEC-01, 03, 06, 10 | Closed (brief) | None | none |
| EXEC-02 | Stands. `throw std::runtime_error("hardware discovery failed.")` at `modules/platform/topology/source/hw_discovery.cpp:223`. The WSL2 pair is already one INFO line (`numa_mapper.cpp:85`) | Fix now, no break | C1 |
| EXEC-04 | Closed. `UsmAllocator::allocate(const UsmRequest&)` (`usm_allocator.hpp:79`, `usm_request.hpp:50-63`); the eight old names are deprecated forwarders (`runtime_context.hpp:225-273`) | None | none |
| EXEC-05 | Changed. `DeviceQueueSet::devices()` returns `std::vector<DeviceInfo>` (`device_queue_set.hpp:75`, `device_info.hpp:55-63`); `API::Runtime` exposes only a count (`runtime.hpp:79`) | Fix now, additive: `AdvancedControls::devices()`. Device order is question 8 | C12 |
| EXEC-07 | Changed. Index errors carry values (see API-02). `SR_THROW_IF` still throws `std::runtime_error` at `runtime_context.cpp:267,280` and `execution_engine.cpp:413-417` | Fix now: `SR_THROW_IF` throws `SushiException` | C2 |
| EXEC-08 | Closed as far as I read: one WARN at route build (`device_queue_set.cpp:367`). Lines 345-372 not read in full | None | none |
| EXEC-11 | Not re-checked | Phase G with the interop README | G |
| EXEC-12 | Stands. `capabilities.host_export = true` (`modules/surface/interop/source/interop.cpp:80`); `kind` defaults to `OpaqueWin32` (`external_memory.hpp:54`) | Fix now; removing the default of `kind` breaks SushiEngine's `to_external_descriptor`, which sets it explicitly (`runtime_backend.hpp:226-229`), so in practice nothing breaks | C1 |
| EXEC-22 | Closed. WARN with the address; adopted mappings are not freed (`usm_allocator.cpp:106-134`) | None | none |
| EXEC-05 order, EXEC-23 | Open | Question 8; the page is phase G | G |

### 2. Target public surface

"Fwd" means a `[[deprecated]]` forwarder is kept for v0.4.x. "Breaks" names the repository and the grep that shows the use.

#### 2.1 Runtime

```cpp
// api/runtime/runtime_config.hpp
namespace SushiRuntime::API
{
    using DeviceFilter = Topology::HardwareFilterConfig;   // include_cpu, include_gpu, include_accelerator, require_fp64
    using PinPolicy    = Scheduler::PinPolicy;
    using WorkerPool   = Scheduler::WorkerPoolInfo;

    struct RuntimeConfig
    {
        DeviceFilter devices;
        std::size_t  worker_count    = 0;
        std::size_t  first_core      = 0;
        PinPolicy    pinning         = PinPolicy::Pin;
        bool         profiling       = false;
        bool         rebalancer      = false;
        bool         force_scheduler = false;
    };
}

// api/runtime/runtime.hpp
class Runtime
{
    public:
        [[nodiscard]] static Runtime create(const RuntimeConfig& config = {});
        [[deprecated("use create(RuntimeConfig) and set RuntimeConfig::devices")]]
        static Runtime create(const DeviceFilter& devices, const RuntimeConfig& config);

        Runtime(Runtime&&) noexcept = default;
        Runtime& operator=(Runtime&&) noexcept = default;
        Runtime(const Runtime&) = delete;
        Runtime& operator=(const Runtime&) = delete;

        [[nodiscard]] Graph        graph() const;
        [[nodiscard]] DynamicGraph dynamic_graph() const;
        template <typename T> [[nodiscard]] Buffer<T> buffer(std::size_t count, DeviceIndex device = DeviceIndex{0},
                                                             Residency residency = Residency::Shared) const;
        template <typename T> [[nodiscard]] State<T>  state(std::size_t count, DeviceIndex device = DeviceIndex{0},
                                                            Residency residency = Residency::Shared) const;
        const Runtime& set_rebalancer_enabled(bool enabled) const;
        [[nodiscard]] AdvancedControls advanced() const noexcept;
        [[nodiscard]] Interop          interop() const noexcept;
        [[nodiscard]] Execution::RuntimeContext& unsafe_context() const noexcept;
};

class AdvancedControls            // holds std::shared_ptr<Execution::RuntimeContext>
{
    public:
        [[nodiscard]] std::size_t device_count() const;
        [[nodiscard]] const std::vector<Device::DeviceInfo>& devices() const;   // new, EXEC-05
        [[nodiscard]] bool is_profiling_enabled() const noexcept;
        [[nodiscard]] bool is_rebalancer_enabled() const;
        [[nodiscard]] WorkerPool worker_pool() const;
        [[nodiscard]] std::size_t host_to_device_bytes() const noexcept;
        [[nodiscard]] std::size_t device_to_host_bytes() const noexcept;
};
```

| Old | New | Fwd | Breaks |
| --- | --- | --- | --- |
| `create(const Topology::HardwareFilterConfig& = {}, const RuntimeConfig& = {})` | `create(const RuntimeConfig& = {})` | yes | SushiEngine compiles with a warning: `runtime_simulation.cpp:82` `Runtime::create({}, to_runtime_config(options))` |
| `using RuntimeConfig = Execution::RuntimeSettings` | the struct above, same flat field names | not needed | None found. SushiEngine assigns `worker_count` and `pinning` (`runtime_simulation.cpp:71-74`), which keep their names. Code that passes an `API::RuntimeConfig` where a `RuntimeSettings` is expected stops compiling; no such use in the three repositories (`grep -rn "RuntimeConfig" sushiengine sushiblas sushiai`, 2 hits, both above) |
| `Runtime& rebalancer(bool)` | `set_rebalancer_enabled(bool)` | yes | None found |
| `profiling_enabled()`, `rebalancer_enabled()` | `is_...` | yes | None found (`advanced()` is used once: `runtime_backend.hpp:909`, `device_count`) |
| implicit copy of `Runtime` | deleted | no forwarder possible | Not found in SushiEngine (`new Runtime(Runtime::create())` at `runtime_backend.hpp:994` is a move). Question 7 |
| `AdvancedControls(Execution::RuntimeContext&)` public constructor | private, `Runtime` is friend | no | None found |

`RuntimeConfig` converts to `Topology::HardwareFilterConfig` and `Execution::RuntimeSettings` in one private function of `runtime.hpp`; the lower structs keep their own types, and SushiAI keeps using `RuntimeSettings` with `RuntimeContext` directly.

#### 2.2 Buffer and State

```cpp
template <typename T>
class Buffer
{
    public:
        using value_type = T;
        Buffer() = default;
        Buffer(Buffer&&) noexcept;  Buffer& operator=(Buffer&&) noexcept;

        [[nodiscard]] T*          data() const noexcept;
        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] bool        empty() const noexcept;
        [[nodiscard]] DeviceIndex device_index() const noexcept;
        [[nodiscard]] Residency   residency() const noexcept;
        [[nodiscard]] bool        is_host_accessible() const noexcept;
        T& operator[](std::size_t i) const;
        [[nodiscard]] span<T>        host() const;
        [[nodiscard]] std::vector<T> read_range(ElementRange range) const;            // throws invalid_access
        void                         write_range(ElementRange range, span<const T> source) const;  // throws invalid_access
        [[nodiscard]] BufferRegion   region(ElementRange range) const;                // throws invalid_access
        Buffer& distributed() &;
        [[nodiscard]] Buffer distributed() &&;

    private:
        friend class Runtime;  friend class Interop;  template <typename> friend class State;
        friend struct detail::HandleAccess;
        Buffer(std::shared_ptr<Execution::RuntimeContext>, std::size_t, DeviceIndex, Residency);
        Buffer(std::shared_ptr<Execution::RuntimeContext>, Memory::Allocation, std::size_t, DeviceIndex, Residency);
};

template <typename T>
class State        // same member set as Buffer, plus current()/next(); data() is absent because there are two buffers
{
    public:
        [[nodiscard]] T* current() const noexcept;
        [[nodiscard]] T* next() const noexcept;
        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] bool        empty() const noexcept;
        [[nodiscard]] DeviceIndex device_index() const noexcept;      // new
        [[nodiscard]] Residency   residency() const noexcept;
        [[nodiscard]] bool        is_host_accessible() const noexcept;
        T& operator[](std::size_t i) const;
        [[nodiscard]] span<T>        host() const;
        [[nodiscard]] std::vector<T> read_range(ElementRange range) const;
        void                         write_range(ElementRange range, span<const T> source) const;
        [[nodiscard]] BufferRegion   region(ElementRange range) const;  // new: the current buffer's window
        State& distributed() &;
        [[nodiscard]] State distributed() &&;
};

// api/detail/handle_access.hpp: the one door the graph layer uses
namespace detail
{
    struct HandleAccess
    {
        template <typename T> static void* key(const Buffer<T>&) noexcept;
        template <typename T> static void* read_key(const State<T>&) noexcept;
        template <typename T> static void* write_key(const State<T>&) noexcept;
        template <typename T> static Memory::LivenessObserver liveness(const Buffer<T>&) noexcept;
        template <typename T> static Memory::LivenessObserver liveness(const State<T>&) noexcept;
        template <typename T> static void advance(State<T>&) noexcept;
    };
}
```

| Old | New | Fwd | Breaks |
| --- | --- | --- | --- |
| public context-taking constructors (`buffer.hpp:168,197`, `state.hpp:77`) | private | no | None found. SushiEngine wraps buffers it got from the factories (`runtime_backend.hpp:253`) |
| `dependency_key()`, `liveness()`, `read_key()`, `write_key()`, `advance()` | `detail::HandleAccess` | yes | None: `grep -rnE "\.(dependency_key|read_key|write_key|liveness)\(\)"` over the three repositories finds no runtime handle use |
| `write_range(ElementRange, const T*)` | `write_range(ElementRange, span<const T>)` | yes; the forwarder throws on null | SushiEngine `runtime_backend.hpp:313` compiles with a warning |
| clamping `read_range`, `write_range`, `region` | throw `invalid_access` naming offset, count and size | n/a | Behaviour. SushiEngine calls both (`runtime_backend.hpp:302,313`); whether any call relies on clamping was not checked |
| `host_accessible()` | `is_host_accessible()` | yes | Not grepped downstream |
| `Core::ResourceRegion` return type | `API::BufferRegion` = `Memory::ResourceRegion` | alias | None (same type) |

The allocating constructor shares the overflow check with the adopting one, and its failure message names element count, bytes, residency and device.

#### 2.3 Graph and DynamicGraph builder

One canonical order: **modifier, launch, access, callable**. The builder surface is one class template shared by `Graph` and `Region`, so the two cannot drift.

```cpp
// Launch descriptors (api/launch/)
std::size_t n                // 1-D, origin 0; kernel index is std::size_t
Extent<N>                    // field `offset` renamed `origin`; kernel index sycl::id<N>, or std::size_t when N == 1
NdRange<N>                   // gains `origin` and `at(origin)` (question 2)
Dynamic + capacity           // late-bound window; `Dynamic` is the modifier, capacity is the launch

// Access descriptors (api/vocabulary/)
Reads, Writes                // handles named; the kernel captures its own pointers
In<T>, Out<T>, InOut<T>...   // handles named once; the kernel receives the pointers
State<T>&                    // kernel receives (i, const T* current, T* next)
Coupled(out, in0, in...)     // new wrapper for the coupled-State form
On(handle_or_device)         // untracked: no ordering, placement stated (section 4)

template <class Derived>
class BuilderSurface
{
    public:
        template <class Launch, class F>              Derived& add(Launch launch, const Reads& reads, const Writes& writes, F&& kernel);
        template <class Launch, class... Captures, class F>
                                                      Derived& add(Launch launch, Captures... captures, F&& kernel);
        template <typename T, class F>                Derived& add(State<T>& state, F&& kernel);
        template <class Launch, typename T, class F>  Derived& add(Launch launch, State<T>& state, F&& kernel);
        template <class Launch, class... States, class F>
                                                      Derived& add(Launch launch, Coupled<States...> states, F&& kernel);
        template <class... Args>                      Derived& add(Dynamic modifier, Args&&... launch_access_kernel);
        template <class... Args>                      Derived& add(Tasks::OpID op, Args&&... launch_access_kernel_params);

        template <class Launch, class F>              Derived& add_untracked(Launch launch, F&& kernel);            // DeviceIndex{0}
        template <class Launch, class F>              Derived& add_untracked(Launch launch, On placement, F&& kernel);
        template <class F>                            Derived& add_untracked(On placement, F&& raw_kernel);         // fn(sycl::handler&)

        template <class F>                            Derived& add_host(const Reads& reads, const Writes& writes, F&& task);
        template <class F>                            Derived& add_host(F&& task);
        template <class F>                            Derived& add_emitter(F&& emitter);   // void(Graph::TaskGraph&), replayed at each build
        template <class F>                            Derived& extend(F&& extension);      // void(Recorder&), run once, section 3

        Derived& name(std::string_view label);        // copies; names every node of the last rule
        [[nodiscard]] std::size_t size() const noexcept;        // rules, one per add*/extend call
        [[nodiscard]] std::size_t node_count() const noexcept;  // recorded nodes
        [[nodiscard]] std::size_t last_rule() const noexcept;
        [[nodiscard]] Recorder& recorder() noexcept;
};

class Graph  : public BuilderSurface<Graph>  { /* plan cache + run surface */ };
class Region : public BuilderSurface<Region> { public: void clear(); /* no run surface; not movable */ };
```

Run surface, generated once from the step driver, identical on `Graph` and `DynamicGraph`:

```cpp
RunReport  run();                         RunReport  run(std::size_t steps);
RunReport& run(RunReport& reuse);         RunReport& run(std::size_t steps, RunReport& reuse);
template <class R, class P> RunReport  run_for(std::chrono::duration<R, P> budget);
template <class R, class P> RunReport& run_for(std::chrono::duration<R, P> budget, RunReport& reuse);
template <class Pred> RunReport  run_until(Pred done);
template <class Pred> RunReport& run_until(Pred done, RunReport& reuse);
// each of the eight has a [[nodiscard]] noexcept try_ twin returning Result<RunReport, Error> or Result<RunReport*, Error>
[[nodiscard]] RunHandle<Self> run_async();
[[nodiscard]] Result<RunHandle<Self>, Error> try_run_async() noexcept;
void cancel() noexcept;
Self& set_profiling(bool requested) noexcept;      [[nodiscard]] bool is_profiling_requested() const noexcept;
[[nodiscard]] std::uint32_t rule_registration(std::size_t rule) const noexcept;                    // Graph
[[nodiscard]] std::uint32_t rule_registration(RegionKey key, std::size_t rule) const noexcept;     // DynamicGraph
static constexpr std::uint32_t NO_REGISTRATION;
[[nodiscard]] std::size_t compile_count() const noexcept;   // Graph
[[nodiscard]] std::size_t compose_count() const noexcept;   // DynamicGraph
```

| Old (file:line) | New | Fwd | Breaks |
| --- | --- | --- | --- |
| `add(reads, writes, n, fn)` `graph.hpp:396`; `add(reads, writes, Extent, fn)` `:412`; `add(reads, writes, NdRange...)` `:442` | `add(launch, reads, writes, fn)` | yes | None found downstream (SushiEngine uses the `Dynamic` form only) |
| `add(Dynamic, reads, writes, capacity, fn)` `graph.hpp:644` | `add(Dynamic, capacity, reads, writes, fn)` | yes | SushiEngine warning: `runtime_backend.hpp:368`, `:656` |
| `add(State, Extent, fn)` `:573` | `add(Extent, State, fn)` | yes | None found |
| `add(State& out, State& in0, Rest&&...)` `:609` | `add(launch, Coupled(out, in0, ...), fn)`; a two-argument `add(Coupled, fn)` covers the whole-field case | yes | Not grepped downstream |
| `add(OpID, n, reads, writes, local, params)` `:1715,1732` | `add(OpID, n, reads, writes, local, params)` unchanged: it already matches "modifier, launch, access, callable, options" | n/a | None |
| `add(const std::vector<void*>&, const std::vector<void*>&, n, fn)` `:626`, `:659` | removed; `Reads`/`Writes` take a `BufferRegion`, which a raw pointer converts to | yes for v0.4.x | Not grepped downstream |
| `name_last(const char*)` `:727` | `name(std::string_view)` | yes; the forwarder accepts null, which SushiEngine passes (`runtime_backend.hpp:368-371` "node.name may be null") | SushiEngine warning at `runtime_backend.hpp:371,394,659,676` |
| `size()` counts specs `:1813` | counts rules; `node_count()` is the old number | n/a | Value changes only for graphs with multi-node rules. SushiEngine forwards `rule_registration(rule)` (`runtime_backend.hpp:547-549`); its callers index by node today when a primitive is in the graph and must be re-read in the upgrade commit |
| `no_registration` `:1816` | `NO_REGISTRATION` | yes | Not grepped downstream |
| `set_profiling_requested` / `profiling_requested` `:1844,1855` | `set_profiling` / `is_profiling_requested` | yes | SushiEngine warning: `runtime_backend.hpp:602-604` |
| `BackingGraph` alias `:82` | `Graph::TaskGraph` spelled out in `add_emitter`'s contract | yes | Not grepped downstream |
| `DynamicGraph::compile_count()` `dynamic_graph.hpp:187` | `compose_count()` | yes | Not grepped downstream |
| `DynamicGraph::region(key)` dirties on access `dynamic_graph.hpp:102` | does not dirty; a region is rebuilt when its recorder's generation changed | n/a | Behaviour. SushiEngine calls `graph_.region(key)` to create (`runtime_backend.hpp:809`) and then records into it, which still rebuilds |
| `Region` move-constructible | not movable | no | None found |
| `Extent::offset` field (`API/launch/extent.hpp:51`) | `origin` | no (a data member cannot forward) | Not found in SushiEngine (`grep -n "Extent" runtime_backend.hpp` has no hit); question 2 |
| `add_reduce`, `add_segmented_reduce`, `add_prefix_scan`, `add_counting_sort` | gone, section 3 | no | SushiEngine: `runtime_backend.hpp:410,428,448,483,526,685,733,745,762` |

Registration-time checks (GRAPHAPI-22) throw `invalid_graph` naming the rule, the extent with its origin, and the size it was compared with. Signature mistakes (GRAPHAPI-02) hit one `static_assert` per family in `api/detail/callable_checks.hpp`, stating the expected signature in words.

#### 2.4 Late-bound vocabulary

```cpp
// api/vocabulary/late_count.hpp
class LateCount
{
    public:
        using Provider = Core::Delegate<std::size_t(), 48>;                      // same type Dynamic uses (dynamic.hpp:78-84)
        [[nodiscard]] static LateCount fixed(std::size_t count);
        [[nodiscard]] static LateCount from_host(std::size_t capacity, Provider provider);
        [[nodiscard]] static LateCount from_device(std::size_t capacity, const Buffer<std::uint32_t>& slot, std::size_t index = 0);
        [[nodiscard]] std::size_t capacity() const noexcept;
};

// api/vocabulary/dynamic.hpp: fields private; free builders and members share one checked slot helper
[[nodiscard]] Dynamic when(Dynamic::WhenProvider);              Dynamic& Dynamic::when(WhenProvider) &;   // and && forms
[[nodiscard]] Dynamic sized(Dynamic::CountProvider);            Dynamic& Dynamic::sized(CountProvider) &;
[[nodiscard]] Dynamic sized_from_device(const Buffer<std::uint32_t>&, std::size_t index = 0);
                                                                Dynamic& Dynamic::sized_from_device(const Buffer<std::uint32_t>&, std::size_t index = 0) &;
[[nodiscard]] Dynamic based_at(Dynamic::BaseProvider);          // member likewise
[[nodiscard]] Dynamic based_at_device(const Buffer<std::uint32_t>&, std::size_t index = 0);
```

| Old | New | Fwd | Breaks |
| --- | --- | --- | --- |
| `struct LateCount { capacity; std::function host; const Buffer* device; device_index; }` `graph.hpp:104-121` | the class above; "exactly one source" cannot be written wrong, and the device index is checked at construction | no: public fields cannot stay if the invariant is to hold | **SushiEngine**: `runtime_backend.hpp:208-215` (`lc.capacity = capacity; lc.host = ...`) becomes `LateCount::from_host(capacity, ...)` or `fixed(capacity)` |
| `Dynamic` public fields `dynamic.hpp:89-115` | private | no | **SushiEngine**: `to_dynamic` (`runtime_backend.hpp:122-136`) builds a `Dynamic`; lines 125-136 were not read, so whether it assigns fields or chains builders is unverified |
| `and_when`, `and_sized`, `and_based_at`, `and_sized_from_device(const uint32_t*, index)`, `and_based_at_device(const uint32_t*, index)` | members named like the free functions, taking the `Buffer` | yes for the three provider forms; the two raw-pointer forms are removed because they skip the check | Not grepped downstream |

Both types document "resolved once per step", which is what the code does (the resolver node runs every step, `graph.hpp:2683-2694`).

#### 2.5 Errors

```cpp
namespace SushiRuntime::Core
{
    enum class Errc { ok, invalid_graph, compile_failed, allocation_failed, kernel_failed, device_lost,
                      invalid_access, dangling_resource, unsupported,
                      invalid_state };                              // new: the call is not allowed in the object's current state
    struct Error { Errc code = Errc::ok; std::string message; };
    class SushiException : public std::runtime_error { public: explicit SushiException(Error); const Error& error() const noexcept; };
    template <typename T, typename E> class [[nodiscard]] Result;   // members unchanged (result.hpp:67-186)
}
```

The library throws one type, `Core::SushiException`. `SR_THROW_IF` throws it with `invalid_state`.

| Where | Thrown or returned today | After |
| --- | --- | --- |
| Hardware discovery fails | `std::runtime_error("hardware discovery failed.")`, `hw_discovery.cpp:223` | `SushiException{unsupported}` carrying the SYCL text and the filter in force |
| Graph has no root, or a cycle | `std::runtime_error`, `task_graph.cpp:547,596` | `SushiException{invalid_graph}` naming one undrained node and its registration index |
| `execute` after shutdown, or while a run is in progress | `std::runtime_error` through `SR_THROW_IF`, `execution_engine.cpp:413-417` | `SushiException{invalid_state}` |
| Sync run while an async step is pending | `Error{invalid_graph}`, `graph.hpp:2159-2162` | `Error{invalid_state}` |
| `RunHandle::wait()` on an empty handle | `invalid_graph`, `run_handle.hpp:147-150` | `invalid_state` |
| Exception while building the plan | `compile_failed` | `compile_failed` |
| `sycl::exception` during a step | `kernel_failed`, `graph.hpp:2175-2178` | `kernel_failed` |
| Other exception during a step (a throwing host task) | `compile_failed` from `try_run` (`graph.hpp:2179-2187`), `kernel_failed` from `try_wait` (`run_handle.hpp:214-222`) | `kernel_failed` from both, through one `error_from_current_exception(phase)` in the step driver |
| Range that does not fit a handle | silent clamp, `buffer.hpp:334-337` | `invalid_access` with offset, count and size |
| Allocation fails | `allocation_failed`, "(out of USM memory)", `buffer.hpp:181-185` | `allocation_failed` with element count, bytes, residency and device |
| Header and library versions differ | nothing | `unsupported` with both versions (2.6) |

Message rule for every new or rewritten message: the call, the offending value, the limit it broke, and the fix. Builder calls (`add`, `buffer`, `LateCount::from_device`) throw at the call. Run entries throw from `run*` and return from `try_run*`; nothing throws from a destructor.

Breaks: the `Errc` of three conditions changes. SushiAI reads `Core::Errc` twice (`grep -rhoE "SushiRuntime::Core::Errc" sushiai`, 2 hits); which enumerators it compares was not read.

#### 2.6 Logging and the version check

```cpp
// logging/logger.hpp; the state lives in modules/foundation/logging/source/logger.cpp, exported once
namespace SushiRuntime::Core
{
    enum class LogLevel : int { Off = 0, Error, Warn, Info, Debug };
    using LogType [[deprecated("use LogLevel")]] = LogLevel;     // ERR/WARN/INFO/DEBUG/OFF kept as deprecated enumerators of equal value
    using LogSink = Core::Delegate<void(LogLevel level, std::string_view line), 48>;

    SUSHIRUNTIME_API void     set_log_level(LogLevel level) noexcept;
    SUSHIRUNTIME_API LogLevel log_level() noexcept;
    SUSHIRUNTIME_API void     set_log_sink(LogSink sink);        // empty restores the default: stderr, every level
    SUSHIRUNTIME_API void     log_write(LogLevel level, std::string&& line);
}
```

The default level is `Warn`. `SR_LOG_LEVEL` is read once when the logger is first used. The compile-time `SR_ENABLE_LOGGING` switch stays as an upper bound. Each macro loads the level with a relaxed read before formatting and is wrapped in `do { } while (0)`. `Runtime::create()` then prints nothing on a healthy machine, because every start-up line is INFO (`device_queue_set.cpp:169,371`, `hw_discovery.cpp:54`, `numa_mapper.cpp:49`). `SR_LOGGER_SET_SYNC_MODE` forwards to an exported function, so a test executable reaches the library's logger.

Logging is process state, so it is set by free functions and `RuntimeConfig` has no log field (question 3).

```cpp
// core/version.hpp
[[nodiscard]] inline bool version_matches() noexcept;             // unchanged
inline void require_matching_version();                           // throws SushiException{Errc::unsupported}
```

`Runtime::create` is header-inline, so it calls `require_matching_version()` first. The message reads: "SushiRuntime headers are 0.4.0 and the loaded library is 0.3.2; an older sushiruntime shared library is earlier on the loader path." No downstream break.

#### 2.7 `[[nodiscard]]` policy

| Gets it | Does not |
| --- | --- |
| Factories and anything returning an owning handle: `Runtime::create`, `graph`, `dynamic_graph`, `buffer`, `state`, `run_async`, `interop`, `export_host`, `import_device`, `LateCount::*`, the free `Dynamic` builders, rvalue `distributed() &&` | Fluent calls that return `*this`: `add*`, `name`, `extend`, `set_*`, lvalue `distributed() &` |
| Status values: `Result` and `RunHandle` at class level, `version_matches`, `ready`, `pending` | `run*` returning a report; dropping a report is ordinary use |
| Pure queries: `size`, `empty`, `device_count`, `devices`, `read_range`, `region`, `rule_registration` | `operator[]` |

A consumer that drops one of these results gets a new warning, which is an error under `-Werror`; the changelog says so.

#### 2.8 Umbrella header

`SushiRuntime.h` holds two includes: `core/version.hpp` and `api/fluent.hpp`. `fluent.hpp` holds the includes of the api leaf headers and `core/error.hpp`, `core/result.hpp`, and no `using` line.

Removed from the umbrella (today at `SushiRuntime.h`, non-comment lines): `core/export.hpp`, `core/common.hpp`, `core/sushi_ptr.hpp`, `core/ref_counted.hpp`, `logging/logger.hpp`, `topology/hw_discovery.hpp`, `topology/numa_mapper.hpp`, `topology/affinity.hpp`, `memory/epoch_reclamation.hpp`, `memory/usm_registry.hpp`, `execution/execution_engine.hpp`, `task/node_widths.hpp`, `graph/dependency_tracker.hpp`, `scheduler/task_scheduler.hpp`, `scheduler/range_task.hpp`, `scheduler/event_polling.hpp`, `scheduler/rebalancer_interfaces.hpp`.

**Breaks SushiBLAS**: 16 files include the umbrella (`grep -rlE "#include <SushiRuntime/SushiRuntime.h>" sushiblas`). `modules/data/tensor/include/SushiBLAS/tensor/storage.hpp:34` includes nothing else from the runtime and uses `Execution::RuntimeContext` (`:65`), `Core::RefCounted` (`:54`) and `Core::DEFAULT_ALIGNMENT` (`:43`). Some of those names stay reachable through the api headers' own includes; which of the 16 files stop compiling can only be told by compiling them, which I did not do. **SushiAI**: one file, `tests/common/test_environment.hpp`. **SushiEngine**: 4 files, all using the fluent surface. The changelog line names the header to include for each removed name.

#### 2.9 Name promotion and namespaces

One rule: each leaf header promotes the public names it declares into `SushiRuntime::`; `fluent.hpp` promotes none. After the phase these are reachable as `SushiRuntime::X`: `Runtime`, `RuntimeConfig`, `DeviceFilter`, `PinPolicy`, `WorkerPool`, `AdvancedControls`, `Buffer`, `State`, `Residency`, `DeviceIndex`, `ElementRange`, `BufferRegion`, `Extent`, `NdRange`, `Reads`, `Writes`, `In`, `Out`, `InOut`, `Coupled`, `On`, `Dynamic`, `LateCount`, `DynamicGraph`, `Region`, `RunHandle`, `Recorder`, `Error`, `Errc`, `RunReport`, `SushiException`, `Result`, `LogLevel`, `set_log_level`, `set_log_sink`. `when`, `sized`, `based_at` and their device forms stay in `API`: they are short verbs, and a `using namespace SushiRuntime` should not bring `when` into scope. `Graph` is question 1.

The nine headers that changed module keep namespace `Core` today (`Core::ResourceRegion` in `memory/`, `Core::RunReport` in `observability/`; `graph.hpp:3016,3031` and `run_report.hpp:155,218`). Fix: declare each in the namespace of its module and leave an alias in `Core`:

```cpp
namespace SushiRuntime::Core { using ResourceRegion [[deprecated("use Memory::ResourceRegion")]] = Memory::ResourceRegion; }
```

for `ResourceRegion`, `ResourceId`, `LivenessObserver`, `LivenessToken`, `make_liveness` (to `Memory`); `RunReport`, `NodeTiming`, `WorkerTiming`, `NodeEventRecord`, `ClockPairing`, `ExecutionLayer`, `ProfilingLedger`, `WIDTH_UNKNOWN` (to `Observability`). Downstream compiles with warnings: SushiBLAS `Core::ResourceRegion` 16 uses, SushiEngine `Core::RunReport` 12, `Core::NodeEventRecord` 11, `Core::ResourceRegion` 3, `Core::WIDTH_UNKNOWN` 2 (`grep -rhoE "SushiRuntime::[A-Za-z_:]+" | sort | uniq -c`). SushiEngine `static_assert`s the layout of `NodeEventRecord` (`runtime_backend.hpp:51-72`); the type does not change, only its namespace.

Casing (API-19): `Errc` enumerators stay snake_case (they mirror `std::errc` and SushiAI reads them); `Residency`, `PinPolicy`, `LogLevel`, `ExecutionLayer` are PascalCase; `Core::Detail` becomes `Core::detail` with no forwarder (it is not public). `StateView::cur` becomes `current`.

### 3. The recorder split and the numeric primitives

#### What exists today only for reduce, scan and counting sort

| Item | Where | Used by anything else? |
| --- | --- | --- |
| The eight primitive overloads, about 970 lines | `graph.hpp:734-1703` | no |
| `emit_late_count_resolver` | `graph.hpp:2651-2700` | no (four call sites, all in the primitives: `:901,1083,1167,1219,1450`) |
| `emit_device_sized` | `graph.hpp:2710-2759` | no |
| `registration_token` and `width` parameters of `emit_per_element_tracked` | `graph.hpp:2451-2453` | no; the public `add` passes neither (`:401`) |
| `reduce_scratch_`, `reduce_registration_tokens_` and the token reset in `build_plan` | `graph.hpp:3022,3025,2972-2973` | `reduce_scratch_` also holds fault records (`:2619`) |
| `LateCount` and its four helpers | `graph.hpp:104-241` | no: `grep -rl LateCount` finds only `graph.hpp`, `device_fault.hpp`, `fluent.hpp` and tests |
| `DEVICE_FAULT_LATE_COUNT` | `API/detail/device_fault.hpp:85-88` | no |
| `detail::make_reduce_scratch` | `API/detail/fixed_order_reduce.hpp:118-133` | yes: `allocate_fault_record` (`graph.hpp:2615`), which `Dynamic` with a device slot needs (`:2629-2641`) |
| `fixed_order_reduce.hpp`, `fixed_order_scan.hpp`, `vocabulary/combine.hpp` | 152, 183 and 109 lines | no |
| Four `using Graph::add_*` lines | `region.hpp` (the four names after `add_host`) | no |

`DynamicGraph` holds nothing for the primitives; it replays whatever specs a region recorded (`dynamic_graph.hpp:678-682`). One thing I noticed there and did not run: `build_region` does not reset the registration tokens that `build_plan` resets (`graph.hpp:2972-2973`), so after a region rebuild a multi-node rule may keep the index reserved on the previous backing graph until `renumber_registration_indices` (`dynamic_graph.hpp:902`) rewrites it. The recorder design below removes the token, so the question disappears with it.

#### The recorder brick

`Recorder` is the single owner of recorded work. `Graph` is a recorder plus a plan cache plus the step driver; `Region` is a recorder; `DynamicGraph` composes regions through the recorder's public const interface, with no friend and no cast.

```cpp
// api/graph/recorder.hpp
namespace SushiRuntime::API
{
    /** What a node reads and writes, with the liveness of the handles it names. */
    struct Access
    {
        Access() = default;
        Access(const Reads& reads, const Writes& writes);
        std::vector<BufferRegion> reads;
        std::vector<BufferRegion> writes;
        std::vector<Memory::LivenessObserver> lives;
    };

    /** A launch that runs at capacity and reads its live width on the device. */
    struct DeviceSized
    {
        std::size_t capacity = 0;
        const std::uint32_t* live_width = nullptr;   // work-items at or past *live_width return; null means all run
        const std::uint32_t* live_words = nullptr;   // one or two words handed to the kernel, by its arity
    };

    struct NodeOptions
    {
        std::size_t width = Observability::WIDTH_UNKNOWN;   // launch width stamped into node event records
        std::optional<DeviceIndex> placement;               // for a node with no region
    };

    class Recorder
    {
        public:
            Recorder(Recorder&&) noexcept;             // resets the moved-from counters
            Recorder& operator=(Recorder&&) noexcept;
            Recorder(const Recorder&) = delete;

            /** Groups every node recorded until the scope ends into one rule: one registration index, one name, one unit of size(). */
            class RuleScope;
            [[nodiscard]] RuleScope rule();

            template <class F> void kernel(std::size_t count, const Access& access, F&& fn, NodeOptions options = {});
            template <int N, class F> void kernel(Extent<N> extent, const Access& access, F&& fn, NodeOptions options = {});
            template <int N, class F> void kernel(NdRange<N> range, const Access& access, F&& fn, NodeOptions options = {});
            template <class F> void kernel(DeviceSized launch, const Access& access, F&& fn, NodeOptions options = {});
            template <class F> void kernel(Dynamic window, std::size_t capacity, const Access& access, F&& fn, NodeOptions options = {});
            template <class F> void raw_kernel(const Access& access, F&& fn, NodeOptions options = {});        // fn(sycl::handler&)
            template <class F> void host(const Access& access, F&& fn, NodeOptions options = {});
            template <class F> void emitter(F&& fn);                                                          // void(Graph::TaskGraph&)

            /** One single-task node that resolves K late counts and hands the live values to fill, on the device. */
            template <std::size_t K, class Fill>
            void resolve(const std::array<LateCount, K>& counts, DeviceIndex device, const Access& writes,
                         Fill fill, NodeOptions options = {});

            /** USM the recorder owns and frees with itself; ordering regions are built from the returned span. */
            template <typename T> [[nodiscard]] span<T> scratch(std::size_t count, DeviceIndex device, Residency residency);

            void name_last_rule(std::string_view label);
            void clear();

            [[nodiscard]] std::size_t rule_count() const noexcept;
            [[nodiscard]] std::size_t node_count() const noexcept;
            [[nodiscard]] std::uint64_t generation() const noexcept;        // bumped by every record; DynamicGraph compares it
            [[nodiscard]] bool has_dangling_resource() const noexcept;      // one observer per distinct allocation
            void replay(Graph::TaskGraph& target) const;                    // what build_plan and build_region call
            [[nodiscard]] const detail::StepMachinery& steps() const noexcept;
    };
}
```

Every `kernel`, `host` and `resolve` goes through one private `record(NodeSpec)`, which appends the spec, attaches it to the open rule or opens a one-node rule, records the liveness of `access.lives`, and bumps the generation. Today that epilogue is written out at each emit site (`++task_count_; dirty_ = true; return *this;`, for example `graph.hpp:287-289`, `:2474-2476`, `:2698-2699`, `:2757-2758`), and a primitive has to remember `observe(reads); observe(writes);` before each emit (the comment at `graph.hpp:761` and `:813` says so). With `Access` carrying the observers, a domain library cannot forget either step.

`RuleScope` replaces the shared `std::shared_ptr<std::optional<std::uint32_t>>` token: the recorder knows which specs belong to a rule and reserves one registration per rule at replay. `resolve` allocates the fault record itself when a count has a device source (today `graph.hpp:2669`), and the step driver checks it; a domain library never sees fault records.

#### How SushiBLAS adds `reduce`

A free function over the public interface, with no friend access:

```cpp
// SushiBLAS, sketch
template <typename T, class Combine>
void reduce(SushiRuntime::Recorder& recorder, const SushiRuntime::Buffer<T>& input, const SushiRuntime::Buffer<T>& output,
            SushiRuntime::LateCount count, Combine combine, T identity)
{
    auto rule = recorder.rule();                                             // one rule, one registration index
    const auto levels = detail::plan_tiled_levels(count.capacity());
    span<T> scratch = recorder.scratch<T>(detail::reduce_scratch_count(levels), input.device_index(), input.residency());
    span<std::uint32_t> live = recorder.scratch<std::uint32_t>(2 * levels.size(), input.device_index(), input.residency());

    recorder.resolve(std::array<SushiRuntime::LateCount, 1>{count}, input.device_index(),
                     Access(Reads(), Writes(region_of(live))),
                     [words = live.data(), n = levels.size()](const std::array<std::size_t, 1>& v)
                     { detail::fill_live_level_words(n, v[0], words); }, {1});

    for (each level i)
        recorder.kernel(DeviceSized{level.out_count, live.data() + levels.size() + i, live.data() + i},
                        Access(Reads(source_region, region_of(live)), Writes(target_region)),
                        [src, dst, combine](std::size_t t, std::size_t in_count) { /* fold one tile */ },
                        {level.out_count});
}
```

The caller writes `SushiBLAS::reduce(graph.recorder(), in, out, n, Sum<float>{}, 0.0f);`, or inside a chain `graph.add(...).extend([&](Recorder& r) { SushiBLAS::reduce(r, in, out, n, Sum<float>{}, 0.0f); }).name("total")`. It works the same on a `Region`, because `Region` has the same `recorder()` and `extend()`. The three-arity kernel contract of `DeviceSized` (`fn(t)`, `fn(t, w0)`, `fn(t, w0, w1)`) is today's `if constexpr` ladder at `graph.hpp:2743-2752`, kept and documented.

#### Files that leave the runtime

| Leaves | Lines |
| --- | --- |
| `API/vocabulary/combine.hpp` | 109 |
| `API/detail/fixed_order_reduce.hpp`, except `make_reduce_scratch`, which becomes `Recorder::scratch` | 152 |
| `API/detail/fixed_order_scan.hpp` | 183 |
| `graph.hpp:734-1703`, first moved by C5 into `API/primitives/{reduce,segmented_reduce,prefix_scan,counting_sort}.hpp` as free functions over `Recorder`, then deleted by C6 | about 970 |

#### Tests

`modules/surface/api/tests/numeric/` holds 57 tests in 7 files (counted with `grep -cE "^TEST"`). I read their names, not their bodies.

| File | Tests | Leaves with the primitives | Stays, rewritten on a trivial kernel |
| --- | --- | --- | --- |
| `test_counting_sort.cpp` | 17 | all 17 | none |
| `test_prefix_scan.cpp` | 8 | all 8 | none |
| `test_fixed_order_reduce.cpp` | 11 | all 11 | none |
| `test_reduce_dispatch_width.cpp` | 4 | all 4 | replaced by one test: `NodeOptions::width` reaches `NodeEventRecord::launched_width` |
| `test_late_count.cpp` | 10 | 5 (`LevelCountsAtTileBoundaries`, `LiveCountBelowCapacityMatchesThatCountAtCapacity`, `LiveCountZeroFoldsNothing`, `RadixDigitCountAtByteBoundaries`, `RadixPlanSkipsDigitsAboveTheLiveKeyRange`) | 5 (`DefaultResolvesToCapacity`, `HostProviderBelowCapacity`, `AboveCapacityIsRejected`, `DeviceSlotResolvesFromHostResidentBuffer`, `NonHostDeviceSlotIsRejectedAtRegistration`), against `Recorder::resolve` plus a kernel that copies the live count out |
| `test_late_count_ordering.cpp` | 1 | 0 | 1: the resolver sees the count its predecessor wrote |
| `test_device_sized_primitives.cpp` | 6 | 4 (the four `...ChainMatchesScheduler...` cases named after a primitive) | 2 (`SizedFromDevice...`, `DeviceCountOverCapacityFailsAChainedRun`), plus one new: a `DeviceSized` kernel that marks the items it ran gives the same bytes on the chain and the scheduler layer as the count changes |

Outside that folder: `tests/unit/test_registration_index.cpp:192,239,298` (three cases on `add_reduce`) are rewritten on `Recorder::rule()` with three trivial kernels; `tests/integration/test_determinism.cpp:103-104,233` leave for SushiBLAS as bit-equality tests; `modules/surface/api/tests/test_rule_registration.cpp:81-122` is rewritten on a rule scope. The runtime's kept tests cover: rule grouping and registration, name per rule, `size()` against `node_count()`, late count from each of the three sources, over-capacity fault through both layers, device-sized launch, scratch freed with the recorder, and the dangling check for handles named through `Access`.

#### SushiBLAS side, in outline

`D:\Projects\sushiblas\cmake\SushiBLASLayers.cmake` orders the layers foundation, data, kernels, engine, operations, library. SushiBLAS today builds on `Graph::TaskGraph` (6 uses) and never on `API::Graph` (no hit for `API::Graph` or `API::Runtime` in the count above), so this is its first code over the fluent recorder, and it does not touch tensors or the SushiBLAS engine. Two modules, because the plan arithmetic and tile kernels need no graph and the recording functions do:

| Module | Layer | Holds | Depends on |
| --- | --- | --- | --- |
| `ordered_kernels` | kernels | `combine.hpp` (`Sum`, `Minimum`, `Maximum`), reduction and scan level plans, `fold_tile`, radix plan; header-only | `core` |
| `ordered` | operations | `reduce`, `segmented_reduce`, `prefix_scan`, `counting_sort` as free functions over `SushiRuntime::Recorder`; the 49 algorithm tests and the determinism tests | `core`, `ordered_kernels` |

Both need a line in `SUSHIBLAS_MODULE_LAYERS`. Whether `sushiblas_add_module` accepts a module with no `SOURCES` was not checked. SushiBLAS already has `engine/task_recorder.hpp` and `support/reduce_1d.hpp`; the runtime type is named `Recorder`, not `TaskRecorder`, so the two do not read as one thing. Names are question 6. Order: SushiBLAS upgrades the runtime to `v0.4.0` (adapting to 2.8 and to the deprecations), adds the two modules, releases.

#### SushiEngine call sites that change

All runtime use sits behind one seam file, `D:\Projects\sushiengine\engine\foundation\execution\include\SushiEngine\execution\backend\runtime_backend.hpp`. The engine's own `Sum`, `Minimum`, `Maximum` come from its math library (`engine/foundation/core/include/SushiEngine/core/types.hpp:103-106`), so the solver files that call `add_reduce` on the engine's `Graph` wrapper (`runtime_matter_solver.hpp`, `matter_device_neighbours.hpp`, `matter_ghost_gather.hpp`, `runtime_graph_builder.hpp:752`) do not change. `native_backend.hpp` implements the same four primitives without the runtime and does not change.

| Site | Change |
| --- | --- |
| `runtime_backend.hpp:410`, `:685` | `graph_.add_reduce(...)` becomes `SushiBLAS::reduce(graph_.recorder(), ...)` |
| `:428`, `:448`, `:733` | `add_segmented_reduce` likewise |
| `:483`, `:745` | `add_prefix_scan` likewise |
| `:526`, `:762` | `add_counting_sort` likewise; the nullable `const Buffer*` (`:524`) becomes an overload |
| `:208-215` | `to_late_count` uses `LateCount::from_host` / `fixed` |
| `:122-136` | `to_dynamic`, if it assigns `Dynamic` fields (not read) |
| `:368`, `:656` | argument order of `add(Dynamic, ...)` (warning until changed) |
| `:371`, `:394`, `:659`, `:676` | `name_last` to `name`, guarding the null name |
| `:547-549` | `rule_registration` now takes a rule index |
| `:602-604` | `set_profiling` |
| `engine/world/simulation/source/runtime_simulation.cpp:82` | `Runtime::create(config)` |
| `engine/domain/audio/include/SushiEngine/audio/accelerator_sycl.hpp:63,94-97` | `context_->free_usm` is already deprecated; move to `memory().release` |
| `tests/integration/test_animation_device_parity.cpp:319` | `unsafe_context().get_queue(0)` is already deprecated; move to `devices().queue(0)` |

### 4. The untracked-kernel routing defect

Where it happens. A node with no region has no anchor (`modules/engine/scheduler/source/task_scheduler.cpp:750-755`), so `recorded_device_index` returns no context and `cached_queue_index_` stays negative (`:784`). The queue then comes from `devices_.queue_or_numa_local(std::nullopt, target_numa_id)` (`:799-800`), which calls `next_device_for_numa` (`modules/platform/device/source/device_queue_set.cpp:263-267`), and that rotates over the node's devices (`:318-326`, `next_rotation`). `add_untracked` records `bg.add_node(fn)` with no region (`graph.hpp:281`, `:320`), so its kernel, which captured a pointer the runtime cannot see, lands on whichever device the rotation reaches. The chain layer refuses such a node (`owning_queue_index` returns -1 for no region, `modules/engine/execution/source/chain_plan.cpp:64-80`, `:156-158`), so the defect is on the scheduler layer only.

The rule. Every graph node has one placement, fixed when the plan is built, from the first of these that applies:

1. It names regions: the device that owns the first write region, else the first read region. This is today's rule and does not change.
2. It names no region and states a placement: that device. `On(buffer)` and `On(state)` resolve to the handle's `device_index()` and also register the handle's liveness, so a freed buffer is caught by the dangling check; `On(DeviceIndex)` names the device outright.
3. It names nothing: `DeviceIndex{0}`, the same default `Runtime::buffer` and `Runtime::state` use (`runtime.hpp:171,183`). A default buffer and a default untracked kernel therefore always meet on one device.

A `Task` that is not a graph node keeps NUMA-local placement without rotation: `device_for_numa(numa)` returns the first device of the route (`device_queue_set.cpp:255-261`).

Mechanics. `TaskGraph::add_node` returns `Node&` (CORE-13) and `Node` gets `place_on(std::size_t device)`; a region-less node defaults to 0. One function, `placement_of(const Node&, const Memory::UsmAllocator&)`, in the task module, answers for both the scheduler (`task_scheduler.cpp:789-808`, whose fallback branch goes away for nodes) and `classify_chain` (`chain_plan.cpp:69`). The second use makes a graph with untracked kernels chainable when all placements agree, which changes `RunReport::layer` for such graphs; if the owner prefers to leave the chain rule alone for now, C3 limits the function to the scheduler and nothing else in this section changes.

The test. `tests/integration/test_layer_equivalence.cpp:735-737` loses its `GTEST_SKIP`. Its body does not change: I did not read `run_untracked_launches`, but the skip comment and the known-issues entry say its buffers come from the default device. One test is added beside it: on a machine with more than one device, a buffer on `DeviceIndex{1}` and `add_untracked(n, On(buffer), fn)` give the host reference's bytes under both layers; on one device it runs with `DeviceIndex{0}`. `docs/backlog/README.md:10` and `docs/backlog/README.md:15-22` lose their entries in the same task.

### 5. Task list

`graph.hpp` and `dynamic_graph.hpp` are split first, by one worker who holds both (C5). The split comes before every finding that touches them because (a) after it the findings land in different files and can run in parallel, and (b) C5 also moves the primitives onto the public recorder interface while their 57 tests still exist, which is the only point at which the interface can be proven sufficient by tests that already pass. C6 then deletes code that no longer has a private dependency.

Build-file edits (new `logger.cpp` in the logging target, the api test list, the module list of any new header-check) are the orchestrator's, as in phase A's A7.

| Task | Closes | Files | Acceptance (one) | Waits on |
| --- | --- | --- | --- | --- |
| C1 core and platform errors | EXEC-02, EXEC-12, CORE-17 (EBR part), API-07 (function), API-20 (`Result`) | `modules/foundation/core/include/SushiRuntime/core/{error,result,version}.hpp`, `modules/platform/topology/source/hw_discovery.cpp`, `modules/platform/memory/include/SushiRuntime/memory/epoch_reclamation.hpp`, `modules/surface/interop/source/interop.cpp`, `modules/surface/interop/include/SushiRuntime/interop/external_memory.hpp`, their module tests | `grep -rn "throw std::runtime_error" modules/platform modules/surface/interop modules/foundation` is empty, and a test catches `SushiException` with `Errc::unsupported` from a filter that excludes every device | none |
| C2 logger | CORE-09, API-06, EXEC-07 | `modules/foundation/logging/include/SushiRuntime/logging/logger.hpp`, new `modules/foundation/logging/source/logger.cpp`, `modules/foundation/logging/tests/` | A test captures stdout and stderr around `Runtime::create()` at the default level and both are empty; the same test sets `LogLevel::Info` and sees the device line through its sink | C1 (`invalid_state`); orchestrator adds the source to the target |
| C3 engine graph and placement | CORE-13, CORE-14, CORE-17, CORE-20, routing defect (engine half) | `modules/engine/task/include/SushiRuntime/task/node.hpp`, `modules/engine/graph/include/SushiRuntime/graph/task_graph.hpp`, `modules/engine/graph/source/task_graph.cpp`, `modules/engine/scheduler/include/SushiRuntime/scheduler/{task_scheduler,thermal_proxy_strategy}.hpp`, `modules/engine/scheduler/source/task_scheduler.cpp`, `modules/engine/execution/source/chain_plan.cpp`, their module tests | A unit test builds two region-less nodes on a two-entry synthetic device set and both dispatch to device 0; a third placed with `place_on(1)` dispatches to device 1 | C1. The four `*_last_node` accessors stay undeprecated here, because the api module still calls them |
| C5 split and recorder | GRAPHAPI-04, 05, 15; API-21 (first half) | `API/graph/{graph,dynamic_graph,region,run_handle}.hpp`, new `API/graph/{recorder,builder_surface,plan_cache,step_driver,boundary_access,region_composer}.hpp`, new `API/detail/{emit_*,late_window,device_sized,late_count_resolution}.hpp`, `API/detail/fixed_order_reduce.hpp` (scratch helper out), new `API/vocabulary/late_count.hpp` (moved as is), new `API/primitives/*.hpp`, `API/runtime/runtime.hpp` (factory definition only) | `grep -n "friend class Region\|friend class DynamicGraph\|static_cast<const Graph&>\|static_cast<Graph&>" API/graph` is empty, `API/primitives/*.hpp` name no private member, and every existing api test passes unchanged, with the primitives still reachable through forwarding members | none (file set disjoint from C1 to C3) |
| C6 primitives leave | API-21 | delete `API/primitives/`, `API/vocabulary/combine.hpp`, `API/detail/fixed_order_{reduce,scan}.hpp`; `API/fluent.hpp` (one include); `modules/surface/api/tests/numeric/` (per the table in section 3); `tests/unit/test_registration_index.cpp`, `tests/integration/test_determinism.cpp`, `modules/surface/api/tests/test_rule_registration.cpp`; `API/graph/README.md`, `API/vocabulary/README.md`, `API/detail/README.md` | `grep -rnE "add_reduce|add_segmented_reduce|add_prefix_scan|add_counting_sort|fold_tile|Sum<" modules tests applications` is empty and the kept tests of section 3 pass. The commit before this one is recorded as the source SushiBLAS copies from | C5 and the build after it |
| C7 recorder semantics | GRAPHAPI-07, 11, 12, 13, 16 | `API/graph/{recorder,region,dynamic_graph,plan_cache,region_composer}.hpp`, new tests | `world.region(k).size()` between two runs leaves `compose_count()` unchanged, and a graph with one three-node rule reports `size() == 1`, `node_count() == 3` and one name on all three `NodeTiming` rows | C6 |
| C10 data handles | API-04, 05, 08, 12, 16, 20 | `API/data/{buffer,state}.hpp`, new `API/detail/handle_access.hpp`, `API/vocabulary/{dependencies,captures}.hpp`, `API/detail/{step_machinery,callable_traits}.hpp`, `API/interop/interop.hpp`, `API/data/README.md`, tests | `read_range({size, 1})` throws `invalid_access` whose message holds the offset, the count and the size; no file under `API/` outside `detail/handle_access.hpp` calls `dependency_key`, `read_key`, `write_key`, `liveness` or `advance` | C6 |
| C12 runtime entry | API-01, 02, 03, 07, 12, 19, 20; EXEC-05 | `API/runtime/runtime.hpp`, new `API/runtime/{runtime_config,advanced_controls}.hpp`, `API/runtime/README.md`, tests | `Runtime::create({.worker_count = 2})`-style construction (one struct) yields `advanced().worker_pool().count == 2`, and an `AdvancedControls` taken before its `Runtime` is destroyed still answers `device_count()` | C1, C6 |
| C11 late-bound vocabulary | GRAPHAPI-08, API-15 | `API/vocabulary/{late_count,dynamic}.hpp`, `API/detail/{late_count_resolution,device_fault}.hpp`, tests | `LateCount::from_device(n, slot, slot.size())` throws `invalid_access` at construction, and `grep -n "std::function" API/vocabulary` is empty | C6, C10 (`Buffer` accessors) |
| C8 builder surface | GRAPHAPI-01, 02, 09, 22, 23; routing defect (api half) | `API/graph/builder_surface.hpp`, `API/detail/emit_*.hpp`, new `API/detail/callable_checks.hpp`, `API/launch/{extent,nd_range}.hpp`, new `API/vocabulary/{placement,coupled}.hpp`, `API/launch/README.md`, `tests/integration/test_layer_equivalence.cpp`, tests | `UntrackedOffsetLaunchesMatchAcrossLayers` contains no `GTEST_SKIP`, and a compile-fail probe of `add(state, wrong_fn)` reports the `static_assert` text | C3, C7, C10, C11 |
| C9 run driver | GRAPHAPI-06, 10, 23 (profiling names) | `API/graph/{step_driver,run_handle,graph,dynamic_graph}.hpp` (run entries only), tests | A host task that throws yields `Errc::kernel_failed` from `try_run` and from `try_wait`, on `Graph` and on `DynamicGraph`, and both classes expose the same sixteen run entries (checked by one `static_assert` list) | C7 (same two kept headers) |
| C13 umbrella, promotion, namespaces | API-10, API-11, GRAPHAPI-03, CORE-12 (umbrella part), CORE-13 (deprecation flip) | `API/../SushiRuntime.h`, `API/fluent.hpp`, the closing `using` block of every api leaf header, `modules/platform/memory/include/SushiRuntime/memory/{resource_region,resource_liveness}.hpp`, `modules/platform/observability/include/SushiRuntime/observability/*.hpp`, `task_graph.hpp` (attributes only), `tests/package/main.cpp` | A translation unit that includes only `SushiRuntime.h` compiles the quickstart with every type spelled `SushiRuntime::X`, and `grep -c "#include" SushiRuntime.h` is 2 | C8, C9, C10, C11, C12; runs alone |
| C14 examples and callers | API-13, API-14 | `applications/examples/*.cpp`, `applications/examples/README.md`, in-repo tests still on deprecated spellings | The syntax check of all units reports no deprecation warning, and one example handles a `try_run` error and reads a device-resident field back | C13 |
| Orchestrator | changelog, build lists | `docs/reference/CHANGELOG.md`, the api and logging `CMakeLists.txt`, `KNOWN_ISSUES.md`, `REMAINING_WORK.md` | One changelog line per row marked as a break in section 2, each naming the replacement | after each wave |

Waves, by disjoint file sets:

| Wave | Tasks | Then |
| --- | --- | --- |
| 1 | C1, C3, C5 in parallel; C2 after C1 | build and test (the first build of the split) |
| 2 | C6 alone | build and test (first build without the primitives) |
| 3 | C7, C10, C12 in parallel; C11 after C10 | build and test |
| 4 | C8, then C9 | build and test, benchmark suite |
| 5 | C13 alone | build and test, then the three downstream repositories configured against the tree as a smoke check |
| 6 | C14 | build, test, benchmark suite, package consumer |

C8 and C9 are serial in wave 4 only because C9 waits on nothing C8 produces but both are large; they have disjoint files and may share the wave if two workers are free.

Benchmark-relevant paths:

| Task | Path | Benchmark |
| --- | --- | --- |
| C3 | queue choice per dispatch (`task_scheduler.cpp:789-808`); chain classification | `scheduler_dispatch`, `chain_dispatch`, `wide_fanout`, `root_fanout` |
| C2 | one relaxed load per log macro; none on the dispatch path unless a line is logged | `scheduler_dispatch` as a guard |
| C5, C7 | plan build and region rebuild; the dangling check at each run entry | `test_graph_compile_perf`, `test_recompose_churn_perf`, `test_dynamic_graph_perf` |
| C9 | the step loop itself (`graph.hpp:2258-2307`) | `chain_dispatch`, `root_fanout`, `wide_fanout` |

Per PLAN.md:205-208 the machine's run-to-run spread reached 35 percent, so these runs can show a regression of that size and no smaller.

### 6. Questions for the owner

1. **The name `Graph`.** `SushiRuntime::Graph` is the engine namespace, so the builder cannot be promoted under that name. Recommend: keep the namespace, leave the builder at `SushiRuntime::API::Graph`, promote everything else. Alternative: rename the engine namespace (for example `SushiRuntime::Tasks`) so `SushiRuntime::Graph` is the builder; that rewrites 287 `SushiRuntime::Graph::` uses in SushiBLAS and about 10 in SushiAI with no forwarder possible.
2. **`NdRange` origin.** Recommend: `NdRange` gets `origin` and `at(origin)` like `Extent`, with a second kernel form `fn(sycl::nd_item<N>, sycl::id<N> global)` that receives the shifted id, and `Extent::offset` is renamed `origin`. Alternative: `NdRange` stays origin-less and the README says so; `Extent::offset` keeps its name.
3. **Where logging is set.** Recommend: process-wide free functions `set_log_level` / `set_log_sink` plus `SR_LOG_LEVEL`. Alternative: fields on `RuntimeConfig`, with the last created runtime winning.
4. **`RuntimeConfig` shape.** Recommend: flat fields with today's names plus `devices` (SushiEngine compiles unchanged). Alternative: nested `workers { count, first_core, pinning }`.
5. **Untracked default.** Recommend: an untracked kernel with no `On(...)` runs on `DeviceIndex{0}`. Alternative: make the placement mandatory and deprecate the two-argument `add_untracked`.
6. **SushiBLAS modules.** Recommend: `ordered_kernels` (kernels layer) and `ordered` (operations layer). Alternative: one module `ordered` in operations holding both halves.
7. **`Runtime` copy.** Recommend: delete it. Alternative: keep it and document that a copy is a second handle to one runtime.
8. **Device order.** Recommend: keep SYCL enumeration order and expose `advanced().devices()`. Alternative: sort GPUs first, which changes what `DeviceIndex{0}` and the untracked default mean on a machine with a CPU device listed first.

### 7. Not read, not verified

- `graph.hpp`: read lines 77-391, 716-965, 1800-1869, 2041-2360, 2436-2765 and 2956-3059, plus a grep outline of every declaration. Not read: the tracked, capture, State, Dynamic and offload overload bodies (392-715, 1705-1800), segmented reduce, prefix scan and counting sort (966-1703), the run entries (1870-2040), `emit_coupled` and `emit_captured` (2360-2435), the Dynamic and offload emitters (2766-2955).
- `dynamic_graph.hpp`: read 90-189, 664-713, 1118-1135 and the declaration outline. The composer (434-933) and the drivers (935-1070) were not read.
- `buffer.hpp`, `state.hpp`, `dynamic.hpp`, `dependencies.hpp`, `captures.hpp`, `run_handle.hpp`, `step_machinery.hpp`, `device_fault.hpp`, `callable_traits.hpp`: read with comment lines filtered out, so their Doxygen was not checked. `combine.hpp`, `extent.hpp`, `nd_range.hpp`, `region.hpp`: partly. `export.hpp`: my filter printed nothing for it; not read. `host_export.hpp`, `api/interop/interop.hpp`, `run_report_builder.hpp`, `fixed_order_scan.hpp`, every README in the api module: not read.
- `logger.hpp`: declaration outline and the two stream lines only. `result.hpp`: member list only. `runtime_context.hpp`, `runtime_settings.hpp`, `run_report.hpp`, `error.hpp`, `version.hpp`: declarations read, comments filtered.
- Examples: grepped for the calls API-13 and API-14 name; no example read in full.
- Tests: names only for `tests/numeric/`; `run_untracked_launches` in `test_layer_equivalence.cpp` not read, so "its buffers are on the default device" rests on the skip comment and the known-issues text.
- Audit: every `API-`, `GRAPHAPI-`, `CORE-` finding and EXEC-01 to 25 read. Findings marked "not re-checked" in section 1 were not compared with today's code. `SPEC.md` not read.
- Downstream: counts come from grep over source files; nothing was compiled. Which of SushiBLAS's 16 umbrella includers break (2.8), whether SushiEngine relies on clamped ranges (2.2), how `to_dynamic` builds a `Dynamic` (2.4), and which `Errc` values SushiAI compares (2.5) are open. `SushiBLASModule.cmake` was not read beyond one grep, so header-only module support is unverified.
- The `DynamicGraph` token observation in section 3 and every "would chain" statement in section 4 are read from code, not run.
- Nothing was measured. Every performance statement cites the benchmark that would settle it.

# Phase D: distributed layer, production ready for a trusted LAN

Read-only design against the working tree at `703eed9`. Paths are relative to `D:\Projects\sushiruntime`. `M/` stands for `modules/extension/distributed/`, `inc/` for `M/include/SushiRuntime/distributed/`, `src/` for `M/source/`. Nothing was built or run, so every behavioural claim comes from reading.

### 1. Findings table

Status: **closed** (today's code settles it), **changed** (the evidence moved or part is done), **stands**. Task ids are from section 7.

| Id | Status | Evidence in today's code | Settled by |
| --- | --- | --- | --- |
| DIST-01 SIGPIPE | stands | `src/tcp_transport.cpp:176` sends with flags 0; a failed write only shuts the socket (`:251`), so later writes hit it again | T3, T6, T14 |
| DIST-02 output allocation kills the worker | stands | `src/worker_agent.cpp:393` resolves outputs before the `try` at `:395`; throws at `:232-238`; `exec_loop` has no guard (`:483-497`) | T10 |
| DIST-03 open port, no peer gate | stands | `INADDR_ANY` at `src/tcp_transport.cpp:316`; `BUFFER_REQUEST` and `BUFFER_PUSH` served with no registered check (`src/distributed_coordinator.cpp:239-272`); fence compares a wire version (`:260`); a rejected HELLO only breaks (`:211-216`) | T2, T6, T8, T9 |
| DIST-04 stray connection takes a slot | stands | `src/tcp_transport.cpp:261-271` accepts exactly `worker_count` sockets, then returns | T6, T8, T14 |
| DIST-05 slow link reads as dead worker | stands | liveness refreshes after a whole frame (`src/distributed_coordinator.cpp:199-203`, `src/tcp_transport.cpp:225-229`); one send mutex per socket (`:245`); `registered` never clears (`src/worker_agent.cpp:474`) | T6, T9, T10, T14 |
| DIST-06 silent version mismatch | stands | `src/distributed_coordinator.cpp:211-216` logs and breaks without a reply; the worker re-sends HELLO every heartbeat (`src/worker_agent.cpp:474-475`) | T1, T8, T10 |
| DIST-07 worker never learns the master is gone | stands | `ITransport::send` returns `void` and there is no peer-state callback (`inc/i_transport.hpp:45-96`); `run()` only returns on `stop()` (`src/worker_agent.cpp:610-615`) | T5, T6, T10 |
| DIST-08 unused master `KernelRegistry` | changed | entry point is now `Cluster::attach` (`inc/cluster.hpp:70-73`), which still requires `kernels`; the coordinator stores it (`src/distributed_coordinator.cpp:370`) and never reads it | T5, T16 (Q8) |
| DIST-09 no worker entry point | stands | the only worker `main` is the demo (`applications/examples/distributed_demo.cpp:104`) | T12, T15 |
| DIST-10 manual registration, silent local run | changed | `Buffer::distributed()` shares and is a no-op with no coordinator (`modules/surface/api/include/SushiRuntime/api/data/buffer.hpp:404-429`). Still open: `prepare()` skips silently (`src/distributed_coordinator.cpp:499-500`, `:512-514`); `wait_for_workers` polls at 5 ms (`src/cluster.cpp:53`, `:178-188`) | T8, T11 (Q9) |
| DIST-11 kernels get pointers without sizes | stands | `inc/kernel_registry.hpp:58-62`; the worker drops the size at `src/worker_agent.cpp:372-383` | T5, T10 (Q8) |
| DIST-12 256 MiB cap | stands | `src/tcp_transport.cpp:241` casts the length to `uint32_t`; the receiver drops at `:218-223` | T1, T9 |
| DIST-13 four copies, two zero-fills | stands | `src/distributed_coordinator.cpp:246-248`; `inc/protocol.hpp:141`, `:302`, `:332`; `src/tcp_transport.cpp:225`; `src/worker_agent.cpp:341`, `:421-423` | T9, T10 (two copies remain, see section 9) |
| DIST-14 blocking send and INFO per task | stands | `src/distributed_coordinator.cpp:839` writes on the pool worker; `:841` logs at INFO per task | T6, T11 |
| DIST-15 struct memory on the wire, no magic | stands | `inc/protocol.hpp:235` writes `BufferHandle` raw, `:230` a struct; `FrameHeader` (`:105-109`) is unused; the frame is `[u32][u8]` (`src/tcp_transport.cpp:210-215`) | T1 |
| DIST-16 master readers unguarded | stands | the handler is installed with no `try` (`src/distributed_coordinator.cpp:567-572`); the worker has one (`src/worker_agent.cpp:578-593`) | T6 |
| DIST-17 errors without endpoint or cause, no hostnames | stands | `src/tcp_transport.cpp:322`, `:411`; `inet_pton` only (`:390-391`); `WSAStartup` result dropped (`:73`) | T3, T6 |
| DIST-18 unwritten `ITransport` contract | stands | `inc/i_transport.hpp:51`, `:63`; master is `-1` in TCP (`src/tcp_transport.cpp:60`) and the worker addresses it as `0` (`src/worker_agent.cpp:118`); `InProcessFabric::impl_` is public (`inc/in_process_transport.hpp:131-133`) | T5, T6 |
| DIST-19 four responsibilities per file | stands | one `Impl` holds membership, pending table, message handling and dispatch (`src/distributed_coordinator.cpp:89-173`); the worker likewise (`src/worker_agent.cpp:53-188`) | T7-T12 |
| DIST-20 inflight drift | stands | `src/distributed_coordinator.cpp:285-287` decrements for a result that was not pending | T7 |
| DIST-21 platform branches in the transport | stands | `src/tcp_transport.cpp:38`, `:68`, `:86`, `:102`, `:128`, `:332`; `SO_REUSEADDR` at `:311` | T3 |
| DIST-22 worker cache only grows | stands | entries leave only in `free_all()` (`src/worker_agent.cpp:502-508`); no release message in `MsgType` (`inc/protocol.hpp:87-98`) | T1, T9, T10 |
| DIST-23 API shape depends on the flag | closed for API shape | no header reads `SR_DISTRIBUTED` (`cmake/SushiRuntimeLibrary.cmake:73-76`; a grep of `modules/` finds none). Install rules for an OFF build were not checked | none |
| DIST-24 stale documentation | stands | `docs/guides/DISTRIBUTED_GUIDE.md:102`, `:166`, `:193`, `:223`, `:320`, `:337`; `M/README.md:59`, `:160`, `:326`, `:382` | T17 |
| DIST-25 transitive includes | stands | `inc/protocol.hpp:31-33` | T1, T5 (the header is deleted) |
| D1 pending nodes leak at stop | stands | `stop()` (`src/distributed_coordinator.cpp:584-599`) never touches `pending` (`:113`) | T7, T11 |
| D2 no stopped check; node touched after publish | stands | publish at `:821-822`, then `attempts` at `:836` and `metadata.name` at `:841` | T11 |
| D3 dynamic node bumps the snapshot list | stands | `settle` walks `io_writes_` (`:181-182`), not `live_writes` (`modules/engine/task/include/SushiRuntime/task/offload_state.hpp:80`) | T11 |
| D4 aborted run still offloads and bumps | stands | dispatch branch has no run-state check (`modules/engine/scheduler/source/task_scheduler.cpp:594-599`); a skipped node is pushed as ready (`:649-661`) and the poller reports it (`modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp:303-304`) | T4 |
| D5 handle vectors copied per dispatch | stands | `src/distributed_coordinator.cpp:797-798` | T11 |
| D6 hang with deadline 0 | stands | `reap_timed_out_tasks` returns at `:657-659` | T1, T7 (Q7) |
| D7 `noexcept` over a mutex and over `ITransport::stop()` | stands | `statistics()` (`:544`) locks through `:468`; `stop()` (`:584`) calls `:598`, declared without `noexcept` at `inc/i_transport.hpp:88` | T5, T8, T11 |
| D8 `shutdown()` skips the coordinator | stands | `modules/engine/execution/source/runtime_context.cpp:272-276` against the destructor at `:159-161` | T4 |
| D9 failed `start()` leaves a coordinator installed | stands | install at `src/cluster.cpp:159-164`, `start()` at `:165`, no cleanup | T11 |
| D10 offloaded count is cumulative | stands | `modules/surface/api/include/SushiRuntime/api/detail/run_report_builder.hpp:84-90` | T4 |
| D11 destroyed handle does not detach | stands, unruled | `inc/cluster.hpp:54-55` | Q4 |
| N1 (new) copy outside the registry lock | hypothesis from reading | lookup then `memcpy` at `src/distributed_coordinator.cpp:242-247`, `:257-270`, `:299-308`, while `on_release` unregisters (`:556-559`) | T9 |
| N2 (new) unsynchronised peer table in TCP | hypothesis from reading | accept thread writes `peers` and `readers` (`src/tcp_transport.cpp:268-269`); `send` and `stop` read them unlocked (`:432-438`, `:466-474`) | T6 |
| N3 (new) the attempt cap pins a node for good | hypothesis from reading | `off.offloadable = false` (`src/distributed_coordinator.cpp:761`), though `attempts` is documented per run (`offload_state.hpp:69`) | T11 |

### 2. Target shape of the module

Flat files, no subfolders under `source/` (I did not load `repository-layout`; rename if it says otherwise).

**Public, `inc/`**

| Header | Owns |
| --- | --- |
| `cluster.hpp` | `Cluster` handle and `ClusterOptions` |
| `cluster_statistics.hpp` | `ClusterStatistics`, `WorkerSnapshot` (plain data) |
| `shared_secret.hpp` | `SharedSecret`: bytes, `from_file`, `from_environment` |
| `endpoint.hpp` | `Endpoint{host, port}` and `parse("host:port")`, `"[v6]:port"` |
| `message_type.hpp` | `MsgType`, the only wire type a transport author needs |
| `buffer_handle.hpp` | `GlobalBufferID`, `BufferHandle` (returned by `Cluster::buffer_handle`) |
| `buffer_view.hpp` | `BufferView{data, size_bytes}`, `as<T>()` |
| `i_transport.hpp` | `ITransport`, `TransportEvents`, `FrameSource`, `PeerId`, `SendStatus`, `TransportStatus`, the written contract |
| `tcp_transport.hpp` | `TcpTransport::make_master(TcpMasterOptions)`, `make_worker(Endpoint, TcpWorkerOptions)` |
| `in_process_transport.hpp` | fabric and endpoints with the same factory shape; `impl_` private |
| `i_offload_policy.hpp` | unchanged apart from the contract text (called under the directory lock, must not call back) |
| `kernel_registry.hpp` | registry; gains the `BufferView` kernel form |
| `worker_agent.hpp` | `WorkerAgent`, for an application that embeds a worker |
| `worker_main.hpp` | `WorkerOptions`, `run_worker(...)` |
| `registered_task.hpp` | DIST-08 helper (T16, after Q8) |

Leaving `inc/`: `protocol.hpp` (deleted, replaced by the private codec), `buffer_registry.hpp` and `distributed_coordinator.hpp` (moved to `src/`). See Q2.

**Private, `src/`**

| File | Owns |
| --- | --- |
| `wire_codec.hpp` | little-endian field writer and reader with bounds and range checks |
| `wire_frame.hpp/.cpp` | the 12-byte frame header, magic, version, limits |
| `wire_messages.hpp/.cpp` | one struct, `encode` and `decode` per message |
| `message_router.hpp` | table `MsgType -> handler`, used by both roles |
| `sha256.hpp/.cpp` | SHA-256 |
| `hmac_sha256.hpp/.cpp` | HMAC over it, constant-time compare |
| `handshake.hpp/.cpp` | nonce, the two proofs, verification; pure functions |
| `shared_secret.cpp`, `endpoint.cpp` | the two public value types |
| `socket.hpp`, `socket_posix.cpp`, `socket_windows.cpp` | the one platform seam: listen, connect, accept, exact read, full write, shutdown, close, resolve, error text |
| `outbox.hpp/.cpp` | per-peer send queue: control frames ahead of bulk streams |
| `peer_connection.hpp/.cpp` | one TCP connection: its reader thread, writer thread and outbox |
| `tcp_transport.cpp` | acceptor or connector, the peer table |
| `in_process_transport.cpp` | fabric, fault policy, pump |
| `buffer_registry.hpp/.cpp` | ids, versions, and the only code that copies master buffer bytes (`read`, `write` under its lock) |
| `worker_directory.hpp/.cpp` | per-peer session state, `WorkerStats`, op support, liveness, alive count |
| `pending_tasks.hpp/.cpp` | the task table; owns the node references |
| `buffer_exchange.hpp/.cpp` | serves pulls as a `FrameSource`, applies fenced output chunks |
| `offload_dispatcher.hpp/.cpp` | `prepare` and `try_offload` |
| `cluster_monitor.hpp/.cpp` | the monitor thread: pings, liveness, deadlines, handshake deadlines |
| `distributed_coordinator.hpp/.cpp` | implements `Graph::OffloadCoordinator` and `Memory::UsmReleaseObserver` by composing the bricks; start and stop order |
| `cluster.cpp`, `offload_handles.hpp`, `i_offload_policy.cpp`, `kernel_registry.cpp` | as today |
| `worker_buffer_cache.hpp/.cpp` | worker cache: chunk assembly, byte budget, least-recently-used eviction, pins |
| `dispatch_deduplicator.hpp/.cpp` | seen task ids |
| `master_link.hpp/.cpp` | worker side of the handshake, heartbeat, master-gone detection |
| `dispatch_executor.hpp/.cpp` | job queue, exec thread, kernel run, result and output chunks |
| `worker_agent.cpp` | composes the four worker bricks |
| `stop_signal.hpp/.cpp` | SIGINT, SIGTERM (and SIGBREAK where defined) into one flag |
| `worker_main.cpp` | `run_worker`: flags, environment, signals, exit codes |

**What `Cluster` exposes.** `attach(runtime|context, transport, ClusterOptions)`, `detach()`, `wait_for_workers`, `alive_workers`, `liveness_timeout`, `task_deadline`, `supports`, `bump_buffer`, `buffer_handle`, and one `statistics()` returning `ClusterStatistics` (section 5). The four counter getters (`inc/cluster.hpp:111-120`) stay as forwarders. `ClusterOptions` carries `secret`, `policy`, `kernels` (optional), `liveness_timeout`, `task_deadline`, `handshake_timeout`, `chunk_bytes`. The runtime keeps seeing only `Graph::OffloadCoordinator`: `context.install_offload` still receives the aliasing `shared_ptr` built at `src/cluster.cpp:162-164`.

### 3. Protocol v2

**Frame.** 12 bytes, little-endian, then the payload.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `'S','R','D','P'` |
| 4 | 2 | `version` = 2 |
| 6 | 1 | `type` (`MsgType`) |
| 7 | 1 | `flags`: bit 0 = last chunk; other bits must be 0 |
| 8 | 4 | `payload_length`, at most `MAX_FRAME_PAYLOAD` = 1 MiB |

The magic read as a v1 length is 0x50445253 (about 1.35 GB), above v1's 256 MiB cap, so a v1 master drops a v2 worker at `src/tcp_transport.cpp:218-223` instead of misparsing it. There is no checksum; TCP's own is the only one on a LAN.

**Field encoding.** Fixed-width little-endian integers. `bool` is one byte, and any value other than 0 or 1 fails the decode. Enums are range-checked. Strings are `u16` length plus bytes, at most 255. Lists are a `u32` count checked against the remaining bytes before any reserve. `BufferHandle` is `id u64, version u32, size u64`: 20 bytes, no padding. `TaskMetadata` is `task_type u8, priority i32, numa_affinity i32, param_count u8, params u64[]`; `name` stays off the wire as today (`inc/protocol.hpp:228`).

**Messages.**

| # | Type | Direction | Payload |
| --- | --- | --- | --- |
| 1 | HELLO | W to M | `worker_nonce[32]`, `build_id` str, `node_name` str |
| 2 | CHALLENGE | M to W | `master_nonce[32]`, `master_proof[32]`, `session_id u64` |
| 3 | PROOF | W to M | `worker_proof[32]`, ops `u32[]`, `max_jobs u32`, `cache_budget u64` |
| 4 | WELCOME | M to W | `worker_id u32`, `liveness_timeout_ms u32`, `ping_interval_ms u32`, `chunk_bytes u32` |
| 5 | REJECT | either | `reason u8` (version, auth, malformed, busy), `sender_version u16`, text |
| 6 | GOODBYE | either | `reason u8` (shutdown, liveness, detach), text |
| 7 | TASK_DISPATCH | M to W | `task_id`, `op_id`, metadata, input handles, output handles |
| 8 | TASK_ACCEPTED | W to M | `task_id` |
| 9 | TASK_RESULT | W to M | `task_id`, `status u8`, output handles (bytes travelled before it as chunks) |
| 10 | BUFFER_REQUEST | W to M | `id`, `version` |
| 11 | BUFFER_CHUNK | either | `purpose u8` (pull reply, task output), `task_id u64`, handle (20), `offset u64`, then data to the end of the frame |
| 12 | BUFFER_ABORT | sender to receiver | `id`, `version`, `task_id`, `reason u8` (stale, gone) |
| 13 | BUFFER_RELEASE | M to W | `id` |
| 14 | PING | M to W | `token u64` |
| 15 | PONG | W to M | `token u64`, `free_mem u64` |
| 16 | HEARTBEAT | W to M | `queue_depth u32`, `oldest_held_task_id u64`, `cache_bytes u64` |

**Handshake and the shared secret.** No hash exists in the tree that could authenticate anything (the only hit is the op-id hash in `modules/engine/task/include/SushiRuntime/task/task_types.hpp`). Proposal (Q1): SHA-256 and HMAC-SHA256 written in the module, about 200 lines, pinned by the FIPS 180-4 and RFC 4231 vectors. That adds no dependency.

- Supply: a file of at least 32 random bytes, the same on both machines. Master: `ClusterOptions::secret = SharedSecret::from_file(path)`. Worker: `--secret-file` or `SR_CLUSTER_SECRET_FILE`. No flag takes the secret inline, because a process list would show it.
- On the wire: nonces and proofs, never the secret. `master_proof = HMAC(secret, "SRDP2 master" || worker_nonce || master_nonce)`, `worker_proof = HMAC(secret, "SRDP2 worker" || worker_nonce || master_nonce)`. The worker checks the master's proof before it sends PROOF, so authentication is mutual. The labels stop one side's proof being replayed as the other's.
- The session layer runs the handshake, above `ITransport`, so the in-process transport and its fault policy exercise the same code as TCP. An empty secret runs the same exchange with an empty key (one path). `Cluster::attach` refuses an empty secret when `transport->exposure()` is `Network`.
- Until WELCOME a peer is not a worker: `WorkerDirectory` drops every other message from it, and closes it after `handshake_timeout` (default 5 s). Over a lossy fabric the worker restarts with a fresh nonce if no WELCOME arrives within one heartbeat interval.
- Protects against: stray connections and port scans, a worker from another cluster, a host on the LAN without the file reading or overwriting buffers, a passive listener learning the secret, replay of a recorded handshake.
- Does not protect against: reading buffer bytes off the wire (no encryption, by ruling), an on-path host injecting or altering frames after the handshake (frames carry no MAC), connection floods beyond the cap on unauthenticated peers, offline guessing of a short secret from a recorded handshake (hence 32 random bytes).
- If the owner rejects in-tree SHA-256, only a compared token is possible: the secret travels in HELLO in the clear. That still stops stray and misconfigured peers, but any host that can capture one handshake can then join.

**Version negotiation.** None in v2: both sides must be equal. Every frame carries the version, and the receiver enforces it on every frame.

| Event | Master logs | Worker logs |
| --- | --- | --- |
| Good magic, other version | ERROR once per connection: `worker <ip:port> speaks protocol v3, this master v2; build both from one commit; closed`. Sends REJECT(version), closes | ERROR once: `master <host:port> speaks protocol v2, this worker v3; build both from one commit`. Exits 4, no reconnect |
| Bad magic | WARN once: `<ip:port> sent bytes that are not a SushiRuntime frame (first four 0x........); closed`. No reply | ERROR: `<host:port> is not a SushiRuntime master`. Exits 4 |
| v2 worker, v1 master | (v1 drops an oversized frame) | WARN: `master closed the connection during the handshake; protocol or secret mismatch is likely`, then the reconnect rules |

**Chunked transfer and flow control (DIST-05, DIST-12).**

- Chunk data is 256 KiB by default, sent in WELCOME, allowed range 4 KiB to 1 MiB minus 64. At 100 Mbit/s one chunk takes about 21 ms. Offsets and sizes are `u64`, so buffer size is bounded by memory, not by a frame.
- Chunks describe themselves (handle, offset, last flag); there is no begin frame. The receiver accepts offset 0 or the next expected offset; a gap or a duplicate discards the transfer, and the existing recovery (pull timeout, then a failed result, then redispatch) takes over.
- Sender: `BufferExchange` hands the transport a `FrameSource`. The peer's writer thread calls `next()` when it is ready for a bulk frame, so a buffer is never queued whole. `BufferRegistry::read(id, version, offset, out)` copies one chunk into the writer's reusable scratch under the registry lock and reports `Stale` or `Gone`, on which the sender emits BUFFER_ABORT. This closes N1.
- Interleaving: the outbox has a control queue and a list of streams. The writer drains all control frames, then sends one chunk, round-robin over streams. A heartbeat or dispatch therefore waits at most one chunk.
- Flow control is TCP's window plus that single writer: nothing accumulates on the sender because streams are pulled, and nothing on the receiver because the reader copies each chunk into the destination allocation before it reads the next frame. There are no application credits. The control queue is bounded (default 4096 frames); when it is full `send` returns `QueueFull` and `try_offload` declines. A write that makes no progress for the liveness timeout closes the peer. The alternative, per-transfer credits, is listed in section 9.
- Liveness counts any frame, so a transfer in progress refreshes it every chunk. The worker's pull timeout becomes an idle timeout that restarts on each chunk of that buffer. The master's task deadline measures from the later of dispatch time and the last chunk exchanged with that worker.
- Outputs: the worker sends BUFFER_CHUNK(task output) frames, then TASK_RESULT. The master writes each chunk in place through `BufferRegistry::write`, which accepts it only when the task is still pending and the chunk's version equals the version recorded at dispatch. Reaping a task bumps its output versions first, so late chunks fail the fence. Today's fence trusts the wire version (`src/distributed_coordinator.cpp:260`, `:301`). See Q6 for in-place against staged.

**Heartbeat and master-gone detection (DIST-07).** WELCOME carries the liveness timeout, so both sides use one number. The master pings every monitor pass. The worker treats the master as lost when the transport reports the connection closed or when no frame arrived for one liveness timeout (half-open link). It then drops its queued jobs, logs once and reconnects with backoff (Q10). When the master reaps a silent worker it calls `close_peer`, so the worker sees the close, reconnects and registers again; the stuck `registered` flag of DIST-05 has no equivalent, because registration belongs to a connection. `session_id` is random per coordinator: a worker that reconnects to a different session clears its cache, since buffer ids restart at 1 in each master process (`src/buffer_registry.cpp`, `next_id_++`).

**Lost dispatch or result without a deadline (D6).** The worker acknowledges each dispatch with TASK_ACCEPTED. The master reaps a task that is not acknowledged within one liveness timeout, whatever the deadline. HEARTBEAT carries the smallest task id the worker still holds; an acknowledged task below it with no result is reaped as a lost result. A deadline of 0 then means only "no bound on kernel run time" (Q7).

**Compatibility rule.** The 12-byte header and the first three fields of REJECT and GOODBYE are frozen for every future version, so any two versions can refuse each other in words. Everything else may change, and any change to a field bumps `version`. Master and worker must carry the same version; there is no cross-version operation. `M/README.md` gets a version table, and the golden-bytes test fails on any layout change until the constant and the table move together.

### 4. Threading and ownership

**Threads.**

| Thread | Runs |
| --- | --- |
| Pool worker | `try_offload`, `on_local_completion` |
| Compiling or user thread | `prepare`, `share`, `on_release`, `statistics`, `attach`, `detach`, `stop` |
| TCP acceptor (master) or connector (worker) | accept or resolve-and-connect; creates a `PeerConnection` |
| Peer reader, one per connection | reads frames into a reusable buffer, calls `TransportEvents`; on the master this is where `sink->complete_remote` and `resubmit` run |
| Peer writer, one per connection | drains the outbox |
| Monitor (master) | pings, liveness, deadlines, handshake deadlines; calls `sink->resubmit` |
| Exec, heartbeat (worker) | kernel execution; heartbeat and master silence |

Two threads per connection is a choice for two machines that hard-codes nothing: the peer table is a map keyed by `PeerId`, ids are never reused, and `peer_count()` leaves the interface. A reactor behind the same `ITransport` is the later step.

**Pending nodes.** `PendingTasks` owns the scheduling reference from the moment `insert` succeeds until one of `take(task_id)`, `take_for_worker`, `take_overdue` or `drain` returns the node. Whoever removes the entry owns the node and must pass it to the sink. No other code holds a `Node*` across a lock release. The entry records the worker and the output ids and versions, so remote settlement never reads the node's lists.

**`stop()`.** In order: (1) `PendingTasks::close()` sets `stopped` under its mutex, after which `insert` refuses; (2) the monitor is joined; (3) the transport stops, which joins every reader, so no handler is running; (4) `drain()` returns the remaining nodes and each goes to `sink->resubmit`. The seam allows sink calls until `stop()` returns (`modules/engine/task/include/SushiRuntime/task/offload_coordinator.hpp:124-131`), and `resubmit` re-queues through `submit` (`task_scheduler.cpp:1172-1178`), so an in-flight run finishes locally instead of hanging or leaking. `stop()` stays `noexcept`: `ITransport::stop()` becomes `noexcept`, and the resubmit loop sits in a plain `try`/`catch (...)` that logs and releases the node. There is no rethrow and no nested exception anywhere in the design.

**`try_offload` without blocking.**

1. Check the attempt cap, then `attempts++`. This is the last write to the node.
2. One registry lock: snapshot input and output handles. Fixed-buffer nodes read the baked vectors through spans (D5); a dynamic node resolves its live lists into a reused thread-local scratch vector.
3. Directory lock: return false if stopped; `policy->pick_worker`; allocate the task id.
4. Registry lock: bump output versions, read them back. Encode the frame.
5. Pending lock: refuse if stopped; insert `{node, worker, outputs, time}`. From here the node belongs to `PendingTasks` and this thread does not touch it (D2).
6. `transport->send(...)`, which copies the small frame into the outbox and returns. On any status but `Queued`, `take(task_id)`: if it returns the node, undo the inflight count and return false; if not, a reaper already owns it and the answer is true.

There is no socket call, no INFO line (a counter and one TRACE), and four short locks. A node that is not offloadable costs nothing new: the branch at `task_scheduler.cpp:594-599` is unchanged for it.

**Lock order.** Five mutexes: directory, pending, registry, one outbox per peer, the transport's peer table. None is held while calling another brick, the transport or the sink, so none nests and no order exists to violate. The one outward call under a lock is `policy->pick_worker` under the directory mutex, as today (`src/distributed_coordinator.cpp:811-812`), and the policy contract will forbid calling back. `statistics()` reads atomics only, including an atomic alive count the directory maintains (D7).

**Lifetime rule.** `Cluster::State` owns the registry, then the coordinator (`src/cluster.cpp:91-94`). The context co-owns the state through the attachment and releases it in its destructor after the engine has shut down (`runtime_context.cpp:159-174`). The coordinator holds the transport and is its only event sink; a transport is started once and belongs to one coordinator, and a second `start` returns an error status. Bricks are destroyed only after `stop()` has joined every thread that can reach them. A `Cluster` handle may outlive the runtime and then reads a stopped coordinator, as `ClusterHandleOutlivesRuntime` already pins (`M/tests/test_distributed_e2e.cpp:1059`). `RuntimeContext::shutdown()` stops the coordinator before the engine (D8). `attach` uninstalls and throws a fresh exception when `start()` returns a failed status (D9); `start()` returns a status instead of throwing, which is also what DIST-17 asks for.

### 5. Operability

**Worker entry point (DIST-09).** Kernels are C++ callables registered in the worker's own binary (`inc/kernel_registry.hpp:72`), so a generic shipped executable would have nothing to run. Proposal (Q5): a library function plus a thin `main`.

- `int Distributed::run_worker(int argc, char** argv, sushi_ptr<KernelRegistry> kernels)` and `int run_worker(const WorkerOptions&, sushi_ptr<KernelRegistry>)`.
- `applications/examples/distributed_worker.cpp`: registers the demo kernel and returns `run_worker(argc, argv, kernels)`. The demo keeps the master role only.

| Flag | Environment | Default |
| --- | --- | --- |
| `--master host:port` | `SR_MASTER` | required |
| `--secret-file path` | `SR_CLUSTER_SECRET_FILE` | none (refused by a master that has one) |
| `--name text` | `SR_WORKER_NAME` | hostname |
| `--max-jobs n` | `SR_WORKER_MAX_JOBS` | 1024 |
| `--cache-bytes n` | `SR_WORKER_CACHE_BYTES` | 0 = half of device memory |
| `--heartbeat-ms n` | `SR_WORKER_HEARTBEAT_MS` | 1000 |
| `--pull-idle-timeout-ms n` | `SR_WORKER_PULL_IDLE_TIMEOUT_MS` | 5000 |
| `--connect-timeout-s n` | `SR_WORKER_CONNECT_TIMEOUT_S` | 60; 0 waits forever |
| `--no-reconnect` | | reconnect on |
| `--log-level`, `--version`, `--help` | | |

A flag beats the environment, which beats the default. `--version` prints the protocol version and the build id.

Signals: the first SIGINT or SIGTERM (SIGBREAK on Windows) stops intake, fails queued jobs with status `shutting_down`, lets the running kernel finish, sends GOODBYE and exits 0. The master redispatches at once instead of waiting for liveness. A second signal exits 130 immediately. The handler only sets a flag; the main thread polls it every 100 ms.

| Exit code | Meaning |
| --- | --- |
| 0 | stopped by signal |
| 1 | bad flag, bad endpoint, unreadable or short secret file |
| 2 | master not reached within the connect timeout |
| 3 | rejected: authentication |
| 4 | rejected: protocol version, or the peer is not a master |
| 5 | master lost with `--no-reconnect` |
| 70 | internal error |

**Log lines (DIST-17).** Every line names the endpoint and the cause. Nothing logs per task or per chunk above TRACE.

| Failure | Master | Worker |
| --- | --- | --- |
| Bind fails | ERROR `cannot listen on <addr>:<port>: <os text>; another process holds the port, choose another or stop it` | |
| Listening | INFO `listening on <addr>:<port>, protocol v2, authentication on/off` | |
| Resolve or connect fails | | WARN on the first failure and every tenth attempt: `cannot reach master <host>:<port> (<resolved ip>): <os text>; attempt <n>, next in <ms>` |
| Connected, registered | INFO `worker <id> "<name>" <ip:port> registered, <n> kernels, build <id>` | INFO `registered with master <host:port> as worker <id>` |
| Wrong secret | WARN `<ip:port> failed authentication; closed` | ERROR `master <host:port> rejected the secret in <file>` |
| Version or magic | see section 3 | see section 3 |
| Handshake timeout | WARN `<ip:port> sent no valid handshake in <ms> ms; closed` | |
| Peer lost | WARN `worker <id> <ip:port> lost: <cause>; <n> tasks returned to the scheduler` | WARN `master <host:port> lost: <cause>; reconnecting` |
| Liveness | WARN `worker <id> silent for <ms> ms (limit <ms>); closed, <n> tasks returned` | WARN `no frame from master for <ms> ms; reconnecting` |
| Task deadline, lost dispatch, lost result | WARN, rate-limited to one per worker per second, with the task id and which of the three | |
| Malformed frame | WARN `<ip:port> sent a malformed <type> frame (<len> bytes): <field>; closed` | same wording about the master |
| Exception in a handler | ERROR with peer, type, length and `what()`; the peer is closed, the process lives (DIST-16) | same |
| Output allocation fails | | ERROR `task <id>: cannot allocate <n> bytes for buffer <id> (budget <n>, in use <n>)`; result status `output_alloc_failed` (DIST-02) |
| Nothing stamped at compile | WARN once per `prepare`: `<k> of <n> op-tagged nodes will run locally: <a> no live worker for the op, <b> buffer not shared (first: op <id>)` (DIST-10) | |
| Write to a dead socket | handled inside `Socket`: `MSG_NOSIGNAL` on Linux, `SO_NOSIGPIPE` on macOS, peer marked closed after the first failed write (DIST-01) | same |

**Counters.** `Cluster::statistics()` returns `ClusterStatistics`: cumulative `offloaded`, `declined`, `redispatched`, `stale_writebacks` (today's four), plus `handshake_rejected_auth`, `handshake_rejected_version`, `peers_lost`, `tasks_reaped_deadline`, `tasks_reaped_unacknowledged`, `tasks_reaped_lost_result`, `bytes_sent`, `bytes_received`, `chunks_sent`, `chunks_received`, `transfers_aborted`, `send_queue_full`, gauges `alive_workers` and `pending_tasks`, and one `WorkerSnapshot` per worker (`id`, `name`, `endpoint`, `inflight`, `rtt_micros`, `free_mem`, `queue_depth`, `cache_bytes`, `ms_since_last_frame`). The worker keeps `tasks_run`, `tasks_failed` by status, `cache_bytes`, `cache_evictions`, `reconnects`, and logs them at INFO on shutdown. `RunReport` reports the delta of `offloaded` over the run (D10). Exporting to a file or a socket is not designed here.

**Hostname resolution.** `getaddrinfo` with `AF_UNSPEC` inside `Socket`. Each connect attempt tries the returned addresses in order, and each reconnect cycle resolves again. The master's bind address defaults to `127.0.0.1`; `0.0.0.0`, `::` or a named interface must be given explicitly, and `attach` refuses a non-loopback bind with no secret. On Windows the listener sets `SO_EXCLUSIVEADDRUSE` instead of `SO_REUSEADDR` (`src/tcp_transport.cpp:311`), and the `WSAStartup` result is checked.

### 6. Tests

All new suites run in the fast lane. TCP tests bind port 0 on loopback and wait on conditions (a counter, an event), never on a sleep.

| File (new) | Test | Pins |
| --- | --- | --- |
| `test_wire_format.cpp` | `GoldenBytes_<Message>` (16) | exact bytes per message; fails on any layout change (DIST-15) |
| | `FrameHeaderGolden`, `MagicReadsAsOversizedV1Length` | header layout; the v1 interaction of section 3 |
| | `EveryTruncationFailsCleanly` | each prefix of each golden frame decodes to an error |
| | `RejectsOutOfRangeEnumAndBool`, `ListCountBoundedByRemainingBytes` | range checks |
| `test_handshake.cpp` | `Sha256Fips180Vectors`, `HmacRfc4231Vectors` | the two primitives |
| | `ProofsDifferByRole`, `WrongSecretFails`, `FreshNonceDefeatsReplay` | the exchange |
| `test_socket.cpp` | `WriteToClosedPeerReturnsError` | the process survives (DIST-01) |
| | `BindInUseNamesPortAndCause`, `LocalhostResolves`, `Ipv6LoopbackConnects` | DIST-17 |
| `test_transport_conformance.cpp`, parameterised over in-process and TCP | `DeliversInOrderPerPeer`, `PeerLostOnceAfterLastFrame`, `NoCallbackAfterStop`, `SendFromCallbackAllowed`, `SendAfterStopReturnsStopped`, `ControlFrameOvertakesStream` | the written contract (DIST-18) |
| `test_pending_tasks.cpp` | `InsertAfterCloseRefused`, `TakeIsExactlyOnce`, `DrainReturnsEveryNode` | D1, D2 |
| | `LateResultLeavesInflightAlone` | DIST-20 |
| | `UnacknowledgedTaskReapedWithDeadlineZero`, `AcknowledgedTaskBelowOldestHeldReaped` | D6 |
| `test_worker_directory.cpp` | `UnregisteredPeerMessagesDropped`, `HandshakeDeadlineClosesPeer` | DIST-03, DIST-04 |
| | `WaitForWorkersWakesOnRegistration` | DIST-10 (no poll) |
| `test_buffer_exchange.cpp` | `ChunkedRoundTrip` at 0, 1, chunk-1, chunk, chunk+1, 3 chunks+7 bytes | chunk boundaries |
| | `ForgedVersionFenced`, `GapDiscardsTransfer`, `ReleaseDuringStreamAborts` | DIST-03 fence, N1 |
| `test_worker_bricks.cpp` | `OutputAllocationFailureFailsTheTask` | DIST-02 |
| | `ReleaseEvicts`, `BudgetEvictsLeastRecentlyUsedUnpinned` | DIST-22 |
| | `KernelSeesBufferSizes`, `LegacyKernelSignatureStillRuns` | DIST-11 |
| `test_distributed_tcp_faults.cpp` with `tcp_fault_proxy.hpp` (a loopback proxy that can pass, stall, cut, throttle or truncate) | `StrayConnectionTakesNoSlot` | DIST-04 |
| | `GarbageBytesClosedMasterLives`, `V1FrameRejected`, `OversizedLengthClosesPeerNotProcess` | DIST-15, DIST-16 |
| | `WrongSecretRejected`, `VersionMismatchLoggedOncePerConnection` | DIST-03, DIST-06 |
| | `MasterKilledWorkerSurvivesAndReconnects` | DIST-01, DIST-07 |
| | `WorkerCutMidChunkTaskFinishesLocally` | partial transfer |
| | `StalledLinkReapedThenWorkerRejoins` | DIST-05 rejoin |
| | `ThrottledTransferKeepsWorkerAlive` (8 MiB at 4 MiB/s, liveness 500 ms) | DIST-05, scaled from 64 MiB at 100 Mbit/s against 5 s |
| | `ManyChunkBufferCrossesIntact` (1 MiB at `chunk_bytes` 4 KiB) | DIST-12 in the fast lane |
| | `DispatchReturnsWhilePeerStalled` (an unrelated local task completes while the proxy stalls) | DIST-14 |
| `modules/engine/scheduler/tests/test_offload_seam.cpp`, with a fake coordinator | `AbortedRunOffersNoNode`, `SkippedNodeNotReportedAsCompleted` | D4 |
| | `ShutdownStopsCoordinatorBeforeEngine` | D8 |

Added to `test_distributed_e2e.cpp`: `StopResubmitsPendingNodes` (D1), `TryOffloadAfterStopRunsLocally` (D2), `DynamicStateBumpsTheLiveBufferAfterSwap` (D3), `AttachStartFailureLeavesNothingInstalled` (D9), `RunReportOffloadedIsPerRun` (D10), `AttemptCapDoesNotOutliveTheRun` (N3), `RefusesEmptySecretOnNetworkExposure`. A heavy-lane test `BufferAbove256MiBCrosses` covers DIST-12 at real size.

Existing tests that change:

- `test_distributed.cpp:67-117` and `:322-393` (round trips and fuzz) are rewritten against the new codec; `BufferPushMsg` and the inline `TaskResultMsg` bytes no longer exist. `:120` moves to the new transport interface.
- Every e2e and jepsen test that builds `InProcessTransport(fabric, -1)` or calls the four-argument `attach` moves to the factories and `ClusterOptions`.
- `OffloadOverTcpLoopback` (`test_distributed_e2e.cpp:538-567`) uses `TcpMasterOptions`.
- `StaleWritebackIsFenced` (`:760`) and `LostBufferPushTimesOutInsteadOfHanging` (`:838`) filter on `BUFFER_CHUNK`.
- `RegistrationSurvivesMessageLoss` (`test_distributed_jepsen.cpp:274`) now covers a four-message handshake.
- `tests/benchmark/benchmark_distributed_offload.cpp` is ported and gains the TCP loopback sizes of DIST-13 (64 KiB, 4 MiB, 64 MiB).

### 7. Task list

Files in one wave are disjoint. "Outside" marks work beyond `M/`.

| Task | Acceptance criterion | Files (N new, C changed, D deleted) | Waits on | Wave |
| --- | --- | --- | --- | --- |
| T0 Owner rules on Q1-Q10 | answers recorded in the work folder | | | 0 |
| T1 Wire format v2 | `test_wire_format.cpp` passes, 16 golden messages | N `src/wire_codec.hpp`, `wire_frame.hpp/.cpp`, `wire_messages.hpp/.cpp`, `message_router.hpp`, `inc/message_type.hpp`, `inc/buffer_handle.hpp`, `tests/test_wire_format.cpp` | Q6, Q7 | 1 |
| T2 Authentication primitives | FIPS and RFC vectors pass | N `src/sha256.hpp/.cpp`, `hmac_sha256.hpp/.cpp`, `handshake.hpp/.cpp`, `shared_secret.cpp`, `inc/shared_secret.hpp`, `tests/test_handshake.cpp` | Q1 | 1 |
| T3 Socket seam | `test_socket.cpp` passes on Windows and Linux; no `_WIN32` outside the two socket sources | N `src/socket.hpp`, `socket_posix.cpp`, `socket_windows.cpp`, `endpoint.cpp`, `inc/endpoint.hpp`, `tests/test_socket.cpp` | | 1 |
| T4 Engine and surface fixes. **Outside**: D4 is the scheduler's dispatch and skip path, D8 is `RuntimeContext`, D10 is the report builder, and the seam text must say `try_offload` does not block on I/O | `test_offload_seam.cpp` passes; a non-offloadable node executes no new instruction at dispatch | C `modules/engine/scheduler/source/task_scheduler.cpp` (run-state check inside the `offloadable` branch; an `executed` flag on the events pushed at `:656-661` and `:667-672`), C `.../scheduler/event_polling.hpp` (report only executed nodes), C `modules/engine/execution/source/runtime_context.cpp`, C `modules/engine/task/include/SushiRuntime/task/offload_coordinator.hpp` (contract text), C `modules/surface/api/include/SushiRuntime/api/detail/run_report_builder.hpp` and the caller that captures the "before" counters, N `modules/engine/scheduler/tests/test_offload_seam.cpp` | | 1 |
| **Build and run all tests after wave 1.** The old distributed code is untouched and should stay green. | | | | |
| T5 Contract headers | each public header passes `-fsyntax-only` alone | C `inc/i_transport.hpp`, `tcp_transport.hpp`, `in_process_transport.hpp`, `cluster.hpp`, `kernel_registry.hpp`, `worker_agent.hpp`, `i_offload_policy.hpp`; N `inc/cluster_statistics.hpp`, `buffer_view.hpp`, `worker_main.hpp`; D `inc/protocol.hpp`; moved to `src/`: `buffer_registry.hpp`, `distributed_coordinator.hpp` | T0, T1-T3 | 2 |
| T6 Transports | conformance suite passes for both transports | N `src/outbox.hpp/.cpp`, `peer_connection.hpp/.cpp`, `tests/test_transport_conformance.cpp`; C `src/tcp_transport.cpp`, `in_process_transport.cpp` | T5 | 3 |
| T7 `PendingTasks` | `test_pending_tasks.cpp` passes | N `src/pending_tasks.hpp/.cpp`, test | T5 | 3 |
| T8 `WorkerDirectory` | `test_worker_directory.cpp` passes | N `src/worker_directory.hpp/.cpp`, test | T5 | 3 |
| T9 Registry and `BufferExchange` | `test_buffer_exchange.cpp` passes | C `src/buffer_registry.hpp/.cpp`; N `src/buffer_exchange.hpp/.cpp`, test | T5 | 3 |
| T10 Worker bricks | `test_worker_bricks.cpp` passes | N `src/worker_buffer_cache.hpp/.cpp`, `dispatch_deduplicator.hpp/.cpp`, `master_link.hpp/.cpp`, `dispatch_executor.hpp/.cpp`, test; C `src/kernel_registry.cpp` | T5 | 3 |
| T11 Master composition | the coordinator source holds no `switch (type)` and no mutex of its own; e2e passes after the wave | N `src/offload_dispatcher.hpp/.cpp`, `cluster_monitor.hpp/.cpp`; C `src/distributed_coordinator.hpp/.cpp`, `cluster.cpp`, `offload_handles.hpp` | T6-T9 | 4 |
| T12 Worker composition and entry | `run_worker` returns each exit code of section 5 in `test_worker_main.cpp` | C `src/worker_agent.cpp`; N `src/worker_main.cpp`, `stop_signal.hpp/.cpp`, `tests/test_worker_main.cpp` | T6, T10 | 4 |
| T13 Port and extend existing tests | the three suites compile against the new headers and carry the seven new e2e cases | C `tests/test_distributed.cpp`, `test_distributed_e2e.cpp`, `test_distributed_jepsen.cpp` | T5 | 4 |
| T14 TCP fault tests | the twelve tests of section 6 exist and are deterministic over 100 repeats | N `tests/tcp_fault_proxy.hpp`, `tests/test_distributed_tcp_faults.cpp` | T3, T5 | 4 |
| T15 Applications and benchmark. **Outside**: the demo and the benchmark call the API that changed | the demo worker `main` is under 20 lines | C `applications/examples/distributed_demo.cpp`; N `applications/examples/distributed_worker.cpp`; C `tests/benchmark/benchmark_distributed_offload.cpp` | T5 | 4 |
| **Build and run the distributed tests after wave 4.** This is the first build that can be green since T5: T5 changes `ITransport` under the old coordinator and agent, so waves 2 and 3 end with the build red. Workers in those waves have only `clang -fsyntax-only`. | | | | |
| T16 Registered-kernel fallback (DIST-08). **Outside** only if Q8 chooses the fluent form, which edits `modules/surface/api/.../graph/graph.hpp` | one e2e test adds an op with no lambda and passes with and without a worker | N `inc/registered_task.hpp`, `src/registered_task.cpp`; C `tests/test_distributed_e2e.cpp` | Q8, T11 | 5 |
| T17 Documentation (DIST-24). **Outside**: `docs/` | each line listed in DIST-24 is corrected; the protocol version table exists | C `docs/guides/DISTRIBUTED_GUIDE.md`, `M/README.md`, `M/tests/README.md`, `applications/examples/README.md`, `docs/backlog/README.md`, changelog | T11, T12 | 5 |
| **Build and run everything after wave 5.** | | | | |

For the orchestrator's `SOURCES` list in `M/CMakeLists.txt` (`:11-18` today), the new `.cpp` files are: `wire_frame`, `wire_messages`, `sha256`, `hmac_sha256`, `handshake`, `shared_secret`, `endpoint`, `socket_posix`, `socket_windows`, `outbox`, `peer_connection`, `pending_tasks`, `worker_directory`, `buffer_exchange`, `offload_dispatcher`, `cluster_monitor`, `worker_buffer_cache`, `dispatch_deduplicator`, `master_link`, `dispatch_executor`, `stop_signal`, `worker_main`, and `registered_task` in wave 5.

Other build-file work for the orchestrator:

- New test sources for `sushiruntime_add_module_tests` (`M/CMakeLists.txt:28-33`): `test_wire_format`, `test_handshake`, `test_socket`, `test_transport_conformance`, `test_pending_tasks`, `test_worker_directory`, `test_buffer_exchange`, `test_worker_bricks`, `test_worker_main`, `test_distributed_tcp_faults`.
- The brick tests include private headers, so the test target needs `M/source` on its include path.
- A scheduler test target for `test_offload_seam.cpp`.
- An example target for `distributed_worker.cpp` (`applications/examples/CMakeLists.txt:66-67`).
- The two socket sources each carry one whole-file platform guard unless the module macro can list sources per platform.

The working tree has uncommitted edits to `docs/guides/DISTRIBUTED_GUIDE.md` and eighteen other documents (`git status`); T17 must start from them.

### 8. Questions for the owner

| # | Question | Recommendation | Alternative |
| --- | --- | --- | --- |
| Q1 | How does the secret authenticate without a dependency? | SHA-256 and HMAC written in the module, mutual challenge-response, pinned by the published vectors | A token compared in HELLO: no hash code, but one captured handshake lets any host join |
| Q2 | Which headers stay public? | Remove `protocol.hpp`, make `buffer_registry.hpp` and `distributed_coordinator.hpp` private, break `ITransport` once (no third implementation exists in the tree) | Keep all three public with deprecation and carry both transport interfaces for a release |
| Q3 | Shape of `attach` and the default bind | `attach(runtime, transport, ClusterOptions)`; the four-argument form deprecated and forwarding with an empty secret; default bind `127.0.0.1`; an empty secret refused on a network-exposed transport | Secret always required, with an explicit "unauthenticated" switch for tests |
| Q4 | Should destroying a `Cluster` handle detach (D11)? | No. The handle is a copyable view and one test relies on it outliving the runtime. Mark `attach` `[[nodiscard]]` | Detach when the last copy dies, which makes a temporary `attach(...)` a silent detach |
| Q5 | Worker entry point | `run_worker` in the library plus a thin `main` per application | A shipped `sr_worker` that loads kernels from a shared library through an exported registration function: one binary for all, at the cost of a plug-in ABI |
| Q6 | Output writeback | In place, chunk by chunk, fenced by pending state and an exact version; a failed transfer leaves the buffer partly written until the node reruns | Stage in master memory and commit on TASK_RESULT: never torn, costs one output-sized allocation and copy |
| Q7 | Meaning of `task_deadline(0)` | "No bound on kernel run time"; lost dispatches and lost results are still reaped through the acknowledgement and the heartbeat | Reject 0 and require a finite deadline |
| Q8 | DIST-08 and DIST-11 API shape | `kernels` becomes optional in `ClusterOptions`; a free function in the module adds an op whose local fallback is the registered kernel, at `TaskGraph` level; kernels take `span<const BufferView>`, the pointer form kept as a deprecated adapter | A fluent `graph.add(op, ...)` with no lambda, which edits the surface module and needs a per-element adapter |
| Q9 | DIST-10: register buffers automatically? | No. Sharing stays explicit (`.distributed()`), since which bytes leave the machine is a data decision; add the WARN | Register any runtime-allocated buffer an op-tagged task touches |
| Q10 | Worker behaviour when the master is lost | Reconnect with backoff, forever, unless `--no-reconnect` | Exit 5 and leave restart to a service manager |

### 9. Not read / not verified

Not opened or only skimmed:

- `inc/buffer_registry.hpp`, `kernel_registry.hpp`, `i_offload_policy.hpp`, `in_process_transport.hpp`, `worker_agent.hpp`: declarations only, through a filter.
- `src/kernel_registry.cpp`; the second half of `src/buffer_registry.cpp`.
- `M/README.md`, `M/tests/README.md` and the guide: headings and the DIST-24 lines only.
- Test bodies: I read the test names, the e2e helpers (`test_distributed_e2e.cpp:40-186`) and the jepsen scenario setup.
- `tests/benchmark/benchmark_distributed_offload.cpp`, `test_helpers.hpp`, `modules/memory` (`usm_release_observer.hpp`, the allocator), `compiled_graph.hpp`, `step_machinery.hpp`, the scheduler README, `SECURITY.md`.
- The optional skills (`repository-layout`, `api-stability`, `file-formats`, `platform-portability`, `testing`, `logging`, `cpp-code-style`, `multi-agent-work`): none loaded. File names, log levels and the status type should be checked against them.
- `distributed_demo.cpp` was read through a filter that dropped line numbers, so only `:104` and `:127` are cited.

Guesses until run or measured:

- N1, N2, N3 are read from code. N1 also depends on whether the allocator calls `on_release` before it frees, which I did not check.
- The chunk size (256 KiB), the control queue bound (4096) and the choice of no application credits. The benchmark of DIST-13 and a throttled run should confirm that TCP backpressure with a pulled stream is enough; per-transfer credits are the fallback.
- DIST-13 goes from four copies and two zero-fills to two cache-sized copies per direction (buffer to scratch, frame buffer to destination). True zero-copy needs pinning in the registry and a receive-into-destination hook in `ITransport`; it is left out.
- `on_local_completion` takes the registry mutex for every local node (`event_polling.hpp:303-304`, `src/distributed_coordinator.cpp:181-182`). Chunk copies now hold that mutex for one 256 KiB `memcpy`. Whether that delays local completions measurably is unknown.
- `std::random_device` as the nonce source. If it is not backed by the OS generator on this toolchain, the nonce needs `getrandom` and `BCryptGenRandom` behind the socket seam, and the second adds a link library.
- Whether module tests can include `src/` headers, whether the scheduler module has a test target, and whether `core` already has a status type for `TransportStatus`.
- That a bare `catch (...)` without rethrow is safe on the Windows toolchain. The design uses no rethrow and no nested exception, but I did not test the catch itself.
- Where the "before" snapshot for D10 is captured in the surface module; I read only `run_report_builder.hpp:70-93`.
- DIST-23: I checked the API shape, not what an OFF build installs.
- Whether a graph reset clears `offloadable` after the attempt cap (N3).

### Rulings on the phase D questions

Given by the owner on 2026-10-10.

| Question | Ruling |
| --- | --- |
| Q1, authentication | SHA-256 and HMAC written in the module; mutual challenge-response |
| Q3, `attach` | `attach(runtime, transport, ClusterOptions)`; the four-argument form deprecated; default bind `127.0.0.1`; an empty secret refused on a network-exposed transport |
| Q5, worker entry point | `run_worker` in the library plus a thin `main` per application. Kernels are C++ callables compiled into the worker's binary, so a single shipped executable would need a plug-in ABI that has to stay stable for no gain at two machines |
| Q6, output writeback | In place, chunk by chunk, fenced by pending state and an exact version |
| Q2, public headers | `protocol.hpp` is deleted for a private codec, `buffer_registry.hpp` and `distributed_coordinator.hpp` become private, `ITransport` breaks once |
| Q4, handle lifetime | Destroying a `Cluster` handle does not detach; `detach()` is explicit and `attach` is `[[nodiscard]]`. The handle stays a copyable view, so a caller that keeps nothing still has a cluster |
| Q8, kernel API | `kernels` is optional in `ClusterOptions`; a free function in the distributed module adds an op whose local fallback is the registered kernel; kernels take `span<const BufferView>` and the pointer form stays as a deprecated adapter. The api module does not learn about the distributed layer |
| Q10, master lost | The worker reconnects with backoff, forever, unless `--no-reconnect` |
| Q7, `task_deadline(0)` | Decided by the orchestrator as recommended: no bound on kernel run time; lost dispatches and results are still reaped |
| Q9, automatic sharing | Decided by the orchestrator as recommended: sharing stays explicit, with one WARN per `prepare` that leaves op-tagged nodes local |

The owner answered Q1, Q2, Q3, Q6 and Q10 and then handed the rest to the orchestrator, to be
decided for performance and for operability later. Q4, Q5 and Q8 above are the orchestrator's,
and each follows the design's recommendation.

### Rulings on the phase C questions

Given by the owner on 2026-10-10, numbered as in section 6 of phase C.

| Question | Ruling |
| --- | --- |
| 2, `NdRange` origin | `NdRange` gets `origin` and `at(origin)` like `Extent`, with the kernel form that receives the shifted id; `Extent::offset` is renamed `origin` with the old name deprecated |
| 3, logging | Process-wide `set_log_level` and `set_log_sink`, plus `SR_LOG_LEVEL` |
| 5, untracked default | `DeviceIndex{0}` when no `On(...)` is given |
| 7, `Runtime` copy | Deleted; `Runtime` is move-only |
| 1, the name `Graph` | Decided by the orchestrator as recommended: the engine namespace keeps the name, the builder stays `SushiRuntime::API::Graph`; the alternative rewrites 287 uses in SushiBLAS with no forwarder |
| 4, `RuntimeConfig` shape | Decided by the orchestrator as recommended: flat fields with today's names plus `devices` |
| 8, device order | Decided by the orchestrator as recommended: SYCL enumeration order, exposed through `advanced().devices()` |
| 6, SushiBLAS modules | Open until the SushiBLAS part starts |

One change to the task lists: D10 (the per-run offloaded count) leaves phase D's T4 for phase C's C9, because its caller is the step driver that C5 moves.

### Found while phase D's first wave ran

- A late-bound node that is disabled for the step but offloadable is still offered to
  `try_offload`: the `enabled` check lives only in `execute_task`. T11 adds it to the branch
  that is taken only for an offloadable node.
- A node whose work throws is pushed as executed and reported to `on_local_completion`, so
  versions are bumped for a node that failed part-way. T11.
- `HEARTBEAT.oldest_held_task_id` has no defined value for an idle worker; with 0 the master
  never reaps a lost result from one. T7 and T10 fix the value: the worker sends the largest
  task id it has finished plus one when it holds none.
- `Core::Errc` has no code for an unreadable file or an invalid argument; `SharedSecret` uses
  `invalid_access`. Aligned with what phase C's C1 adds.
- Reading an environment variable needs one seam: `std::getenv` trips the deprecation warning
  on this toolchain, and `shared_secret.cpp` and the worker entry point both need it. T12.
- On Windows it is not known whether a `select` in one thread wakes when another thread shuts
  the socket down; `Unit_SocketTest.ShutdownEndsAWaitingRead` decides it at the first run.


`Unit_SocketTest.ShutdownEndsAWaitingRead` passed at the first run in 0.03 s: on Windows a waiting
read does wake when another thread shuts the socket down.

### Decisions of the contract headers (T5)

The headers under `modules/extension/distributed/include/SushiRuntime/distributed/` and the brick
headers under `source/` are the contract of phase D from here on; where they and the design above
differ, the headers win. What they decided beyond the design:

1. The message type enum is `MessageType`, in `Distributed`; `Wire::MessageType` still resolves.
2. `PeerId` is a struct naming one connection: numbered from 1, never reused, 0 is no peer. A
   reconnect is a new id on both sides, and a worker learns the master's id from
   `on_peer_connected`. Registration belongs to a connection.
3. A worker's id is its `PeerId::value`; `WorkerStats::worker_id`, `WELCOME.worker_id` and
   `WorkerSnapshot::id` carry the same number.
4. `start(TransportEvents&)` takes the receiver and returns `TransportResult{status, message}`.
5. A master binds inside `start()`; `local_endpoint()` is valid only after `attach`.
6. A fourth event, `on_connect_abandoned(cause)`: a worker transport calls it once when it stops
   trying, so `run_worker` can tell exit 2 from exit 5.
7. `stop()` reports no `on_peer_lost`; the coordinator drains `PendingTasks` after the transport
   stops.
8. `FrameSource` yields whole typed frames. A control frame can overtake a stream, so the worker's
   output stream ends with `TASK_RESULT` and the master's pull stream ends with `BUFFER_ABORT` when
   the registry reports Stale or Gone.
9. `source/task_resubmission.hpp`, `resubmit_tasks()`: the one sequence the monitor, the peer-lost
   handler and `stop()` share (bump outputs, forget transfer progress, lower inflight,
   `sink.resubmit`).
10. A lost result needs two heartbeats: `note_heartbeat` counts misses and `take_overdue` reaps at
    `K_LOST_RESULT_HEARTBEATS = 2`, because a heartbeat can overtake the stream that carries a
    result.
11. Task ids come from `PendingTasks::allocate_task_id()`.
12. The output fence has two halves: `PendingTasks::expects_output()`, then
    `BufferRegistry::write()` at the exact version. Whoever removes a task bumps its outputs before
    resubmitting.
13. `BufferRegistry` returns no address: `read` and `write` copy under its lock, and
    `unregister_buffer` returns the id so the coordinator can send `BUFFER_RELEASE`.
14. `WorkerDirectory` owns the handshake state and verifies proofs; it returns decisions and the
    coordinator's handlers send the frames. `admits(peer, type)` drops frames from unregistered
    peers.
15. `pick()` offers the policy only workers below their `max_jobs`; the wire has no "queue full"
    status.
16. Each brick keeps the counters it causes behind a `statistics()` over atomics.
17. `alive_workers()` returns `std::size_t` and `wait_for_workers` takes one.
18. `attach(runtime, transport, {})` is ambiguous while the deprecated overloads exist; pass an
    explicit `ClusterOptions` or nothing.
19. `ClusterOptions::kernels` only keeps a registry alive; `add_registered_task` takes the registry
    as an argument.
20. `WorkerOptions` is flat, one field per flag; `WorkerAgentOptions` is separate.
21. `WorkerExitCode` includes `SecondSignal = 130`; `StopSignal::request()` lets a test reach exit
    code 0 without raising a signal.
22. TCP options: `max_peers` 64, `write_idle_timeout` 5 s, `attempt_timeout`, `backoff_initial`,
    `backoff_ceiling`, `close_linger` 1 s. `ClusterMonitorOptions::period` is 250 ms and is the
    ping interval in `WELCOME`.
23. The fault policy takes a `FaultedFrame` (direction, worker index, type, flags, payload); the
    worker index is the fabric's, assigned in `make_worker` order and stable across reconnects.
24. `PeerConnection` holds its `Outbox` as a member, the one place a brick contains another.
25. `WorkerBufferCache` takes a `CacheAllocator` (two callables), so its tests need no SYCL device.

Two sources beyond the design's list: `environment.cpp` and `task_resubmission.cpp`.

What each later task must know that its headers do not say:

- **T6 transports** (`outbox.hpp`, `peer_connection.hpp`, `tcp_transport.hpp`,
  `in_process_transport.hpp`): `Outbox::pop` releases its mutex around `FrameSource::next()` and
  around destroying a source. On a version mismatch the reader queues `REJECT(version)`, closes the
  outbox, then reports the loss. A connection above `max_peers` gets `REJECT(busy)` and no event.
  `on_peer_lost` runs on the reader thread, so finished connections are reaped from the accept or
  connect thread. `exposure()` decides loopback itself (`127.0.0.0/8`, `::1`, `localhost`). The
  connector logs the "cannot reach master" WARN; no event carries it.
- **T7 `PendingTasks`**: `take(task_id, worker)` returns nothing for a foreign worker;
  `note_heartbeat` clears the miss count of every task at or above the oldest held id;
  `take_for_worker` also forgets the worker's chunk time and heartbeat state; `PendingTask::cause`
  is meaningful only out of `take_overdue`.
- **T8 `WorkerDirectory`**: the wire carries nonces as `std::array<std::byte, 32>` and
  `handshake.hpp` uses `HandshakeNonce` over `std::uint8_t`; a second `HELLO` from an unregistered
  peer restarts its handshake; `take_silent` marks a worker not alive and keeps the entry until
  `remove_peer`.
- **T9 registry and exchange**: the pull source writes the 37-byte chunk prefix, then resizes the
  payload and lets `read()` copy behind it, so one copy is made. Every transfer ends with a
  last-chunk frame; a 0-byte buffer is one empty chunk. `apply_output_chunk` assumes the caller
  checked `expects_output`.
- **T10 worker bricks**: a task is held until its `TASK_RESULT` frame has been pulled by the writer
  or queued; the idle value of `oldest_held_task_id` is the largest handed-over id plus one. The
  output stream's source holds the cache pins and unpins in its destructor, which may run on a
  transport thread. On `QueueFull` the worker sends nothing and the master reaps the task as
  unacknowledged. `MasterLink`, once ended, closes every new connection without `HELLO`. The
  deprecated `register_kernel` wraps the pointer kernel in a `Kernel`.
- **T11 master composition**: the coordinator takes `(BufferRegistry&, transport, ClusterOptions)`
  and keeps the secret alive for the directory's span. `on_release` is `noexcept`, sends
  `BUFFER_RELEASE` and needs a plain catch around the encode. At the attempt cap the dispatcher
  declines without clearing `offloadable`; `complete_local` and `complete_remote` reset `attempts`.
  The dispatch branch also declines a late-bound node disabled for the step, and a node whose work
  threw is not settled as written.
- **T12 worker composition and entry**: `environment.cpp` is the one reader of an environment
  variable (`shared_secret.cpp` moves onto it). `run_worker` builds its own runtime.
  `WorkerAgent::wait_for_end` is what the 100 ms poll calls.
- **T13 existing tests**: the tree compiles with `-Werror`, so a test that calls a deprecated form
  on purpose silences the warning locally. `InProcessFabric` takes no worker count; the
  `WorkerAgent` setters became `WorkerAgentOptions`; a fake worker implements `TransportEvents`;
  `send` and `start` are `[[nodiscard]]`.
- **T14 TCP fault tests**: the port is known only after `attach`; `write_idle_timeout` is a
  transport option, separate from the cluster's liveness.
- **T15 applications**: `distributed_demo.cpp` and `benchmark_distributed_offload.cpp` use the old
  factories, the old `WorkerAgent` and the pointer kernel.

### What the bricks of wave 3 decided (T6 to T10)

Written by the workers that implemented the bricks; the composition tasks (T11, T12), the test
tasks (T13, T14) and the application task (T15) start from here. A brick's private section is
the implementer's: storage is held directly as members, public declarations are fixed.

**Transports (T6)**

- The transports log neither a bind failure nor "listening on": `start()` returns the bind
  message in `TransportResult::message`, and the coordinator logs it with the authentication
  state. They do log a handler exception (ERROR), the connector's "cannot reach master" (WARN,
  first and every tenth), accept failures and busy refusals (WARN, first and every tenth),
  thread-start failures (ERROR), accept and connect (DEBUG).
- `stop()` drops queued control frames. To get a `GOODBYE` out, call `close_peer()` and wait for
  `on_peer_lost(ClosedLocally)` before `stop()`.
- The TCP worker transport keeps reconnecting after `VersionMismatch` or `BadMagic`; the agent
  calls `stop()` to exit with code 4. A connection that ended sooner than `backoff_ceiling`
  keeps its backoff.
- `PeerLoss::text` holds `first four 0x........` for `BadMagic`, the offending field for
  `MalformedFrame`, `what()` for `HandlerFailed`, `describe(result)` for `IoFailed`.
- `on_connect_abandoned` runs after that peer's `on_peer_lost` has returned.
- In-process: frames, close notices and connection attempts are discarded while a link is cut;
  streams are pulled only while the receiver's inbox holds fewer than 16 undelivered frames of
  that link; the worker polls for a master every `reconnect_delay` (1 ms by default).
- `REJECT(busy)` and `REJECT(version)` are best effort: the socket is shut down right after the
  write, so a test asserts on slots and events, not on receiving the frame. A refused connection
  gets no `PeerId` and makes no event. The acceptor polls `accept` every 100 ms, so a master's
  `stop()` takes up to 100 ms. A stall is reported as `WriteStalled` only when the peer keeps the
  connection open.
- `TcpTransport::statistics()` takes the peer-table mutex briefly; the header's "takes no lock"
  holds for the in-process transport only.
- The conformance suite is a typed test, so its names start with `Unit_` and the label filter
  matches them.

**`PendingTasks` and `WorkerDirectory` (T7, T8)**

- `HelloReply::master_proof` is a `Sha256Digest` over `std::uint8_t`; the wire's
  `ChallengeMessage::master_proof` is `std::array<std::byte, 32>`. The handler converts.
- `accept_hello` keeps `node_name` and drops `build_id`; the "registered" log line takes the
  build id from the handler's own copy of `HELLO`.
- The handshake deadline runs from `add_peer`; a repeated `HELLO` does not restart it. After
  `AuthenticationFailed` the peer owes a new `HELLO`.
- `accept_proof` registers a peer even after `close()`; only `pick` and the waiters look at the
  closed flag.
- `take_silent` and both overdue tests use strictly greater than. `note_frame` never brings a
  taken worker back; `admits` still passes its frames until `remove_peer`, so a late result
  reaches `PendingTasks::take` and finds nothing.
- `pick` does not call the policy when no worker has room, and a policy answer that names a
  worker it was not offered counts as no pick. A worker that sends `max_jobs` 0 is never offered.
- `take_overdue` picks the cause in this order: Unacknowledged, LostResult, Deadline. The order
  of tasks out of `take_for_worker`, `take_overdue` and `drain` is unspecified. `drain` works
  whether or not `close()` was called.
- `wait_for_workers` clamps the timeout to one year.

**Registry and exchange (T9)**

- Chunk prefix, 37 bytes, exactly what `BufferChunkMessage::encode` writes with empty data:
  `purpose u8`, `task_id u64`, `handle.id u64`, `handle.version u32`, `handle.size_bytes u64`,
  `offset u64`. Data follows to the end of the payload.
- Pull stream: `BUFFER_CHUNK` frames in rising offset, purpose `PullReply`, `task_id` 0, the
  handle carrying the requested version and the buffer's whole size; the last-chunk flag on the
  final chunk; a 0-byte buffer is one empty flagged chunk. On `Stale` or `Gone` the final frame
  is `BUFFER_ABORT`, and chunks already sent are not followed by a last-chunk frame.
- Output chunks: the worker flags the chunk that ends at `handle.size_bytes` and no other, and
  every chunk carries the whole buffer size and the dispatched version. Offset 0 always restarts
  a transfer. Any refusal discards the transfer's progress. `finish_task` returns true only when
  every output handle has a transfer that ended with a flagged chunk at its size; for a task
  with no outputs it returns true.
- `register_buffer` on an address already shared returns the handle unchanged; the old code
  raised the version, and one old test expects the raise.
- The registry lock is held for one hash lookup and one `memcpy` of at most `chunk_bytes`; it is
  shared with `bump`, `resolve` in `try_offload`, registration and every other peer's chunk.

**Worker bricks (T10)**

- The executor sends `TASK_ACCEPTED` itself, inside `enqueue()` when it returns `Queued`; the
  agent does not. A task that fails at once can put its `TASK_RESULT` on the wire before its
  `TASK_ACCEPTED`.
- The agent calls `link.note_frame(peer)` for every frame before routing it, and routes
  `BUFFER_CHUNK` with purpose `PullReply` to `accept_chunk(chunk, last flag)`, `BUFFER_ABORT` to
  `abort_transfer`, `BUFFER_RELEASE` to `release`.
- `LinkChange`: `RegisteredNewSession` clears the cache and the deduplicator (the first
  registration returns it too); `Disconnected` calls `executor.discard_queued()`, which also
  resets the held-task ledger; `Ended` reads `link.end()`.
- Dispatch path: `if (dedup.mark_seen(id)) executor.enqueue(peer, std::move(dispatch),
  link.chunk_bytes())`; on `QueueFull` nothing is sent.
- Stop order: `executor.stop()`, `link.say_goodbye()`, `transport.stop()`, `link.stop()`, then
  the cache is destroyed. The cache outlives `transport.stop()`, where kept streams are destroyed
  and unpin. `executor.stop()` can block for up to `pull_idle_timeout`; `cache.clear()` wakes it.
- The link has no endpoint, so the agent logs `host:port` in `on_peer_connected` and the
  transport's cause in `on_connect_abandoned`, and the worker's statistics at INFO on shutdown.
- A throwing `free_memory_probe` escapes `on_ping` to the transport, which closes the peer.
- The link keeps the last four nonces of a connection, so a late `CHALLENGE` for an older one is
  ignored. Inputs are pulled one at a time, in dispatch order.
- The logger has no level below DEBUG, so per-task lines are DEBUG.

**Left for the composition tasks to fold in**

- The try/catch wrapper around the three transport events is written twice
  (`peer_connection.cpp`, `in_process_transport.cpp`), and `send_message` (encode, then
  `transport.send`) twice (`master_link.cpp`, `dispatch_executor.cpp`). Each belongs in one
  private header.
- `Wire::TaskResultStatus` has no value for a full queue.
- `FrameSource::next()` is `noexcept` and its first `resize` allocates.

### What the composition decided (T11, T12)

Written from the reports of the two composition tasks, committed as `9582ee6`. The test tasks
(T13, T14), the application task (T15) and the follow-up task (T18) start from here.

**Master (T11)**

- `DistributedCoordinator(registry, transport, ClusterOptions)`; `start()` returns a
  `TransportResult` and fails with "bind() must run before start()" when no sink is bound.
- Every way a peer leaves (`on_peer_lost`, `GOODBYE`, a failed proof, a malformed or unexpected
  frame) goes through `Impl::retire_peer`, and `WorkerDirectory::remove_peer` makes it idempotent.
  A task leaves `PendingTasks` through one of `take`, `take_for_worker`, `take_overdue`, `drain`,
  under the table's mutex, so a node reaches the sink once.
- `attach` throws `Errc::invalid_state` with one of: `Cluster::attach: the transport can be
  reached from other machines and ClusterOptions::secret is empty; ...`, `... chunk_bytes is N; it
  must be between 4096 and 1048512.`, `Cluster::attach: the master did not start: <transport
  message>; nothing stays installed on the runtime.` After a failed `attach`, `context.offload()`
  is null and the release observer is cleared.
- A node that stays local and writes a shared buffer has an `OffloadState` with
  `offloadable == false` and its shared output handles; a node that writes none has no state.
- A failed `TASK_RESULT`, or a successful one whose outputs did not arrive whole, is resubmitted
  and counts in `redispatched`. `stale_writebacks` counts chunks the registry refused plus chunks
  `expects_output` refused. A refused send counts an attempt and leaves the output version bumped.
- A worker's `GOODBYE` returns its tasks at once. `stop()` sends no `GOODBYE`; a worker sees the
  close and reconnects. `stop()` logs `distributed master stopped; K tasks returned to the
  scheduler` at INFO.
- `ClusterMonitor::period()` is the smaller of 250 ms and a quarter of the tightest timeout
  (liveness, or the task deadline when it is not 0), at least 1 ms. The monitor queues
  `GOODBYE(liveness)` before `close_peer`.
- `handshake_rejected_version` rises on `on_peer_lost(VersionMismatch)`,
  `handshake_rejected_auth` on a failed proof, after which the master sends `REJECT` and closes.
- `Cluster::statistics()` calls `ITransport::statistics()`, which takes a mutex in TCP; the four
  counter getters and `alive_workers()` read atomics only.
- Log lines: `N of M op-tagged nodes will run locally: A no live worker for the op, B buffer not
  shared (first: op ID)` (WARN); `worker N "name" endpoint registered, K kernels` (INFO); `worker
  N "name" endpoint lost: <cause>; K tasks returned to the scheduler` (WARN); `<endpoint> (peer N)
  closed before it registered: failed authentication` (WARN); `peer N sent no valid handshake in X
  ms; closed` (WARN); `task T taken back from worker N: <cause>; it is resubmitted to the
  scheduler` (WARN, one per worker per second). A test asserts on counters and events, not on the
  text of a line.
- `try_offload` allocates nothing in the dispatcher after warm-up; `PendingTasks::insert`
  allocates a map node and a copy of the outputs, and `ITransport::send` copies the payload. Phase
  E measures them.

**Worker (T12)**

- `run_worker(argc, argv, kernels)` parses and calls `run_worker(options, kernels)`, which sets
  the log level, reads the secret, installs `StopSignal`, builds a `RuntimeContext` and a TCP
  worker transport and calls `serve_worker()` of `source/worker_session.hpp`. `serve_worker(context,
  transport, kernels, WorkerAgentOptions, const StopSignal&)` runs one agent on any worker
  transport and returns the `WorkerExitCode`; a test drives it with the in-process fabric and
  `StopSignal::request()`.
- Usage errors go to stderr with the prefix `sushiruntime worker: `; `--help` and `--version` go
  to stdout. A value follows its flag as the next argument or after `=`. `--master` with port 0 is
  refused. The second overload validates the log level name and the secret file itself.
- The default name is `host_name()` of the socket seam, else `worker`.
- An agent that is stopping ignores a loss and an abandon, so `wait_for_end` returns `Stopped`
  after `stop()`. `stop()` closes the registered peer and waits up to 2 s for its `on_peer_lost`,
  which is what gets the last results and `GOODBYE` onto the wire.
- A `TASK_DISPATCH` from a connection the link is not registered on is dropped. A null transport
  or registry makes the `WorkerAgent` constructor throw `invalid_access`.
- `WorkerAgentOptions` has no device field; the kernel queue and the cache use device 0. A device
  that reports 0 bytes of memory gets a 1 GiB cache budget and one WARN.

**Open after the composition, owned by T18 unless another task is named**

1. A node whose work threw is settled by `on_local_completion` like one that ran. The scheduler
   has to push that event with `executed = false`. Owner: the orchestrator, with the engine.
2. The "listening" line names no address. `TcpTransport::start()` and the in-process transport
   return the bound endpoint in `TransportResult::message` on `Ok`, and the coordinator logs it.
3. The "registered" line has no build id. `WorkerDirectory` keeps `build_id` in the session and in
   `WorkerSnapshot`.
4. `send_message` exists three times: `frame_sending.hpp`, `master_link.cpp`,
   `dispatch_executor.cpp`. The two copies go.
5. The PING token clock is computed in `cluster_monitor.cpp` and `distributed_coordinator.cpp`.
   One private header holds it.
6. `BufferRegistry::bump` on a span that names one buffer twice gives two versions of one
   output. `OffloadDispatcher::prepare` refuses a node whose writes name one allocation twice.
7. `MasterLink::on_lost` and `on_connect_abandoned` log WARN and ERROR for a loss the worker
   caused by its own `stop()`.
8. After `Disconnected` the exec thread's open input pull waits out `pull_idle_timeout`.
9. The five exit-code tests of `test_worker_main.cpp` run over TCP and raise a real SIGTERM. They
   move to `serve_worker()` on the in-process fabric with `StopSignal::request()`; one TCP case
   stays for `run_worker` itself.
10. `UsmAllocator` calls `on_release` for every release, so each takes the registry mutex for one
    failed lookup. Phase E.

### What the first outside user of the surface met (T15)

The demo, the worker program and the benchmark are the first code written against the public
headers from outside the module. Each line names its owner.

1. `ClusterStatistics` has no count of tasks a worker finished; the demo derives it from
   `offloaded - redispatched`. Add `completed_remote`. Owner: T19.
2. `add_registered_task` takes a `Graph::TaskGraph&`, which `API::Graph` does not expose, so a
   fluent program states a fallback lambda beside the registered kernel. Owner: C8, through the
   recorder.
3. `Buffer::distributed()` before `attach()` does nothing and logs nothing. One WARN. Owner: T19.
4. The default policy keeps a task under 64 KiB of inputs local; `cluster.hpp` does not say so.
   Owner: T17.
5. The two `register_kernel` overloads both take a `std::function`, so a generic lambda is
   ambiguous. The deprecated pointer form goes in this release, which removes the ambiguity.
   Owner: T19.
6. The `_op` literal lives in `Graph::Literals` and no distributed header re-exports it. Owner: C13.
7. `WorkerAgent` takes an `Execution::RuntimeContext&` only; `Cluster::attach` has an
   `API::Runtime` overload. Owner: T19.
8. `offload_coordinator.hpp` documents `offloaded` as dispatches a worker accepted,
   `cluster_statistics.hpp` as dispatches queued. One sentence. Owner: T17.
9. `run_worker(argc, argv, ...)` refuses a flag it does not know, so an application with a flag of
   its own parses first and calls the `WorkerOptions` overload. Documented. Owner: T17.
10. `SushiRuntime.h` includes no distributed header. One umbrella
    `SushiRuntime/distributed/distributed.hpp`. Owner: C13.
11. `tests/benchmark/README.md` describes the old distributed benchmark. Owner: T17.

### What the TCP fault tests found by reading (T14)

None of these is verified by a run. Owner: T19, unless marked.

1. A peer whose socket is blocked inside `write_all` is retired after `write_idle_timeout` (5 s),
   not after liveness: the writer of `peer_connection.cpp` reads `closed_locally_` only between
   frames, and the reader reports the loss only after the writer shut the socket. The monitor marks
   the worker dead at once, and its tasks come back late. `close_peer()` shuts the socket itself.
2. A worker transport keeps connecting after the link ended with `RejectedAuthentication` or
   `ProtocolMismatch`; an embedded `WorkerAgent` nobody stops makes one connection per backoff
   period. The agent ends the connecting when the link ends.
3. `redispatched` and `peers_lost` rise after the node is back with the scheduler, so a caller that
   reads statistics right after a run can see the old values. The counters rise first.
4. `handshake_rejected_version` rises for a peer the directory no longer knows, while the ERROR
   line is skipped for it.
5. `ClusterStatistics` has no counter for a close on bad magic or a malformed frame.
6. An honest worker with the wrong secret checks the master's proof first and closes without
   `PROOF`, so the master counts nothing and logs "closed before it registered". Protocol
   behaviour; the guide says so. Owner: T17.
7. Every open connection counts toward `max_peers`, registered or not. Floods are out of scope
   for a trusted LAN; the guide says so. Owner: T17.

### What the defect pass left open (T19)

1. `PeerConnection::close()` shuts the socket itself only when the writer has spent `close_linger`
   (1 s) inside one socket write; a writer that waited less still runs to `write_idle_timeout`. A
   stalled transfer is therefore lost on time with the default 5 s liveness and late with a
   liveness under about 1 s. The fix is `Socket::shutdown_receive()` in the socket seam: `close()`
   wakes the reader, which waits for the writer for `close_linger` and then shuts both directions.
2. A failed `attach` still removes the cluster that was installed before it. The engine has to
   offer the sink before a coordinator is installed: `RuntimeContext::offload_sink()`. Then
   `attach` binds and starts the new coordinator first and replaces the old one only on success.
3. A worker with a wrong secret that already sent five `HELLO` on one connection is not rejected on
   that connection; the master's handshake timeout closes it and the next connection rejects it.
4. Comments that the stalled-writer rule made incomplete: `peer_connection.hpp` (`close()`),
   `tcp_transport.hpp:146`, `i_transport.hpp:333-339`, `cluster_monitor.cpp:211`.
   `docs/architecture/ARCHITECTURE.md:1018` names the removed `RemoteKernel`. Owner: T17.
5. `K_LOSS_CAUSE_WORDS` of the coordinator and `loss_cause_name()` of the link are two tables for
   one enum.
6. No test covers the WARN of `Buffer::distributed()` without a cluster, nor a `WorkerAgent` built
   on an `API::Runtime`.
