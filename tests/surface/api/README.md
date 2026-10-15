# api tests

The tests of the `api` module, mirroring `modules/surface/api/`. `tests/surface/api/CMakeLists.txt`
builds them into `sr_api_tests` and `sr_api_benchmarks`, built under `SR_BUILD_BENCHMARKS`.
`sr test` runs them with the rest; `sr run sr_api_tests -- --gtest_filter='<pattern>'` runs one
program alone.

`test_sushiruntime.cpp` builds its own program, `sr_api_allocation_tests`. Its test replaces
the global `operator new` to count allocations, and a replacement is program-wide: linked
into another program it would count that program's allocations too and displace a
sanitizer's own `operator new`.

A benchmark prints its figures and asserts correctness and forward progress; it asserts a
speed only where its section says so.

Three design documents are cited by path in the sections that came from the integration tests:

- `docs/design/GRAPH_REPLAY.md`, the replay layers and their phases.
- `docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md`, the work packages WP-0 to WP-7 and their
  numbered sections.
- `docs/design/ENGINE_BACKBONE_REFACTOR.md`, the backbone items BB-0 to BB-8.

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
| `dynamic_graph_plan_access.hpp` | Reads the composed plan of a `DynamicGraph` and its boundary edges; see its section. |
| `refusing_fill.hpp` | A host fill task whose copy throws on demand, to fail a plan build; see its section. |

## test_fluent.cpp

The file covers the parts of the fluent API that the `Integration_FluentLifecycle` tests do not exercise:

- `run_for(duration)`
- the success path of `run()` and the fields of `RunReport`
- the `add(kernel)` overload that takes a raw `sycl::handler`
- the `add(n, kernel)` range kernel overload
- the pointer-tracked `add(reads, writes, n, kernel)` overload
- `Reads` and `Writes` over `Buffer::region()`, which track a sub-region
- a standalone `Buffer<T>` allocation used in a graph
- `Graph::size()`
- the `Runtime::advanced()` accessor against the context behind the runtime
- `run(0)`

### `TryRunSuccessPath`

`run()` on a non-empty graph must return a successful result with `total_tasks_executed` filled
in. The test proves that the non-throwing path returns a valid `RunReport` and not a
default-constructed one.

### `RunReportFieldsAfterMultiStep`

After a multi-step run, `total_tasks_executed` must equal the tasks per step times the steps, and
`total_duration_ms` must not be negative.

### `RunForRunsAtLeastOneStep`

`run_for(duration)` must run at least one step and then stop. The budget is very short so that the
test does not wait; the test validates the one-step guarantee.

### `RunForRunsMultipleStepsWithinBudget`

With a real budget, `run_for` should complete several steps and the report must reflect them. The
test measures steady-state throughput, not a cold start.

The kernel is warmed before the timing starts. The first dispatch pays for JIT compilation and for
queue and device setup once, and that alone can exceed the 50 ms budget. `run_for` fixes the
deadline before its first step, so an unwarmed kernel overshoots and stops after a single step. In
steady state this trivial kernel costs less than a millisecond, so a warmed run fits many steps.

### `RawHandlerKernelAdd`

`add_untracked(kernel)` takes a function that receives `sycl::handler&` directly, the raw kernel
overload. The kernel must run and produce the expected values.

### `RangeKernelAdd`

`add_untracked(n, kernel)` takes a function that receives `(begin, end, sycl::handler&)`, the range
kernel overload. The function must see `begin = 0` and `end = N` and launch its own work.

### `PointerTrackedAddOrdering`

`add(reads, writes, n, kernel)` is the overload with explicit dependency vectors. Two tasks share one
buffer: a writer, then a reader that squares the values. The dependency tracker must order them
correctly without a `State`.

### `TrackedRangeKernelOrdering`

The tracked range-kernel overload runs the writer and squarer pair of `PointerTrackedAddOrdering`,
but the second task drives the handler itself. The declared `Reads` and `Writes` must still order it
after the writer. An untracked overload would let the square race the write.

### `TrackedRawKernelOrdering`

With the tracked raw-kernel overload the reader owns the whole command group, and its declared
regions still order it after the writer.

### `TrackedNdRangeOrdering`

With the tracked `NdRange` overload a tiled work-group kernel keeps dependency tracking and still
gets an `nd_item`. The kernel is ordered after the writer, and the `nd_item` indexing must cover the
whole global range.

