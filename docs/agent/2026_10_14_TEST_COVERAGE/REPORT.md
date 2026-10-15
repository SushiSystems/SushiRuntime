# Test coverage: report

**Status:** Phases 1 to 3 and task 1.10 landed on 2026-10-14. The break check of the new tests
ran on 2026-10-15.

## What landed

| Commit | Change |
| --- | --- |
| `8fd007b` | `refactor(distributed)!`: the shared vocabulary moves down into the public modules `transport` and `cluster_load`. Nine internal modules take the private parts. Tests stop reaching `source/`. |
| `819915e` | `refactor(engine)`: `dependency_tracking`, `work_stealing`, `region_boundary`, `numa_lookup` and `engine_handles` become modules, each with its own tests. |
| `33c8e15` | `test`: one test file for each of the 105 public headers that had none, the example tests and their README sections. |
| `d449cba` | `docs(agent)`: plan status. |

The include paths of `transport` and `cluster_load` changed, so the first commit breaks
consumers.

## Evidence

- Build: `CMAKE_BUILD_PARALLEL_LEVEL=4 sr build --distributed --jobs 4` reported "Build completed
  successfully".
- Tests: `sr test --distributed` reported "100% tests passed, 0 tests failed out of 1022". Three
  tests were skipped for the reasons the suite already gives:
  `Integration_DevicePipelineTest.DiamondAllKernelNodes`,
  `Integration_InteropHostExport.ImportOnAQueueWithoutTheExtensionReturnsUnsupported` and
  `Integration_SteadyStateAllocation.ProfiledReuseTicksDoNotAllocatePerTick`.
- The layering, naming, source comment, docs truth, changelog and test mirror checkers reported
  no findings.

## Departures from the plan

- `numa_lookup` became a platform module. The plan had it tested through the mapper instead.
- The emit_state tests run what the description does, with no private hook.
- `tools/layering/check_layering.py` keeps its own copy of the same-tier edges, so it gained the
  edges of the new modules. The SushiSkills copy of the checker was not touched.

## Task 1.10

`check_test_mirror.py` gained `rule_private_include`, written in SushiSkills as `1a1e278` and
copied here with this repository's license header. It reports an include that names a header
under a `source/` folder, by a path through `source/` or by its path below `source/`. A quoted
include that resolves beside the test file is a local helper and is not reported. The copy also
brings two rules SushiSkills already held: a unit's folder of aspect files, and the tier in the
include path.

On this tree the rule reports nothing. On `7f4bc7c`, the commit before this work, it reports 54
includes of 31 distinct private headers, the count the plan gives.

## Break check

On 2026-10-15 five headers were broken by hand and the suite was run. `countr_zero_u64(0)`
returned 63, `Extent::count()` added one, the deque's `size()` added one, `late_count_source`
kept an over-capacity host count, and the NUMA lookup added one to the node index. 11 tests
failed, and each break failed the new test written for its header:
`Unit_Bit.CountrZeroFindsTheLowestSetBit`, `Unit_Extent.CountIsTheProductOfTheSizes`, two
`Unit_WorkStealingDeque` tests, `Unit_LateCountResolution.AHostCountAboveTheCapacityResolvesToZero`
and two `Unit_NumaLookup` tests. The breaks were reverted and the suite passed 1022 of 1022.

The rest of the new test files were checked on 2026-10-15. All breaks went in at once, one per
header, each a small change to a value or a branch the header's test reads, and the suite ran
once with every break applied. 322 of 1022 tests failed. 62 headers were broken, and the test
of 60 of them failed:

- foundation: `operation_id`, `error_translation` (core and SYCL), `log_level`, `log_gate`,
  `device_description`, `device_filter`, `worker_pool`, `completion_signal`, `measurement`,
  `liveness`, `barrier`, `usm_shared`;
- platform: `queue_flavour`, `allocation`, `usm_allocation_owner`, `usm_request`,
  `event_timestamps`, `hwloc_topology_provider`, `remapping_topology_provider`;
- engine: `dependency_tracker`, `chain_plan` (execution and graph), `chain_run`,
  `runtime_settings`, the three probe tasks, `failure_naming`, `offload_gate`,
  `worker_pool_settings`, `run_outcome`, `node`, `node_widths`, `late_bindings`, `region_link`,
  `task`;
- surface: `before_step_hooks`, `emit_captured`, `emit_device_sized`, `emit_host`,
  `emit_late_count_resolver`, `emit_tracked`, `emit_untracked`, `handle_access`, `late_window`,
  `node_description`, `node_stamp`, `step_run`, `plan_cache`, `nd_range`, `captures`,
  `dependencies`, `device_uuid`, `external_memory`, `memory_exchange`;
- examples: `diffusion_backend`, `distributed_kernels`.

Two breaks were not caught:

- `matrix_distance_model.hpp`, reading the matrix transposed. The test named for an asymmetric
  fabric builds a symmetric matrix. `Unit_TopologyAbstractionTest.MatrixDistanceModelReadsTheSourceRow`
  now builds one whose two directions differ; with the transposed read it failed, and so did
  `Unit_TopologyView.DistancesMatchMatrixIncludingDiagonal`.
- `step_machinery.hpp`, registering a view's resync on every `view_for()` call. A second resync
  publishes the same view again, so the duplicate costs work and changes no result. No test
  through the surface can see it.

With every break applied, a test fails for its own break and also for the breaks of the headers
it uses, so the run proves that each header's test can fail, not that only its own break fails
it. The failure messages of the unit tests named above match their own breaks.

These headers were not broken, and why:

- Checked by `static_assert`, so a break stops the build: `common.hpp`, `export.hpp`,
  `log_level` values, `device_kind.hpp`, `pin_policy.hpp`, `dispatch_width.hpp`,
  `callable_checks.hpp`.
- An interface, a forward declaration, an alias or plain data with no behaviour of its own:
  `log_sink.hpp`, `device_descriptor.hpp`, `usm_release_observer.hpp`, `topology_provider.hpp`,
  `rebalancer_interfaces.hpp`, `node_profiling.hpp`, `offload_coordinator.hpp`,
  `offload_sink.hpp`, `offload_state.hpp`, `ready_sink.hpp`, `task_work.hpp`,
  `late_controls.hpp`, `plan.hpp`, `node_spec.hpp`, `coupled.hpp`, `context.hpp`,
  `graph/access.hpp`.
- On the path of every run, so a break takes down the suite and hides the other results:
  `quiescence_latch.hpp`, `context_factory.hpp`, `scheduler_factory.hpp`, `emit_task.hpp`,
  `emit_dynamic.hpp`, `sycl_compat.hpp` (`enqueue_host_task`), `device_probe.hpp`,
  `context_handle_access.hpp`, `late_controls_access.hpp`, `plan_access.hpp`,
  `boundary_access.hpp`, `placement.hpp`.

`test_log_sink.cpp` checks only that a sink can be installed and removed; it does not check that
a line reaches the sink.

With the breaks reverted and the new test added, `sr test --distributed` reported "100% tests
passed, 0 tests failed out of 1023", with the three skips listed under Evidence.

## Open

- **Break check**: the headers listed above as not broken were not checked that way.
- **Commit `fa75278`** (an over-capacity host LateCount resolves to 0) came from another
  session. The tests here follow its behaviour. It is also on local `main` as `51af947`, and
  that commit is not pushed.
- The three split candidates and the three test-folder helpers listed in
  `docs/design/REMAINING_WORK.md` are not part of this work.
