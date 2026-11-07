# api tests

The tests of the `api` module, mirroring `modules/surface/api/`. `tests/surface/api/CMakeLists.txt`
builds them into `sr_api_test_objects`, linked into `sr_surface_tests`, and into `sr_api_benchmarks` under
`SR_BUILD_BENCHMARKS`.
`sr test` runs them with the rest; `sr run sr_surface_tests -- --gtest_filter='<pattern>'` runs one
program alone.

`test_sushiruntime.cpp` builds its own program, `sr_api_allocation_tests`. Its test replaces
the global `operator new` to count allocations, and a replacement is program-wide: linked
into another program it would count that program's allocations too and displace a
sanitizer's own `operator new`.

A benchmark prints its figures and asserts correctness and forward progress; it asserts a
speed only where its section says so.

Three design documents are cited by path in the sections that came from the integration tests:

- `docs/design/GRAPH_REPLAY/README.md`, the replay layers and their phases.
- `docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN/`, the work packages WP-0 to WP-7 and their
  numbered sections.
- `docs/design/ENGINE_BACKBONE_REFACTOR/`, the backbone items BB-0 to BB-8.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

The sections of a test file in a subfolder are in that folder's `README.md`:

| Folder | Tests |
| --- | --- |
| [`data/`](data/README.md) | The tests of `Buffer` and `State`, the handles of `api/data/` |
| [`detail/`](detail/README.md) | The tests of the callable traits, the node emitters and the fault record of `api/detail/` |
| [`graph/`](graph/README.md) | The tests of `Graph`, `DynamicGraph`, `Region`, `Recorder`, `PlanView` and `RunHandle` |
| [`graph/plumbing/`](graph/plumbing/README.md) | The tests of the builder surface, the region composer and the step driver |
| [`interop/`](interop/README.md) | The tests of importing and exporting memory |
| [`runtime/`](runtime/README.md) | The tests of `Runtime`, its configuration, its errors and its logging controls |
| [`vocabulary/`](vocabulary/README.md) | The tests of `Dynamic` and `LateCount`, the late-bound words of `api/vocabulary/` |

## Helpers

The headers below are shared by tests of this module only, so they sit in this folder.

| Header | What it does |
| --- | --- |
| `refusing_fill.hpp` | A host fill task whose copy throws on demand, to fail a plan build; see its section. |
| `hooked_copy_task.hpp` | A host task whose next copy runs a hook the test arms, to act inside a plan build; see its section. |
| `run_recorder.hpp` | Adds a recording observer to a runtime and reads the observations of its runs; see its section. |

## compile_api_log_category.cpp

The file includes `api_log_category.hpp` and nothing else, so the header carries every include it
needs. The build compiles it and the test runner lists nothing from it. The category has no promise
a test could break: its name and project are constants.

## test_fluent.cpp

The file covers the fluent surface the other files of this folder reach only in passing. A test whose
kernels have a host form runs small on a host runtime (`Small_FluentAPI`, `Small_FluentGraphSize`,
`Small_FluentHardening`, `Small_FluentLifecycle`). A test that drives a raw handler, a range kernel with a
handler, an `NdRange` or the wall clock runs medium on the program's runtime (`Medium_FluentAPI`,
`Medium_FluentHardening`). Every test fills its buffers through `TestSupport::fill_with` and compares the
whole result with a vector, so no body loops.