### `SubRegionDisjointWritesThenWholeRead`

Two writers touch disjoint halves of one buffer, so the tracker leaves them unordered. A
whole-buffer reader then overlaps both and must run after each. The test proves sub-region identity
end to end through the fluent layer: disjoint slices are independent, and an overlapping access
still orders. An identity based on the base pointer would either serialize the halves or let the
reader observe an unwritten half.

### `BufferAllocateUseAndReadBack`

A standalone `Buffer<T>` is allocated, seeded on the host, used in a graph task through
`Reads` and `Writes` on its data pointer and read back. The test verifies the lifecycle of owning USM.

### `GraphSizeTracksAdds`

`Graph::size()` tracks the number of `add()` calls made so far, also after a mutation that triggers
a recompilation.

### `ContextAccessorIsConsistent`

`Internal::ContextAccess::context()` must return the `RuntimeContext` the fluent `Runtime` wraps,
and `advanced()` must read through to it. The test verifies both by comparing device counts.

### `RunZeroStepsIsNoOp`

`run(0)` is documented as zero steps. It must return immediately, and the `RunReport` should show no
work done.

The file covers the hardened fluent surface:

- `add_untracked(Extent<2>, kernel)`, a multidimensional per-element map
- `add_untracked(NdRange<2>, kernel)`, a tiled work-group kernel
- `add_rule(Extent<2>, state, kernel)`, a multidimensional stepped stencil
- `add(out, in0, in1, kernel)`, a variadic coupled update that reads three fields
- `add(Reads, Writes, n, kernel)`, the handle-based dependency wrappers
- `add(Extent, In/Out, kernel)`, buffer arguments captured automatically
- `add_rule(Extent with offset, state, kernel)`, an interior-only (halo) update

### `Extent2DPerElementMap`

With `add_untracked(Extent<2>, kernel)` the kernel sees a `sycl::id<2>` that covers the whole box and
writes a row-major linear index, which proves 2-D iteration and indexing.

### `NdRange2DTiledKernel`

`add_untracked(NdRange<2>, kernel)` runs a tiled kernel in the work-group model. Each item writes its
global linear index, and a correct result proves that `nd_item` indexing works.

### `State2DSteppedUpdate`

`add_rule(Extent<2>, state, kernel)` is a multidimensional stepped update. Each step adds the row index to
every cell, so after S steps a cell in row y holds `S*y`.

### `VariadicCoupledThreeInputs`

`add(out, in0, in1, kernel)` is a coupled update that reads three fields, `out` and two others: `next =
cur + a + b`. All reads see data of the previous step (Jacobi).

### `ReadsWritesWrappersOrdering`

With `add(Reads, Writes, n, kernel)` the dependency wrappers must order a writer before a dependent
reader exactly as the raw `void*` vectors do.

### `InOutCapturedArguments`

With `add(Extent, In/Out, kernel)` the captured buffer arguments give the kernel raw pointers and
register the dependency in one step. The kernel computes `dst = src * 2`.

### `InteriorOnlyHaloUpdate`

With `add(Reads, Writes, Extent-with-offset, kernel)` an interior-only update must leave the one-cell
border, the halo, untouched, which proves sub-range iteration. The test uses an in-place `Buffer` so
that the border keeps its initial value deterministically.

## test_sushiruntime.cpp

The file makes the steady-state allocation cost of a tick measurable and then holds it to a shape,
not to a number.

A frame loop runs the same compiled plan thousands of times. Anything the plan allocates per tick is
a `malloc` on the critical path of the simulation thread and a slow drift toward heap fragmentation,
and neither shows in a throughput benchmark that runs the graph once over a large input. The counter
is a replacement global `operator new`, gated by a thread-local flag so that it observes only the
thread that drives the run. Worker threads allocate for reasons of their own (event lists, SYCL
submissions), and their noise would drown the signal.

