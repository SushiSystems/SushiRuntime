# api tests

The tests of the `api` module, built into one program, `sr_api_tests`, which
`modules/surface/api/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_api_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

A test file is in the folder named after the part of `include/SushiRuntime/api/` it tests, and
the `README.md` of that folder holds its section.

| Folder | Tests | Notes |
| --- | --- | --- |
| `data/` | `Buffer` and `State` | [data/README.md](data/README.md) |
| `detail/` | The callable traits, the node emitters and the fault record | [detail/README.md](detail/README.md) |
| `graph/` | `Graph`, `DynamicGraph`, `Region`, `Recorder` and `RunHandle`, in three folders | [graph/README.md](graph/README.md) |
| `runtime/` | `Runtime` and the logging controls | [runtime/README.md](runtime/README.md) |
| `vocabulary/` | `Dynamic` and `LateCount` | [vocabulary/README.md](vocabulary/README.md) |

The three `test_fluent_*.cpp` files test `fluent.hpp`, which sits above those folders, so they
are in this one and the sections below are theirs.

## test_fluent_api.cpp

The file covers the parts of the fluent API that `test_fluent_lifecycle.cpp` does not exercise:

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

## test_fluent_hardening.cpp

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