| Test | What it proves |
| --- | --- |
| `RunReportFieldsAfterMultiStep` | After a multi-step run, `total_tasks_executed` is the tasks per step times the steps and the report is successful. |
| `RunForRunsAtLeastOneStep` | `run_for` with a zero budget still runs one step. The multi-step case needs a clock; RW-111 holds it. |
| `RunZeroStepsIsNoOp` | `run(0)` returns at once, reports no work and leaves the state as it was. |
| `RawHandlerKernelAdd` | `add_untracked(kernel)` takes a function that receives `sycl::handler&` directly. |
| `RangeKernelAdd` | `add_untracked(n, kernel)` takes a function that receives `(begin, end, sycl::handler&)` and sees `begin = 0`, `end = N`. |
| `PointerTrackedAddOrdering` | `add(reads, writes, n, kernel)` over `BufferRegion` keys orders a writer before the reader that squares its values. |
| `TrackedRangeKernelOrdering` | The tracked range-kernel overload, which drives the handler itself, is ordered after the writer by its declared `Reads` and `Writes`. |
| `TrackedRawKernelOrdering` | The tracked raw-kernel overload, which owns the whole command group, is ordered after the writer. |
| `TrackedNdRangeOrdering` | The tracked `NdRange` overload keeps dependency tracking, gets an `nd_item` and covers the whole global range. |
| `SubRegionDisjointWritesThenWholeRead` | Writers of the two halves of one buffer stay unordered, and a whole-buffer reader runs after both: sub-region identity end to end. |
| `BufferAllocateUseAndReadBack` | A standalone `Buffer<T>` seeded on the host is used through its data pointer in a task and read back. |
| `BufferMoveLeavesSenderEmpty` | A moved-from `Buffer` is empty and the receiver holds the allocation. |
| `GraphSizeTracksAdds` | Rows of zero, one and two `add_rule` calls: `Graph::size()` equals the number made. |

### Multidimensional, variadic, wrapper and captured forms

| Test | What it proves |
| --- | --- |
| `Extent2DPerElementMap` | `add_untracked(Extent<2>, kernel)` sees a `sycl::id<2>` that covers the whole box and writes a row-major index. |
| `Small_NdKernelMath.SquareInPlaceSquaresOneElement` | `TestSupport::square_in_place()`, the arithmetic of the tracked `NdRange` kernel, squares the one element it is given. |
| `Small_NdKernelMath.WriteIndexWritesTheElementsOwnIndex` | `TestSupport::write_index()`, the arithmetic of the raw and range handler kernels, writes an element's own index and no other. |
| `Small_NdKernelMath.WriteDoubleIndexWritesTwiceTheIndex` | `TestSupport::write_double_index()`, the arithmetic of the raw handler kernel, writes twice an element's index into it. |
| `Small_NdKernelMath.StoreValueAndAddValueChangeOneElement` | `TestSupport::store_value()` and `add_value()`, the arithmetic of the dispatch, command-group and queue-body kernels, change the one element they are given. |
| `Small_NdKernelMath.StoreRangeWidthStoresEndMinusBegin` | `TestSupport::store_range_width()`, the arithmetic of the range kernel, stores the width of the range. |
| `Small_NdKernelMath.StoreDoubledAndStoreSuccessorMapOneElement` | `TestSupport::store_doubled()` and `store_successor()`, the arithmetic of the two-kernel queue bodies, map one element. |
| `Small_NdKernelMath.StoreOrdinalStoresTheIndexPlusOne` | `TestSupport::store_ordinal()`, the arithmetic of the queue-body kernel, stores an element's index plus one. |
| `Small_NdKernelMath.WriteLinearIndexWritesTheRowMajorIndex` | `TestSupport::write_linear_index()`, the index math of the tiled kernel, writes the row-major index at column x, row y. |
| `NdRange2DTiledKernel` | `add_untracked(NdRange<2>, kernel)` runs a tiled kernel in the work-group model; each item writes its global linear index. |
| `State2DSteppedUpdate` | `add_rule(Extent<2>, state, kernel)` adds the row index to every cell each step, so row y holds `S*y` after S steps. |
| `VariadicCoupledThreeInputs` | `add_coupled(Coupled(out, a, b), kernel)` computes `next = cur + a + b` from the data of the previous step. |
| `ReadsWritesWrappersOrdering` | `Reads` and `Writes` wrappers order a writer before a dependent reader as the raw key vectors do. |
| `InOutCapturedArguments` | `add(Extent, In, Out, kernel)` hands the kernel raw pointers and registers the dependency in one step. |
| `InteriorOnlyHaloUpdate` | An interior-only `Extent` with an origin leaves the one-cell border untouched. |