The assertion is deliberately not "zero", and on Windows it cannot be. SushiRuntime is a shared
library, so the replacement `operator new` of this file sees only allocations made by code compiled
into the test executable, never by code compiled into the DLL. The command-group functions handed to
`sycl::handler::parallel_for` are templates, instantiated where they are written. They are inline in
`SushiRuntime/api/graph/graph.hpp`, so their object code lands in whichever translation unit
includes that header. SYCL's own `StoreLambda()` allocates the stored kernel object on the heap on
every submission (`llvm-sycl/include/sycl/handler.hpp`, `MHostKernel.reset(new
Detail::HostKernel<...>(...))`). The scheduler runs that submission from a worker thread, so the
thread-local flag of the counter never sees it. The chain runs it synchronously from the calling
thread (`modules/engine/execution/source/chain_run.cpp`, `ChainRun::submit_node()`), so the counter does see it. The
chain could never allocate nothing per tick once SYCL's kernel storage is inline. The rule the test
holds is that the chain adds no more allocation than SYCL's own submission of the same kernel
already costs on the same thread, and the baseline measures that cost directly.

The file asserts three relative claims, the last of them twice:

1. The counter can see the path it measures. Where it sees the library, the value-returning `run()`
   costs at least one more allocation per tick than the reuse form, and the extra allocation is the
   `RunReport`. On Windows it does not see the library, and the claim is the narrower one under
   "What the counter sees on Windows".
2. The per-tick growth of the reuse form, measured as the count for four times the ticks minus the
   count for one times the ticks, is no more than the per-tick growth of a plain `sycl::queue` that
   submits the identical kernel, in order, on the same device and context from the same thread. What
   the chain adds beyond bare SYCL is the runtime's own cost, and that cost must not scale.
3. On a run forced onto the scheduler (`RuntimeSettings::force_scheduler`), the count does not scale
   with the ticks, for the plain run and for the profiled one. The submission runs on a worker
   thread, so the counter cannot see it, and the count stays flat because it stays zero.

The raw per-tick figures for the chain, the baseline and the forced scheduler are recorded as test
properties, so every number is visible even where it cannot be asserted.

### What the counter sees on Windows

`K_COUNTER_SEES_THE_LIBRARY` is false on Windows and true elsewhere. A Windows DLL binds its own
`operator new`, so an allocation made by code compiled into `sushiruntime.dll` never reaches the
replacement in this file. On Linux the replacement is program-wide and covers the shared library.

The step loop and the report builder are library code: `run_steps()` of
`modules/surface/api/source/step_run.cpp` calls `fill_run_report()` of `run_report_builder.hpp`,
whose `numa_statistics.push_back()` is the one allocation a fresh `RunReport` costs per tick. The
value form and the reuse form therefore count the same on Windows, 128 allocations over 64 ticks
each on the development machine, and the comparison between them proves nothing there.

What the counter still sees on Windows is the kernel submission. The command-group templates of
`graph.hpp` are instantiated in the test executable, and the chain calls them on the calling
thread. The Windows form of claim 1 is that bare SYCL costs at least one counted allocation per
tick and that the chain's count is no lower than bare SYCL's for the same kernel. A counter that
did not reach the run path would count nothing for the chain.

On Windows the suite therefore proves that the code the executable instantiates on the run path
adds no allocation that scales beyond SYCL's own. It does not prove that the library's step loop,
chain or report builder allocate nothing per tick: an allocation added there passes unseen. Only a
build whose counter sees the library covers those.

### Sanitizer detection

The preprocessor block detects a build whose sanitizer runtime owns the allocator. It checks both
spellings for each sanitizer, because Clang and IntelLLVM do not agree on which they define: the
`__has_feature` form is native to Clang, the `__SANITIZE_*` macros are the predefined GCC-style
ones, and a toolchain may offer only one of them. A guard that tested a single spelling, and only
for AddressSanitizer, let the ThreadSanitizer lane through. `modules/foundation/test_runner/source/test_main.cpp` uses the
same idiom for `SR_TSAN_BUILD`.

### `SkippedUnderSanitizer`

AddressSanitizer and ThreadSanitizer each replace `operator new` and `operator delete` themselves:
ASan to place its redzones and track ownership, TSan to attach its own bookkeeping. A second
replacement in this translation unit measures nothing. It is a duplicate definition of the same
symbols and fails at link, on Windows against ASan and on Linux against `libclang_rt.tsan_cxx.a`.

The suite is skipped under a sanitizer, not adapted. Counting could be kept alive by installing
allocator hooks in place of the replaced `operator new`, but the numbers would then include the
sanitizer's own allocations, and the suite asserts on exact steady-state counts. A test that runs
while it measures something other than what it claims is worse than a test that reports it was
skipped. The target stays in the sanitizer lanes, where it still compiles, links and runs, so the
cost of the guard stays visible.

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

