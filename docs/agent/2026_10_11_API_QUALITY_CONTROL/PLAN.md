# API refinement: plan

**Status:** Proposed on 2026-10-11; no task started. It waits for the owner's word, which comes
when the SushiAI work in the sibling checkout is committed.

`SPEC.md` of this folder holds the decisions and the measurements. `REPORT.md` holds the first
pass, whose repair plan stood in this file until this rewrite; its open items are in
`docs/design/REMAINING_WORK.md`.

## Rules

- Inline: one session, no worker writes source. Read-only studies may be dispatched.
- One task, one acceptance criterion, one build: `sr build --distributed --jobs 4`, then
  `sr test --distributed`. A consumer step uses `sb`, `sa` or `se` with four threads.
- One build runs at a time on the machine.
- One commit per task, staged by path, with its tests, its README, its changelog line.
- Every removal and every rename is breaking and carries `!`. No alias and no forwarder is left
  behind: the release is `v0.4.0` and nothing below `v1.0.0` owes a deprecation window.
- A step that turns out to need a decision listed under "Decisions that stay with the owner"
  stops there.

## The surface after the plan

```cpp
#include <SushiRuntime/sushiruntime.hpp>

auto runtime = SushiRuntime::Runtime::create();
if (!runtime)
    return report(runtime.error());

auto heat = runtime->state<float>(N);
if (!heat)
    return report(heat.error());

auto simulation = runtime->graph();
simulation.add_rule(*heat, [](std::size_t i, const float* current, float* next) { /* ... */ });

const auto report = simulation.run(STEPS);
if (!report)
    return report_failure(report.error());
```

| Part | Shape |
| --- | --- |
| Header | `<SushiRuntime/sushiruntime.hpp>`; single headers stay at `<SushiRuntime/api/<area>/<name>.hpp>` |
| Namespace | Every public name is in `SushiRuntime::`, once. The extension is `SushiRuntime::Distributed::` |
| Internal code | `SushiRuntime::Internal::<Module>::` for the engine, `SushiRuntime::Detail::` inside the surface; neither is installed |
| Failure | `Result<T>` carrying `Error{ErrorCode, message}`. A broken precondition stops the process through `SR_ASSERT` |
| Recording | A verb returns the builder. The builder keeps the first refusal; the next run returns it, and `refusal()` reads it earlier |

### Verbs of a builder

| Verb | Records | Replaces |
| --- | --- | --- |
| `add(launch, reads, writes, kernel)`, `add(launch, captures..., kernel)` | A tracked kernel | The same two forms |
| `add_rule(state, kernel)`, `add_rule(launch, state, kernel)` | A rule over one `State` | `add(State&, ...)` |
| `add_coupled(Coupled(...), kernel)` and its launch form | A rule over several states | `add(Coupled, ...)` |
| `add_dynamic(Dynamic, capacity, reads, writes, kernel)`, `add_dynamic(Dynamic, state, kernel)` | A late-bound window | `add(Dynamic, ...)` |
| `add_task(reads, writes, task)` | A body `(sycl::queue&, events) -> sycl::event` | New; what SushiBLAS records |
| `add_host(reads, writes, task)` | Host work | Unchanged |
| `add_untracked(...)` | A kernel with no declared access | Unchanged |
| `extend(extension)` | What an extension records through `Recorder&` | `extend`, and `add_emitter`, which handed out the engine's graph |

An offload is not a verb. A node option names the operation, and the distributed extension reads
it when the plan is built.

```cpp
struct NodeOptions
{
    std::size_t width = K_WIDTH_UNKNOWN;
    std::string_view name;
    OperationID operation;
    span<const std::uint64_t> parameters;
};
```

`Recorder` carries the same verbs under the names `kernel`, `state_rule`, `coupled_rule`,
`dynamic_kernel`, `task`, `host`, `untracked_kernel`, and each returns the position of the rule
it recorded.

## Names

