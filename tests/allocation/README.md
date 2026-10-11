# Allocation tests

`tests/allocation` holds one test, and `tests/CMakeLists.txt` builds it into its own program,
`sr_allocation_tests`. The test replaces the global `operator new` to count allocations, and a
replacement is program-wide: linked into another program it would count that program's allocations
too and displace a sanitizer's own `operator new`. `sr test` runs the program with the rest;
`sr run sr_allocation_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_steady_state_allocation.cpp

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

1. The value-returning `run()` costs at least one more allocation per tick than the reuse form. The
   extra allocation is the `RunReport`, and it shows that the counter can see the path it measures.
2. The per-tick growth of the reuse form, measured as the count for four times the ticks minus the
   count for one times the ticks, is no more than the per-tick growth of a plain `sycl::queue` that
   submits the identical kernel, in order, on the same device and context from the same thread. What
   the chain adds beyond bare SYCL is the runtime's own cost, and that cost must not scale.
3. On a run forced onto the scheduler (`RuntimeSettings::force_scheduler`), the count does not scale
   with the ticks, for the plain run and for the profiled one. The submission runs on a worker
   thread, so the counter cannot see it, and the count stays flat because it stays zero.

The raw per-tick figures for the chain, the baseline and the forced scheduler are recorded as test
properties, so every number is visible even where it cannot be asserted.

### Sanitizer detection

The preprocessor block detects a build whose sanitizer runtime owns the allocator. It checks both
spellings for each sanitizer, because Clang and IntelLLVM do not agree on which they define: the
`__has_feature` form is native to Clang, the `__SANITIZE_*` macros are the predefined GCC-style
ones, and a toolchain may offer only one of them. A guard that tested a single spelling, and only
for AddressSanitizer, let the ThreadSanitizer lane through. `tests/common/test_main.cpp` uses the
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
   owns. The difference is at least one allocation per tick, and observing it proves that the
   counter reaches the run path and not only the test's own code.
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

The baseline uses the profiling flavour of the in-order queue the chain submits to, so it carries
the cost of profiling as well.