### `allocations_for_reuse_ticks`

`allocations_for_reuse_ticks` runs `ticks` steps and returns how many allocations the calling thread
made. The graph is warmed first, so that compilation and the capacity growth of the first run fall
outside the measured window. The question is what a tick costs in steady state, not what the first
one costs.

### `allocations_for_baseline_ticks`

`allocations_for_baseline_ticks` returns what bare SYCL costs, on the calling thread, to submit the
same per-element kernel `ticks` times, in order, on `queue`. The cost of the chain is judged against
this figure. `queue` must be an in-order queue on the device of the plan, so the function measures
the submission path that `parallel_for` in `graph.hpp` takes, without the chain's dependency waits
and event ledger around it. It is warmed as `allocations_for_reuse_ticks` is, for the same reason:
the one-time cost of the first submission is not the steady state.

### `TheCounterObservesTheCallingThread`

The counter must be able to see what it measures before a claim made with it means anything. A
single vector allocation cannot be missed.

### `ReuseTicksDoNotAllocatePerTick`

The baseline uses the device, the context and the in-order queue that the chain submits to
(the in-order flavour of `Internal::Execution::RuntimeContext::devices()`). It therefore measures what SYCL's
own submission of the identical kernel costs on the calling thread, with none of the chain's
bookkeeping around it.

The test makes the three claims in order:

1. The value form builds a fresh report on every tick, and the reuse form refills one the caller
   owns. Where the counter sees the library the difference is at least one allocation per tick,
   and observing it proves that the counter reaches the run path and not only the test's own code.
   On Windows the test asserts the two comparisons with bare SYCL that "What the counter sees on
   Windows" describes. The property `counter_sees_the_library` records which form ran.
2. The per-tick growth of the chain must not exceed the per-tick growth of bare SYCL submitting the
   same kernel on the same queue. Growth beyond that is the runtime's allocation, not SYCL's, and it
   is the only part the chain controls.
3. The scheduler runs the same kernel from a worker thread, so the counter cannot see any of it. The
   count does not scale because it stays zero.

### `AsyncWaitIntoAReusedReportIsAmortised`

The test makes the same claim on the asynchronous path, which is the one an overlapped frame loop
uses: submit, do unrelated work, settle into a report the loop owns.

### `ProfiledReuseTicksDoNotAllocatePerTick`

The event ledger of a profiled run allocates only for a new shape, never on a later run that replays
the same shape. This is item 3 of section E in
`docs/archive/superpowers/specs/2026-09-15-g1-node-event-records-design.md`.

The baseline uses the profiling flavour of the in-order queue the chain submits to, and reads each
event's timestamps after the wait as the ledger does, so it carries the cost of profiling as well.
On some SYCL backends reading a timestamp allocates, and a baseline that left the event unread
would grow by less than the chain on every tick.

## dynamic_graph_plan_access.hpp

The header is the seam the tests use to read the composed plan of a `DynamicGraph`, which has no
public accessor. `graph/test_dynamic_graph.cpp` and `graph/plumbing/test_region_composer.cpp` include it by
name.

### `DynamicGraphNodeEventsTestAccess`

`DynamicGraphNodeEventsTestAccess` is the friend `dynamic_graph.hpp` names, used only by tests to
reach `DynamicGraph::plan_`. It mirrors `NodeEventRecordsTestAccess` on `Graph`.

### `find_plan_node`, `has_boundary_edge`, `boundary_in_degree`

`find_plan_node` returns the one node of `plan.all_nodes` that carries a name and fails the test
when none or several do; the list is filled only while the graph requests profiling.
`has_boundary_edge` reports whether `from` lists `to` among its cross-region successors, and
`boundary_in_degree` returns how many cross-region edges end at a node. With them a test asserts an
edge of the boundary layer directly and needs no timing.

## refusing_fill.hpp

### `RefusingFill`

`Testing::RefusingFill` is a host task that fills a range, and whose copy constructor throws a
`std::runtime_error` while a flag the test holds is set. Recording moves the task, and every
build of a plan copies it into the node, so the flag decides whether a build throws.
`graph/plumbing/test_step_driver.cpp` and `graph/plumbing/test_region_composer.cpp` include the
header by name.