| Today | After |
| --- | --- |
| `API::X` and `SushiRuntime::X` | `SushiRuntime::X` |
| `Core::`, `Graph::`, `Execution::`, `Task`, `Scheduler::`, `Memory::`, `Device::`, `Topology::`, `Observability::`, `Logging` | `Internal::<Module>::`; the public class `Graph` and the namespace `Graph` no longer share a name |
| `Errc`, `Errc::invalid_graph` | `ErrorCode`, `ErrorCode::InvalidGraph` |
| `SushiException` | Removed |
| `OpID` | `OperationID`; the literal stays `"axpy"_op` |
| `TaskMetadata`, `TaskType` | Internal; `NodeOptions` is the public form |
| `TaskType::MATH_OP`, `ExecutionMode::KERNEL`, `Backend::SYCL` | PascalCase enumerators |
| `IExecutionEngine`, `ITransport`, `IOffloadPolicy`, `ITelemetryProvider`, `ITaskMigrator`, `IRebalanceStrategy`, `IAllocationOwner`, `ITopologyProvider`, `INUMADistanceModel` | The name without `I`; where the one implementation already holds that name, the implementation takes a describing word (`ExecutionEngine` becomes `SchedulerExecutionEngine`) |
| `i_transport.hpp`, `i_offload_policy.hpp`, `i_topology_provider.hpp` | The file named after its type |
| 17 `get_` methods (`get_execution_engine`, `get_worker_count`, `get_name`, `get_param` ...) | The name without the prefix |
| `namespace detail` (66 lines, all in the surface) | `Detail` |
| 39 constants without `K_` (`NO_REGISTRATION`, `WIDTH_UNKNOWN`, `DEFAULT_ALIGNMENT`, `WHOLE`, `MAX_WORKER_THREADS` ...) | `K_` prefix; type traits such as `is_launch_v` keep their standard form |
| `WorkerStats`, `DeviceInfo`, `PeerInfo`, `WorkerPoolInfo` | `WorkerLoad`, `DeviceDescription`, `PeerDescription`, `WorkerPool` |
| `ctx`, `bg`, `fn`, `meta`, `q`, `deps`, `err` as parameter and member names | The whole word |
| `Runtime::rebalancer(bool)` beside `set_rebalancer_enabled(bool)` | `set_rebalancer_enabled` |
| `SushiRuntime.h`, and the 15 forwarding headers of `cmake/SushiRuntimeMovedHeaders.cmake` | `sushiruntime.hpp`; the forwarders are deleted |

## Stages

### Stage 0: ground

| Task | Work | Acceptance criterion |
| --- | --- | --- |
| 0.1 | Branch `feature/api_refinement` in the four repositories. Record the baseline: test counts, the public header count, the umbrella's include closure, the build time of `quickstart.cpp` | The four numbers are in `REPORT.md` under "Refinement baseline" |
| 0.2 | `SR_ASSERT`, `SR_VERIFY` and `SR_FATAL` in `core/assert.hpp`, with a handler a test can replace | A death test proves each macro stops the process and names the file and the line |
| 0.3 | A trial of the compiled boundary on a branch that is thrown away: the tracked kernel alone is recorded through a type-erased description, and the per-node round trip is measured against the baseline | The figure is in `REPORT.md`; above 2 % the owner decides between the boundary and three tiers before task 1.1 starts |

### Stage 1: remove what is deprecated

| Task | Work | Acceptance criterion |
| --- | --- | --- |
| 1.1 | The 31 forwarders of `runtime_context.hpp` and the four of `task_graph.hpp` and `rebalancer_interfaces.hpp`; their callers move to `devices()`, `memory()`, `topology()`, `transfers()` | `git grep '\[\[deprecated' -- modules/engine` is empty |
| 1.2 | The 24 `Core::` aliases of `memory/` and `observability/`, the `LogLevel` spellings, `LogType`, `ACTIVE_LOG_LEVEL`, the forwarding headers and `SushiRuntimeMovedHeaders.cmake` | The same search over `modules/foundation` and `modules/platform` is empty; no forwarding header is generated |
| 1.3 | The 36 deprecated members of the surface and the two `Cluster::attach` overloads; the four test files drop their deprecated cases and their pragmas | The search over the whole tree is empty; `git grep -i 'pragma.*deprecated'` is empty |

### Stage 2: one set of names

Each task is one scripted rename over the tree, in the manner of the acronym pass, followed by a
read of the diff.