### State replay, run_until, coupled fields, recompilation and errors

| Test | What it proves |
| --- | --- |
| `StateReplayIncrement` | One rule replayed 64 times double-buffers: every cell holds 64 and the report counts 64 tasks. |
| `ReplayAcrossCalls` | A second `run()` resets the node state the first consumed: 10 steps then 5 give 15. |
| `RunUntilStopsOnCondition` | `run_until` checks its condition before each step against the state the previous step produced and stops at 20. |
| `CoupledFieldsAreJacobi` | Both rules of a coupled pair read the previous step's data, so `v` is 10 and `u` is 45 after 10 steps. |
| `MutationRecompiles` | Adding a rule recompiles the plan; the state of the first rule continues and the new one starts from its seed. |
| `EmptyGraphIsInvalid` | Running an empty graph returns `InvalidGraph` and throws nothing. |
| `DanglingResourceIsReported` | A graph that outlives a `Buffer` it recorded returns `DanglingResource` instead of dispatching against released memory. |

## test_sushiruntime.cpp

The file makes the steady-state allocation cost of a tick measurable and then holds it to a shape,
not to a number.

A frame loop runs the same compiled plan thousands of times. Anything the plan allocates per tick is
a `malloc` on the critical path of the simulation thread and a slow drift toward heap fragmentation,
and neither shows in a throughput benchmark that runs the graph once over a large input. The counter
is a replacement global `operator new`, gated by a thread-local flag so that it observes only the
thread that drives the run. Worker threads allocate for reasons of their own (event lists, SYCL
submissions), and their noise would drown the signal.

The assertion is deliberately not "zero", and on Windows the counter cannot see the library at all.
SushiRuntime is a shared library, so the replacement `operator new` of this file sees only
allocations made by code compiled into the test executable, never by code compiled into the DLL.
The command-group functions handed to `sycl::handler::parallel_for` are templates, instantiated
where they are written, and SYCL's own `StoreLambda()` allocates the stored kernel object on the heap
on every submission. The scheduler runs that submission from a worker thread, so the thread-local
flag never sees it. The chain runs it synchronously from the calling thread
(`modules/engine/execution/source/chain_run.cpp`, `ChainRun::submit_node()`), so the counter does see
it, and the chain can never allocate nothing per tick.

The file asserts three claims about the public surface:

1. The value-returning `run()` costs at least one more allocation per tick than the reuse form, and
   the extra allocation is the `RunReport`. Observing it also proves that the counter reaches the
   run path and not only the test's own code.
2. On a run forced onto the scheduler, the count for four times the ticks stays below four times the
   count for one, for the plain run and for the profiled one. The submission runs on a worker thread,
   so the count stays flat because it stays zero.
3. On the asynchronous path, `run_async()` waited into one report the caller owns is amortised the
   same way.

The claim that the chain adds no more allocation per tick than bare SYCL's own submission of the
same kernel on the same queue is not tested. Its baseline needs the in-order flavour of the device
queue, which `advanced().queue()` does not hand out, and the event timestamps the profiled form
reads; RW-113 holds it.

### The counter on Windows

`K_COUNTER_SEES_THE_LIBRARY` is false on Windows and true elsewhere. Every test of
`Medium_SteadyStateReuse` skips where it is false, since a count of zero would pass each claim
vacuously. On Windows only `TheCounterObservesTheCallingThread` runs. RW-43 holds proving the
library's allocations there.

The step loop and the report builder are library code: `run_steps()` of
`modules/surface/api/source/step_run.cpp` calls `fill_run_report()` of `run_report_builder.hpp`,
whose `numa_statistics.push_back()` is the one allocation a fresh `RunReport` costs per tick.

### Sanitizer detection

The preprocessor block detects a build whose sanitizer runtime owns the allocator. It checks both
spellings for each sanitizer, because Clang and IntelLLVM do not agree on which they define: the
`__has_feature` form is native to Clang, the `__SANITIZE_*` macros are the predefined GCC-style
ones, and a toolchain may offer only one of them. A guard that tested a single spelling, and only
for AddressSanitizer, let the ThreadSanitizer lane through.
`modules/foundation/test_runner/source/test_main.cpp` uses the same idiom for `SR_TSAN_BUILD`.

