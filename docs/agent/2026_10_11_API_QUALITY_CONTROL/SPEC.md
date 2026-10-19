# API refinement: specification

**Status:** Shipped — 2026-10-12. The findings left open are in the backlog.

This continues the quality-control work of this folder. The first pass repaired defects and left
fourteen reshapes of the public surface open in `docs/design/REMAINING_WORK.md`. This pass breaks
the surface once, in `v0.4.0`, and takes SushiBLAS, SushiAI and SushiEngine with it.

## Decisions of the owner

| Question | Decision |
| --- | --- |
| The layer below the fluent API | One surface. Everything below it becomes internal |
| Error model | `Result` only. No exception leaves the public surface |
| How the plan runs | Inline, one session, in hierarchy order |
| Scope | All six findings, in all four repositories |

## What was measured

Counts are from `git grep` over the four checkouts on 2026-10-11.

### The surface

- 148 public headers are installed; 57 are under `api/`.
- The umbrella header reaches 112 of them. 55 of those are not under `api/`.
- 32 fluent headers include a header of `execution/`, `graph/`, `task/`, `scheduler/`,
  `topology/` or `device/`.
- `modules/surface/api` has no source file. Every line of the fluent layer is compiled into its
  consumer.
- 20 public headers outside `distributed/` are reached by nothing: nine of `scheduler/`, five of
  `topology/`, two of `task/`, `core/bit.hpp`, `memory/epoch_reclamation.hpp`,
  `observability/event_timestamps.hpp` and `sycl/sycl_compat.hpp`.
- 97 `[[deprecated]]` attributes stand in 24 files; `runtime_context.hpp` holds 31.

### What the consumers take from below the fluent API

| Consumer | Takes | For |
| --- | --- | --- |
| SushiBLAS | `TaskGraph::add_task`, `reserve_registration`, `compile`, `take_nodes` | Recording a task whose body is `(sycl::queue&, events) -> sycl::event`, running it, discarding it |
| SushiBLAS | `TaskMetadata`, `TaskType`, `OpID`, `_op` | A name and an operation id per task; 72 files include `task_types.hpp` |
| SushiBLAS | `RuntimeContext` | `memory().allocate`, `devices().queue`, `get_execution_engine().execute` |
| SushiAI | `TaskGraph`, `CompiledGraph`, `rearm`, `IExecutionEngine::execute` | Compiling a step once and replaying it |
| SushiAI | `SushiException`, `Errc` | Mapping a failure to its own `Status` in `seam.cpp` |
| SushiEngine | `RunReport`, `NodeEventRecord`, `ResourceRegion`, `Interop` types | Profiling and interop; the rest goes through `API::` |

SushiBLAS already records ten graph primitives through `API::Recorder`, so a domain library can
live on the fluent surface. What the surface lacks for the rest:

1. A task whose body takes the queue and the dependency events and returns one event.
   `raw_kernel` takes `fn(sycl::handler&)`, one command group.
2. A name and an operation id on a node. `NodeOptions` holds a width and nothing else.
3. A value a record call returns that names the rule it recorded.
4. A hook the caller runs before each replay. SushiAI keeps its own list of them beside the plan.
5. A queue for host-side waits, without `RuntimeContext`.

`Recorder::clear()`, the plan cache of `Graph` and `set_profiling()` already cover discarding,
compile-once replay and profiling.

### Names

| Kind | SushiRuntime | SushiBLAS | SushiAI | SushiEngine |
| --- | --- | --- | --- | --- |
| `namespace detail` / `Detail` | 37 / 12 | 0 / 59 | 0 / 43 | 20 / 83 |
| Types with an `I` prefix | 9 | 0 | 0 | 106 |
| Distinct `get_` methods | 29 | 17 | 0 | 6 |
| Umbrella header | `SushiRuntime.h` | `SushiBLAS.h` | `SushiAI.h` | `SushiEngine.hpp` |

In SushiRuntime alone:

- One type has three names: `Observability::RunReport`, `Core::RunReport`, `SushiRuntime::RunReport`.
  `CompiledGraph` has two, and every fluent type is both `API::X` and `SushiRuntime::X`.
- `Errc` is an abbreviation and its enumerators are lower case. `TaskType` enumerators are upper
  snake case.