| Task | Work | Acceptance criterion |
| --- | --- | --- |
| 2.1 | Engine, platform and foundation namespaces move under `Internal::` | No namespace of the library other than `SushiRuntime`, `Internal`, `Detail`, `Distributed` and `Literals` is declared at `SushiRuntime` scope |
| 2.2 | `API::` is dissolved into `SushiRuntime::`; the 30 second-name `using` lines go | `git grep -w 'API::'` finds only the export macro |
| 2.3 | `I` prefix, `get_` prefix, `detail` to `Detail`, `K_` constants, PascalCase enumerators, the abbreviated type names, the file names that follow | A checker run of `tools/naming` (added by this task) reports nothing on `modules/` |
| 2.4 | Parameter and member names written out; `Runtime::rebalancer` removed | The same checker reports none of the listed short names in a public header |
| 2.5 | `sushiruntime.hpp` replaces `SushiRuntime.h` | The out-of-tree consumer under `tests/package` builds against the new header |

`ErrorCode` and `OperationID` are renamed in stage 3 and stage 4, with the types they belong to.

### Stage 3: what a library author needs

| Task | Work | Acceptance criterion |
| --- | --- | --- |
| 3.1 | `OperationID` and `_op` become public vocabulary of the surface; `NodeOptions` gains `name`, `operation`, `parameters`; every `Recorder` verb takes it | A node recorded with a name and an operation reports both in its `RunReport` record |
| 3.2 | `Recorder::task` and `add_task` | A task that submits two command groups and returns the second event is ordered by its access against a kernel before and a kernel after it |
| 3.3 | Every `Recorder` verb and every builder verb yields the rule it recorded; `last_rule()` stays | The value a verb returns equals `rule_registration()`'s argument for that node after a run |
| 3.4 | `before_each_step(hook)` on a builder, run on the calling thread with the count of finished steps | A hook writes a host word a kernel reads, over eight replays, with one compile |
| 3.5 | `Runtime::wait()` and `AdvancedControls::queue(device)` | A caller waits for device work and submits a host copy without naming `RuntimeContext` |
| 3.6 | `Recorder::record` accepts a node description built without the engine's graph; `emitter` and `add_emitter` are removed and their callers use `extend` | No public signature of `recorder.hpp` or `builder_surface.hpp` names `TaskGraph` |

### Stage 4: `Result` only

| Task | Work | Acceptance criterion |
| --- | --- | --- |
| 4.1 | `ErrorCode` with PascalCase enumerators; `Result<void>`; `Result::value()` on a failure and `error()` on a value assert | The unit test of `result.hpp` covers the eight accessors with no `EXPECT_THROW` |
| 4.2 | The 27 precondition sites become `SR_ASSERT`; the two mislabelled codes (`buffer.hpp:197`, `interop.cpp:356`) with them | Each site has a death test or shares one with its helper |
| 4.3 | The builder keeps the first refusal: the five helpers and their 15 call sites in `recorder.hpp`, the two cycle checks of `TaskGraph::compile`, the cross-device check of `place_for_dispatch` | A refused `add` leaves the builder usable, and the next `run` returns the refusal with the rule's position |
| 4.4 | `Runtime::create`, `buffer`, `state`, `read_range`, `write_range`, `Recorder::scratch`, `Interop::export_host`, `import_device`, `capabilities` return `Result`; SYCL exceptions inside them are translated | No function of these headers lets a `sycl::exception` out; a test injects an allocation failure into each |
| 4.5 | The eleven run and wait pairs collapse: `run`, `run_for`, `run_until`, `run_async`, `wait` return `Result`; `unwrap` goes | `git grep -w 'try_run\|try_wait\|try_run_async'` is empty |
| 4.6 | The four data-driven faults (`device_fault.hpp:106`, `:107`, `late_count_resolution.hpp:96`, `late_window.hpp:79`) return `Result` from the run | A provider that returns a count above capacity fails the run with `InvalidGraph` and the process continues |
| 4.7 | `SushiException` and `SR_THROW_IF` are removed; the engine reports through `Error`; the three leaks found by the study are closed (`event_polling.hpp:348`, the observer call in `USMAllocator::release`, `wait_all` without translation) | `git grep -nE '\bthrow\b' -- modules ':!modules/**/tests/**'` is empty outside the translation of third-party exceptions |

