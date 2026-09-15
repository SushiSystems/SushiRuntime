# G1 implementation plan: node event records and a per-graph profiling switch

**Status:** written 2026-09-15; nothing dispatched.

**Spec:** `docs/superpowers/specs/2026-09-15-g1-node-event-records-design.md`. Exact types,
rules and file:line citations live there; every task reads the spec's sections it names.

## Global constraints

- Every subagent prompt opens with the five-line dispatch block from `~/.claude/CLAUDE.md`.
- Implementers `sonnet`, reviewers `opus`, effort at most `medium`.
- No subagent runs `sr build`, `sr test`, `cmake`, `ninja` or `ctest`, edits a CMake file, or
  runs git stash, checkout, reset, clean, add or commit. The controller wires CMake lists and
  commits with pathspecs.
- The owner builds with `sr build` after each wave. The controller runs the wave's tests with
  `sr test`, after pinging sushiengine-30 and sushiengine-39.
- Profiling off costs one untaken branch per run and no allocation. A profiled run allocates
  only when it exceeds its plan's high-water mark, never inside the poller.
- `Core::NodeEventRecord` and `Core::ClockPairing` stay field-for-field identical to SushiEngine's
  `Execution::NodeEvent` and `Execution::ClockPairing` (commit `642cf1b5` there).
- Public API changes follow the `api-stability` skill; `docs/INTEGRATION.md`,
  `docs/ARCHITECTURE.md` and `docs/CHANGELOG.md` change in the commit that changes the surface.

## Wave 1: the three independent bricks

### Task 1: record types

- **Files:** `include/SushiRuntime/core/run_report.hpp`, `tests/unit/test_node_event_record.cpp`.
- **Waits on:** nothing.
- **Content:** spec §B's `NodeEventRecord`, `ClockPairing`, and the two `RunReport` fields.
  `RunReport::reset()` clears `node_events` without releasing its capacity and zeroes `clock`.
- **Test:** the layout is 32 bytes and trivially copyable; `reset()` keeps capacity and zeroes
  the pairing.
- **Acceptance:** `sr test` passes the new unit test.

### Task 2: registration index

- **Files:** `include/SushiRuntime/graph/node.hpp`, `src/graph/task_graph.cpp`,
  `include/SushiRuntime/api/graph/graph.hpp` (the `add` shapes only),
  `tests/unit/test_registration_index.cpp`.
- **Waits on:** nothing.
- **Content:** spec §D static half. A per-graph counter is stamped on each node at registration;
  every internal node one public `add()` creates shares that call's index (F5).
- **Test:** nodes registered A, B, C carry 0, 1, 2 after compile, whatever the compiled order;
  an `add_reduce` of several tile levels carries one index.
- **Acceptance:** `sr test` passes it. The test fails if the index is read from `plan_index`.

### Task 3: lazy profiling queue

- **Files:** `include/SushiRuntime/execution/runtime_context.hpp`,
  `src/execution/runtime_context.cpp`, `tests/integration/test_profiling_queue.cpp`.
- **Waits on:** nothing.
- **Content:** spec §A option 2 and F2/F3. `profiling_queue(device_index)` returns a queue
  built on the plain queue's context with `enable_profiling`, created on first request under a
  once-flag per device, or eagerly at `probe_hardware` when `RuntimeSettings::profiling` is true.
- **Test:** the plain queue lacks the property and the profiling queue has it; both share one
  context; USM allocated through the plain queue runs a kernel on the profiling queue; with the
  setting off, no profiling queue exists until one is requested.
- **Acceptance:** `sr test` passes it on the CPU device and on CUDA.

**Controller after wave 1:** wire the three tests into `tests/CMakeLists.txt`, syntax-check,
dispatch one `opus` review with named items for SOLID shape, humanizer register, and the spec
rules each task cites, then commit. **Owner build:** `sr build`.

## Wave 2: the graph switch

### Task 4: `Graph` requests profiling and selects the queue

- **Files:** `include/SushiRuntime/api/graph/graph.hpp`, `src/execution/execution_engine.cpp`
  (`build_plan` stamp only), `src/scheduler/scheduler.cpp` (queue selection only),
  `tests/integration/test_graph_profiling_switch.cpp`.
- **Waits on:** wave 1.
- **Content:** spec §B setter pair, stamped into `CompiledGraph::profiling` at `build_plan()`.
  Every run-path read of `ctx_->profiling_enabled()` in `graph.hpp` becomes the plan flag. The
  scheduler dispatches a profiled plan's nodes on `profiling_queue`. `run(RunReport&)` and
  `run(steps, RunReport&)` are added and the value-returning overloads forward to them.
  Records are not captured yet; `NodeTiming` keeps today's path.
- **Test:** two graphs on one runtime, one requested and one not: only the requested one's
  `NodeTiming` rows fill, and the unrequested graph's events carry no profiling info.
- **Acceptance:** `sr test` passes it and the existing observability suite.

**Controller and owner build** as after wave 1.

## Wave 3: capture

### Task 5: records, pairing, collect

- **Files:** `include/SushiRuntime/scheduler/event_polling.hpp`, `src/scheduler/scheduler.cpp`
  (host timing and pairing), `src/execution/execution_engine.cpp` (`collect_profiling`,
  `reset_profiling`), the plan's ledger header beside `CompiledGraph`,
  `tests/integration/test_node_event_records.cpp`.
- **Waits on:** wave 2.
- **Content:** spec §C and F1, F4, F7. The ledger is sized to the high-water mark; the poller
  writes three timestamps per device dispatch; host nodes store steady times converted at
  collect through the run's pairing; the first device submission of a run claims the pairing
  by compare-exchange on the queue that ran it; `NodeTiming` rows derive from the records.
- **Tests:** spec §E items 1, 2, 3, 4 and 6: no records when not requested; `run(5)` on three
  kernel nodes gives fifteen records with invocations 0 to 4 and `submit <= start <= end`; a
  reused report does not allocate on its second run; indices follow registration order; host
  records land inside the run's event window within `uncertainty_ns`.
- **Acceptance:** `sr test` passes them on the CPU device and on CUDA.

**Controller and owner build** as before.

## Wave 4: dynamic graph and documentation, in parallel

### Task 6: `DynamicGraph`

- **Files:** `include/SushiRuntime/api/graph/dynamic_graph.hpp`,
  `tests/integration/test_dynamic_graph_node_events.cpp`.
- **Waits on:** wave 3.
- **Content:** the same setter, run overload and plan flag; indices numbered densely by region
  key then `add()` order at each recompose (F6).
- **Test:** spec §E item 8: two regions, drop and re-add one, indices stay dense and ordered.

### Task 7: documentation and benchmark

- **Files:** `docs/INTEGRATION.md`, `docs/ARCHITECTURE.md`, `docs/CHANGELOG.md`,
  `tests/benchmark/bench_profiling_queue.cpp`.
- **Waits on:** wave 3.
- **Content:** the per-graph request, the record semantics of F1, F5 and F6, and the queue cost
  measured in the spec's §A; spec §E item 7 as a benchmark that reports the per-submission delta.

**Controller after wave 4:** review, commit, and tell sushiengine-30 that T2.2 is unblocked.