AddressSanitizer and ThreadSanitizer each replace `operator new` and `operator delete` themselves:
ASan to place its redzones and track ownership, TSan to attach its own bookkeeping. A second
replacement in this translation unit measures nothing. It is a duplicate definition of the same
symbols and fails at link, on Windows against ASan and on Linux against `libclang_rt.tsan_cxx.a`.
Under a sanitizer the file therefore defines no test and no replacement; the target still compiles,
links and runs in the sanitizer lanes, so the cost of the guard stays visible. Counting could be
kept alive by allocator hooks, but the numbers would then include the sanitizer's own allocations.

### Counter state

The counting flag and the counter are constant-initialized, so both are live before any dynamic
initialization that might allocate. The flag is per thread. The counter is shared only because the
counting thread is the only one that ever touches it.

### Replaced operator new

Every replaced form is paired with its matching deallocation form, so nothing allocated through a
replaced form can be freed by a different allocator. The over-aligned forms, which take
`std::align_val_t`, are deliberately not replaced. Leaving the whole aligned family at its default
keeps that pairing intact as well. The cost is that aligned allocations are not counted, and the run
path makes none.

### The measuring helpers

`allocations_for_reuse_ticks`, `allocations_for_value_ticks` and `allocations_for_async_ticks` run
the graph for a warm-up first, so that compilation and the capacity growth of the first run fall
outside the measured window, and return how many allocations the calling thread made over `ticks`
more runs. The question is what a tick costs in steady state, not what the first one costs.
`is_amortised` says whether the count for four times the ticks stayed below four times the count for
one.

### Tests

| Test | What it proves |
| --- | --- |
| `TheCounterObservesTheCallingThread` | A single vector allocation on the counting thread moves the counter. The counter must see what it measures before a claim made with it means anything. |
| `ARunIntoAReusedReportSavesAnAllocationPerTick` | Claim 1. |
| `ASchedulerForcedRunDoesNotAllocatePerTick` | Claim 2, for the plain run. |
| `AProfiledSchedulerForcedRunDoesNotAllocatePerTick` | Claim 2, with a run observer added: the event ledger of a profiled run allocates only for a new shape, never on a later run that replays it. |
| `AnAsyncWaitIntoAReusedReportIsAmortised` | Claim 3. |

The reuse tests skip on AdaptiveCpp, whose submission allocates a different number of times per call,
so the counts they compare are not stable there.

## refusing_fill.hpp

### `RefusingFill`

`Testing::RefusingFill` is a host task that fills a range, and whose copy constructor throws a
`std::runtime_error` while a flag the test holds is set. Recording moves the task, and every
build of a plan copies it into the node, so the flag decides whether a build throws.
`graph/plumbing/test_step_driver.cpp` and `graph/plumbing/test_region_composer.cpp` include the
header by name.

## hooked_copy_task.hpp

### `HookedCopyTask`

`Testing::HookedCopyTask` is a host task that does nothing when it runs. It shares a
`std::function<void()>` slot with the test; the first copy made after the test fills the slot
empties it and calls the hook once. Recording moves the task and a plan build copies it, so a hook
armed after recording runs inside the next build. `graph/plumbing/test_plan_cache.cpp` and
`graph/plumbing/test_region_composer.cpp` include the header by name.

## run_recorder.hpp

`Testing::RunRecorder` adds a `RecordingObserver` to a `Runtime` and removes it when it is
stopped or destroyed. Adding the observer turns device timing on, so every graph of the runtime records
timing from its next run, and the recorder returns the `RunObservation` of each run: `count()`, `last()`
and `runs()`. `pairing_of()` finds the `ClockPairing` of a device in an observation, and `steady_ns()`
puts a record's time on the steady clock, which a host record already is on. The tests that check node events, names or widths read an observation through it.