Whether an error delivered only through the SYCL asynchronous handler reaches the caller of
`run()` on the scheduler path is not verified. Task 4.7 starts with a test that settles it.

### Stage 5: one verb per kind of node

| Task | Work | Acceptance criterion |
| --- | --- | --- |
| 5.1 | `add_rule`, `add_coupled`, `add_dynamic` on `BuilderSurface`; `add` keeps the tracked kernel | A wrong argument to `add` names at most three candidates in the compiler's message, shown by a compile-fail test |
| 5.2 | The offload overloads and `Recorder::offload`, `offload_rule` are removed; the extension reads `NodeOptions::operation` | `git grep -i offload -- modules/surface` finds only `NodeOptions` and the report field |
| 5.3 | `Buffer::distributed()` and `State::distributed()` move to `Cluster::share(buffer)`; `OffloadCoordinator::share` and `statistics` go | With `SR_ENABLE_DISTRIBUTED` off, no symbol of the library names an offload beyond the gate's five calls |
| 5.4 | `TaskDispatchMessage` owns its fields; `K_PROTOCOL_VERSION` is 3 | A version 2 peer is refused with `ProtocolMismatch` |
| 5.5 | The extension's public headers name no engine type: `add_registered_task`, the `RuntimeContext` overloads and `pick_worker(const Node&)` are reshaped | The 16 headers include only surface headers |

### Stage 6: the compiled boundary

| Task | Work | Acceptance criterion |
| --- | --- | --- |
| 6.1 | Sort every header the surface reaches into public or internal, and name the public module each value type moves to | A table in this file; the owner approves the module list |
| 6.2 | The public value types move: the run report, the region, the device and worker descriptions, the pin policy, the log level and sink, the interop descriptors | Each has one header in a public module and no engine include |
| 6.3 | `Recorder`, `PlanCache`, `RegionComposer` and the non-template half of `StepDriver` move to `modules/surface/api/source/`; a node description crosses the boundary with its kernel type erased once, at record time | `recorder.hpp`, `graph.hpp`, `dynamic_graph.hpp` and `step_driver.hpp` include no internal header |
| 6.4 | `Buffer`, `State`, `Runtime`, `AdvancedControls` and `Interop` reach the context through an opaque handle | The umbrella's include closure holds public headers only |
| 6.5 | The module function gains an `INTERNAL` mark; internal include roots are not installed; the export macro stays on public types only | An installed tree holds no internal header, and `tests/package` builds and runs against it |
| 6.6 | One header-alone compile check per public header | The check target builds |
| 6.7 | Measure the per-node round trip and the build time of `quickstart.cpp` against the baseline | Both figures are in `REPORT.md`; a regression of the round trip above 2 % stops the stage |

Stage 6 is where a kernel body meets a type-erased call. The erasure happens once per recorded
node, at record time. The per-element path stays a template inside the SYCL command group, so the
device code does not change. Task 6.7 is the proof, not this paragraph.

### Stage 7: SushiBLAS

| Task | Work | Acceptance criterion |
| --- | --- | --- |
| 7.1 | `Engine` is built on `Runtime&` and owns a `SushiRuntime::Graph`; `TaskRecorder` records through `Recorder::task`; `execute` and `discard_recorded` use `run` and `clear` | No file of SushiBLAS includes a header outside the surface |
| 7.2 | `TaskMetadata` becomes `NodeOptions` in the 71 operation files; the 148 `using namespace` lines of the literal name the public namespace | `git grep -w 'TaskMetadata\|TaskType'` is empty |
| 7.3 | `storage.hpp` allocates through the surface; the graph primitives return the builder's refusal in place of `SushiException` | The 2206 tests pass |
| 7.4 | The repository's own sweep: 19 `get_` names, `LogType`, `Cfg` | The naming checker reports nothing |

### Stage 8: SushiAI