- 22 constants in public headers have no `K_` prefix; 11 have it.
- `Runtime::rebalancer(bool)` and `set_rebalancer_enabled(bool)` do the same thing.

### Errors

- 66 `throw` statements outside tests and outside the distributed extension, 30 of them in the
  fluent layer: 27 broken preconditions, 11 causes outside the program, 8 refused recordings,
  16 internal, 4 driven by run-time data. The extension holds 14 more.
- No assertion macro exists.
- Every run verb exists twice: `run`, `run_for`, `run_until`, `run_async` and their `try_` forms,
  21 declarations in `step_driver.hpp`.
- `add()` returns the builder, so a refused recording has no return value to carry a failure.

### Weight

`step_driver.hpp` has 967 lines, `recorder.hpp` 923, `region_composer.hpp` 698, `buffer.hpp` 675.
The fluent layer is 10 892 lines of headers.

## Target

1. **One surface.** The installed headers are the fluent ones. The public names live in
   `SushiRuntime::`, each once; `API::` and the cross-namespace aliases go.
2. **A compiled boundary.** The part of `Graph`, `DynamicGraph`, `Recorder`, `StepDriver` and
   `RegionComposer` that names an engine type moves to `modules/surface/api/source/`. A header
   keeps the kernel templates, which need `sycl::handler` and nothing of the engine. This is what
   lets the engine modules become internal, and it is the same work as cutting the header weight.
3. **Author capabilities.** The five missing pieces above are added to `Recorder` and `Graph`, so
   SushiBLAS and SushiAI need no `TaskGraph`.
4. **`Result` only.** A run verb returns `Result`; the `try_` forms go. A builder keeps the first
   refusal of a recording and the next run returns it. A broken precondition is an assertion.
   Each throw site is classified as one or the other.
5. **One verb per kind of node.** `add` records a tracked kernel. A state rule, a coupled rule, a
   dynamic window and an offload each get their own verb; the offload verb belongs to the
   distributed extension.
6. **One set of names.** No `I` prefix, no `get_` prefix, `Detail`, `K_` constants, `ErrorCode`
   with PascalCase enumerators, a snake case umbrella header. The same sweep runs in the three
   consumers.
7. **No deprecated forward.** All 97 go.

## Order

Hierarchy first; one acceptance criterion and one build per step.

| Step | Repository | Work |
| --- | --- | --- |
| 1 | SushiRuntime | Remove the deprecated forwards and their tests |
| 2 | SushiRuntime | Name sweep and the single namespace |
| 3 | SushiRuntime | Author capabilities on `Recorder` and `Graph` |
| 4 | SushiRuntime | `Result` only |
| 5 | SushiRuntime | One verb per kind of node |
| 6 | SushiRuntime | The compiled boundary; engine modules internal and not installed |
| 7 | SushiBLAS | Record through the surface; name sweep; `Result` |
| 8 | SushiAI | Replay through `Graph`; `seam.cpp` reads `ErrorCode` |
| 9 | SushiEngine | The seam of `engine/foundation/blas`, the new names, the engine's own sweep |
| 10 | All | Manual pages, module READMEs, changelog, the `v0.4.0` proposal |

Step 3 comes before step 6 so that the consumers can leave the lower layer before it is hidden.

## Open

- From step 1 until step 8 closes, SushiBLAS and SushiAI do not build against this checkout. The
  consumers read the working tree of their sibling, so a branch does not isolate them. The owner
  decides when the SushiAI work pauses.
- The 14 reshapes listed in `REMAINING_WORK.md` (`IExecutionEngine` with 16 virtuals, `Node` with
  seven responsibilities, the `EpochManager` singleton) stop being public once step 6 lands. They
  stay in the backlog as internal debt and are not part of this pass.
- SushiEngine has 106 `I`-prefixed types and its own acronym rename is still owed. Step 9 is the
  largest of the consumer steps.
- The offload verbs, `Buffer::distributed()` and `OffloadCoordinator` compile in every build: no
  header reads `SR_ENABLE_DISTRIBUTED`. `TaskDispatchMessage` embeds the engine's `TaskMetadata`,
  so shrinking that struct changes the wire format, whose version constant is 2.
