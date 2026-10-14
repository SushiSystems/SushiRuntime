# Test coverage: one test file per public header, no test reaching `source/`

**Status:** Phases 1 to 3 written inline; one build and test run closes them

## Goal

When this work is done:

1. `tools/testing/check_test_mirror.py .` reports nothing. Today it reports 100 public headers
   without a test file.
2. No test includes a header under `modules/<tier>/<module>/source/`. Today 31 private headers
   are included by tests, 23 of them in `distributed`.
3. The full suite passes with `sr build --distributed` and `sr test --distributed`. Before the
   work this was 844 tests, all passing, 3 of them skipped.

The rules come from the `testing` skill ("Private details are not tested directly. If a private
function seems to need its own test, it is a brick that wants its own module") and from
`repository-layout/TESTS.md`.

## Order

The `multi-agent-work` skill puts hierarchy first: module boundaries are fixed and committed
before any test moves across them. Phase 1 changes boundaries, phase 2 moves tests that already
exist, and phase 3 writes the tests that are missing.

| Phase | What | Changes a boundary | New tests |
| --- | --- | --- | --- |
| 1 | Remove every include of a private header from `tests/` | Yes, by D1 to D3 | No |
| 2 | Move tests that check a header but sit in another header's file | No | No |
| 3 | Write the tests no header has | No | Yes |

Phase 1 comes first because it adds public headers, and every header it adds needs a test file
in phase 2 or 3.

## Phase 1: private headers reached by tests

### What is reached

| Module | Private headers the tests include |
| --- | --- |
| `distributed` | `wire_frame`, `wire_codec`, `wire_messages`, `message_router`, `sha256`, `hmac_sha256`, `handshake`, `socket`, `outbox`, `frame_sending`, `buffer_registry`, `buffer_exchange`, `pending_tasks`, `worker_directory`, `offload_dispatcher`, `distributed_coordinator`, `dispatch_deduplicator`, `dispatch_executor`, `master_link`, `worker_buffer_cache`, `worker_session`, `worker_options_parser`, `environment`, `stop_signal` |
| `graph` | `dependency_tracker` |
| `scheduler` | `work_stealing_deque` |
| `topology` | `numa_lookup` |
| `api` | `boundary_access`, `plan_access`, `late_controls_access`, `node_emitters` |

The helpers under `tests/` reach two more: `tcp_fault_proxy.hpp` reaches `socket.hpp`, and
`dynamic_graph_plan_access.hpp` reaches `plan_access.hpp`.

### D1: the bricks of `distributed`

The tests of these bricks are the most valuable tests of the extension: golden bytes,
truncation, fencing, reaping and the handshake vectors. Testing them only through `Cluster`
would lose them, so the skill's answer applies: each group of bricks becomes an internal module
in the `extension` tier, and `distributed` depends on them. Each new module is `INTERNAL`, so
nothing is installed and the public surface of the library does not change.

| Proposed module | Headers it takes from `distributed/source/` | Responsibility |
| --- | --- | --- |
| `wire` | `wire_frame`, `wire_codec`, `wire_messages`, `message_router` | The bytes of a frame and of every message |
| `link_security` | `sha256`, `hmac_sha256`, `handshake` | Proving a shared secret on a new link |
| `network_socket` | `socket`, with `socket_posix.cpp` and `socket_windows.cpp` | One TCP connection on either platform |
| `frame_transport` | `outbox`, `frame_sending` | Ordering and sending frames on a link |
| `offload_coordination` | `buffer_registry`, `buffer_exchange`, `pending_tasks`, `worker_directory`, `offload_dispatcher`, `distributed_coordinator` | The master's side |
| `worker_runtime` | `dispatch_deduplicator`, `dispatch_executor`, `master_link`, `worker_buffer_cache`, `worker_session`, `worker_options_parser`, `environment`, `stop_signal` | The worker's side |

Task 1.0 measures the include graph of these headers before any file moves. A grouping that
needs an edge from a lower group to a higher one is redrawn there, before the owner signs it
off. The same-tier edges go into `cmake/SushiRuntimeTiers.cmake` and its copy in
`check_layering.py` in the same commit.

**Alternative:** keep the bricks private and test them only through `Cluster` and
`run_worker`. This loses about 160 focused tests and is not recommended.

### D2: the engine bricks

| Header | Recommendation | Reason |
| --- | --- | --- |
| `graph/dependency_tracker` | Internal module `dependency_tracking` in `engine` | The 11 tracker tests pin ordering rules that `TaskGraph` tests cover only in part |
| `scheduler/work_stealing_deque` | Internal module `work_stealing` in `engine` | The 3 tests check overflow and concurrent steal, which no scheduler test can force |
| `topology/numa_lookup` | Tested through `numa_mapper.hpp` | The lookup is a function of the mapper; the 3 tests rewrite against the mapper's entry point |

### D3: the test access of `api`

`boundary_access`, `plan_access`, `late_controls_access` and `node_emitters` exist to let a test
see inside a `Graph` or a `DynamicGraph`: the compiled plan, the entry and exit pins of a region,
and the node an emitter builds. `graph.hpp` and `dynamic_graph.hpp` already befriend
`RegistrationIndexTestAccess`, `NodeEventRecordsTestAccess` and
`DynamicGraphNodeEventsTestAccess` for the same reason.

| Option | Shape | Cost |
| --- | --- | --- |
| A (recommended) | One internal module `graph_inspection` in `surface` that holds the read-only views a test needs and the friend structs. The `api` headers name the friends; nothing a consumer includes changes | One module; the friend declarations stay |
| B | Make the views public through `PlanView`, which already reports predecessors and regions | Widens the public surface; an API decision for v0.4.0 |
| C | Rewrite the 27 tests that use them against observable behaviour only | Some pin checks, such as entry-pin shadowing, cannot be observed from outside |

### D4: what counts as a unit

The `testing` skill tests private details through the public unit that uses them. Two kinds of
public header have no behaviour of their own to test:

- Interfaces with no data, such as `OffloadSink`, `ReadySink`, `TopologyProvider` and `LogSink`.
- Vocabulary, such as `DeviceKind`, `PinPolicy`, `QueueFlavour` and `export.hpp`.

**Recommended:** each still gets its test file, holding what the header promises. For an
interface, that is a conformance test the implementations run. For vocabulary, it is the values,
the defaults and the traits, checked with `static_assert` and plain `EXPECT_EQ`. The alternative
is an exemption list in `check_test_mirror.py`, which `TESTS.md` does not allow.

### Tasks

| Task | Acceptance criterion | Build |
| --- | --- | --- |
| 1.0 | The include graph of the 24 `distributed` bricks is written into this plan, and every proposed module depends only downwards | none |
| 1.1 | `wire` exists; `tests/extension/wire/` holds the wire tests; no test includes `wire_*.hpp` or `message_router.hpp` by a quoted name | `sr build --distributed` |
| 1.2 | `link_security`, the same for the handshake tests | the same |
| 1.3 | `network_socket`, the same for the socket tests and `tcp_fault_proxy.hpp` | the same |
| 1.4 | `frame_transport`, the same for the outbox tests | the same |
| 1.5 | `offload_coordination`, the same for the coordinator tests | the same |
| 1.6 | `worker_runtime`, the same for the worker tests | the same |
| 1.7 | `dependency_tracking` and `work_stealing` exist; the tracker and deque tests sit in their mirrors | `sr build` |
| 1.8 | The NUMA lookup tests call the mapper; `numa_lookup.hpp` has no test caller | `sr build` |
| 1.9 | D3 is carried out; no test includes an `api` private header | `sr build` |
| 1.10 | `check_test_mirror.py` gains a rule that reports a test including a header under `source/`; written in SushiSkills first, then copied | the tools tests |

Each of 1.1 to 1.9 ends with `sr test` green and the same test names as before. A test file that
leaves `distributed` keeps its suite and test names, so the comparison stays one `Compare-Object`.

## Phase 2: tests that sit in the wrong file

These headers already have tests, but the tests sit in the file of another header. Moving them
closes the header without writing a new test.

| Header | Its tests sit in today |
| --- | --- |
| `memory/allocation.hpp` | `Unit_Allocation` in `tests/surface/api/data/test_buffer.cpp` |
| `topology/matrix_distance_model.hpp` | `Unit_TopologyAbstractionTest.MatrixDistanceModel*` in `tests/engine/execution/test_runtime_context.cpp` |
| `topology/remapping_topology_provider.hpp` | `RemappingProviderSplitsRealDevicesAcrossFakeNodes` in the same file |
| `execution/chain_run.hpp` | `Integration_ChainLayer` and `Integration_RunInProgress` |
| `distributed/in_process_transport.hpp` | `InProcessTransportDeliversBothDirections` in `test_offload_policy.cpp` |
| `distributed/tcp_transport.hpp` | The conformance tests in `test_transport.cpp` run against it |
| `observability/event_timestamps.hpp` | `HostNodeLandsOnEventClock` in `tests/surface/api/graph/test_graph.cpp` |
| `api/launch/extent.hpp`, `nd_range.hpp` | `Extent2DPerElementMap`, `NdRange2DTiledKernel`, `AnExtentWhoseEndWrapsIsRefusedAtAdd` |
| `api/vocabulary/captures.hpp` | `InOutCapturedArguments` in `test_fluent.cpp` |
| `api/detail/before_step_hooks.hpp` | The `AStepHook*` tests in `test_step_driver.cpp` |
| `api/graph/plumbing/plan_cache.hpp` | `CompileThenRunCompilesOnce` and the recompose counts |

A test moves only when the header is the unit whose behaviour it checks (`TESTS.md`). A test
that drives a `Runtime` to observe `Extent` stays with the higher unit, and the lower header
gets a direct test in phase 3. Task 2.0 settles each row by reading the test.

| Task | Acceptance criterion | Build |
| --- | --- | --- |
| 2.0 | Each row above is marked "move" or "stays", with the test names | none |
| 2.1 | Every "move" row is moved; test names are unchanged | `sr build --distributed` |

## Phase 3: the tests that are missing

After phases 1 and 2, the headers left fall into four kinds. Counts are of today's 100 and
shrink by the headers phase 2 closes.

| Kind | Headers | What the test file holds |
| --- | --- | --- |
| Behaviour | about 35, such as `node.hpp`, `run_outcome.hpp`, `quiescence_latch.hpp`, `offload_gate.hpp`, `operation_id.hpp`, `late_window.hpp`, `step_machinery.hpp`, `measurement.hpp`, `barrier.hpp`, `usm_shared.hpp` | Unit tests of each promise the header documents |
| Interface | about 10, such as `offload_sink.hpp`, `ready_sink.hpp`, `topology_provider.hpp`, `log_sink.hpp`, `rebalancer_interfaces.hpp`, `usm_release_observer.hpp` | A conformance test over a fake and over each implementation in the tree |
| Vocabulary | about 40, such as `device_kind.hpp`, `pin_policy.hpp`, `queue_flavour.hpp`, `node_description.hpp`, `export.hpp`, `common.hpp` | Values, defaults and traits, in `static_assert` and `EXPECT_EQ` |
| Example | `applications/examples/adapter/diffusion_backend.hpp`, `applications/examples/distributed_kernels.hpp` | Tests under `tests/applications/examples/`, built only with the examples |

The seven test-support modules (`measurement`, `thread_barrier`, `device_probe`, `usm_guard`,
`probe_tasks`, `runtime_assembly`, and the headerless `test_runner`) are part of the behaviour
and vocabulary rows. A helper that every test relies on is the first place a wrong result hides.

One task per module keeps a task to one build and one file set:

| Task | Module | Headers |
| --- | --- | --- |
| 3.1 | `core` | `bit`, `common`, `error_translation`, `export`, `operation_id` |
| 3.2 | `log`, `logging` | `log_level`, `log_sink`, `log_gate` |
| 3.3 | `machine` | `device_description`, `device_filter`, `device_kind`, `pin_policy`, `worker_pool` |
| 3.4 | `region`, `report`, `sycl` | `liveness`, `dispatch_width`, `error_translation`, `sycl_compat` |
| 3.5 | test-support modules of `foundation` | `measurement`, `completion_signal`, `barrier`, `device_probe`, `usm_shared` |
| 3.6 | `device`, `memory`, `observability`, `topology` | the headers phase 2 leaves |
| 3.7 | `task` | `node`, `task`, `late_bindings`, `node_profiling`, `node_widths`, the offload and sink headers, `region_link`, `task_work` |
| 3.8 | `scheduler` | `failure_naming`, `offload_gate`, `quiescence_latch`, `rebalancer_interfaces`, `run_outcome`, `worker_pool_settings` |
| 3.9 | `graph`, `execution` | `chain_plan` (both), `chain_run` if phase 2 leaves it, `runtime_settings` |
| 3.10 | test-support modules of `engine` | `atomic_counter_task`, `order_recorder`, `priority_tracking_task`, `context_factory`, `scheduler_factory` |
| 3.11 | `api/detail` | the 20 headers of the folder |
| 3.12 | `api` outside `detail` | `access`, `plan_cache`, `extent`, `nd_range`, `captures`, `coupled`, `dependencies`, `placement` |
| 3.13 | `context`, `interop`, `memory_exchange` | `context`, `context_handle_access`, `device_uuid`, `external_memory`, `memory_exchange` |
| 3.14 | `distributed` | `buffer_view`, `cluster_statistics`, `in_process_transport`, `kernel_registry`, `registered_task`, `tcp_transport`, minus what phase 2 closed |
| 3.15 | examples | the two headers under `applications/examples/` and their build |

The acceptance criterion of each task: `check_test_mirror.py --rule rule_untested_unit` no longer
lists the task's headers, each new test fails when the behaviour it names is broken by hand, and
`sr test` is green. Breaking the code by hand to see a test fail is the `testing` skill's check
that a test proves something; the report names the edit and the failing test.

## Execution

The owner chooses inline or waves. A proposal for waves, by the file sets above:

| Wave | Tasks | Closed by |
| --- | --- | --- |
| 1 | 1.0, then 1.1 to 1.6 in order (they share `distributed/CMakeLists.txt`) | `sr build --distributed`, `sr test --distributed` |
| 2 | 1.7, 1.8, 1.9, 1.10 in parallel (disjoint files) | the same |
| 3 | 2.0, 2.1 | the same |
| 4 | 3.1 to 3.15 in parallel, one worker per module | the same |

Wave 4 writes about 85 test files across 15 workers. Builds stay one at a time, at
`CMAKE_BUILD_PARALLEL_LEVEL=6`, because parallel clang runs out of memory on this machine.