| Task | Work | Acceptance criterion |
| --- | --- | --- |
| 8.1 | `CompiledStep` owns a `SushiRuntime::Graph` and replays it with `run`; the replay preludes become `before_each_step` hooks; `seam.cpp` maps `ErrorCode` and loses the exception ladder for SushiRuntime | `seam.hpp` and `compiled_step.hpp` include no header outside the surface |
| 8.2 | `trainer.cpp` reads profiling from `RunReport` | No file names `IExecutionEngine` or `CompiledGraph` |
| 8.3 | The repository's own sweep: 12 `get_` names | The naming checker reports nothing; the test suite passes |

### Stage 9: SushiEngine

| Task | Work | Acceptance criterion |
| --- | --- | --- |
| 9.1 | The BLAS seam of `engine/foundation/blas`, owed from the earlier session, is built for the first time, on the new SushiRuntime, and committed | `se build` and the unit tests pass on both backends |
| 9.2 | The ten files that include SushiRuntime move to the new names, verbs and `Result` | No `API::` spelling is left |
| 9.3 | The acronym rename owed from the earlier session | The acronym script lists nothing |

The engine's own sweep is not in this plan: 106 `I`-prefixed types, 2 121 constants without `K_`,
three enums with lower-case enumerators and 20 files with `namespace detail`. It is larger than
the three steps above together and gets a work folder of its own in SushiEngine.

### Stage 10: close

| Task | Work | Acceptance criterion |
| --- | --- | --- |
| 10.1 | Manual pages, module READMEs, `docs/guides/INTEGRATION.md`, `DISTRIBUTED_GUIDE.md`, the examples | `check_docs_layout.py` and `check_source_comments.py` report nothing new |
| 10.2 | `REMAINING_WORK.md`: the fourteen reshapes become internal debt or are closed; `cpp-code-style` in SushiSkills gains the builder-refusal rule | The design map and the backlog name no removed symbol |
| 10.3 | The repositories are made public and CI is brought up, by the owner or on the owner's word | The workflows run the checkers and the tests through the CLIs and pass |
| 10.4 | The owner uses and tests the stack | The owner says it is ready |
| 10.5 | The `v0.4.0` proposal, with the entries that drive it | The owner has the proposal; no tag is made without approval |

## Order and what waits on what

The stages run in this order: 0 to 5, then 7, 8 and 9, then 6, then 10. The owner set the
outer order on 2026-10-11: SushiRuntime is broken first, SushiBLAS and SushiAI follow it,
SushiEngine is repaired and built once, then the repositories go public and CI comes up, the
owner tests, and the release follows. SushiEngine is not built on the old SushiRuntime first.

Stages 1 to 5 run inside SushiRuntime. Stage 4 precedes stage 5 because the offload verbs are the
last users of the throwing helpers. Stages 7, 8 and 9 follow in tier order. After them no
consumer names the lower layer, so stage 6 hides it without breaking their source, and their
three test suites check the boundary.

From task 1.1 until stage 8 closes, SushiBLAS and SushiAI do not build against this checkout.

## Decisions that stay with the owner

| Decision | Where it bites |
| --- | --- |
| The list of public modules. Proposed: `core`, `api`, the interop descriptors, and two new ones, `report` for the run report types and `region` for `ResourceRegion`; the engine fills both, so neither can live in `api` | Task 6.1 |
| The engine's own sweep, and when | After stage 9 |
| Every push and the `v0.4.0` tag | Stage 10 |

## Decided by the owner on 2026-10-11

- `sushi_ptr`, `RefCounted` and `make_sushi` stay public, in `core`.
- A consumer is pinned to a tag of SushiRuntime after `v0.4.0`; the sibling checkout stays the
  source until then.
- `Result::value()` on a failure stops the process, and that is the short form an example uses.

## Names chosen here, open to a veto

`OperationID`, `add_rule`, `add_coupled`, `add_dynamic`, `add_task`,
`before_each_step`, `Internal::`, `SchedulerExecutionEngine`, `WorkerLoad`, `DeviceDescription`,
`PeerDescription`.

## Size

51 tasks, each with one build and one test run. A SushiRuntime build and test run took about ten
minutes in the acronym pass, and a SushiBLAS one about forty. The plan has not been estimated
beyond that, and stage 6 is the one whose size is least known.
