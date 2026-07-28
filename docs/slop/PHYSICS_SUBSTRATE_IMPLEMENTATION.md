# Physics Substrate Seams — Implementation Plan

**Status:** implementation plan for the four gaps raised in
[`PHYSICS_SUBSTRATE_REQUIREMENTS.md`](PHYSICS_SUBSTRATE_REQUIREMENTS.md). That document is the
*request* (what is missing and how badly); this one is the *plan* (what is built, where, and how it
is proved). Branch: `feature/physics-substrate-seams`.

**Scope.** R1, R2, R3 and R5 are built. R4, R6 and R7 are not, and §6 says why.

**Status: complete**, one commit per item. The functional suite goes from 154 to 174 tests, all
passing, and the whole tree passes under AddressSanitizer with no report (§5).

---

## 0. What changed between the request and this plan

The request document proposed two possible designs for R1 (device-driven iteration count) and
recommended a device-resident count slot with a host-node fallback. Reading the dispatch path
closely made both unnecessary.

`API::Graph`'s dynamic overloads do not read the live count when the *graph* is built. They read it
inside the `sycl::handler` closure — and that closure runs on a **worker thread at dispatch**, in
`TaskScheduler::execute_task` (`src/scheduler/scheduler.cpp:700-704`), which is reached only after
the node's `in_degree` hit zero. `in_degree` is decremented by `EventPolling::poll_events`
(`include/SushiRuntime/scheduler/event_polling.hpp:302-335`), which calls `event.wait()` on the
predecessor's event *before* `trigger_successors`.

So by the time a node's kernel closure runs, every predecessor kernel has genuinely completed and
its USM writes are visible host-side. A count a predecessor wrote into shared USM can simply be
**read at submit time**. No second `run()`, no readback stall, no host node in the middle of a
device chain, and no launch at worst-case width.

That collapses the "three shapes" of the request document into one, and it is the shape the engine
plan wanted (option 3, indirect dispatch) in everything but name: the count is resolved from a
device-written location at dispatch, and the compiled plan never changes.

The step-boundary `sized()` provider stays exactly as it is. It answers a different question —
*a count the host already knows* — and the two compose.

---

## 1. R5 — an honest `add()` surface

### The problem, precisely

`API::Graph` has five overloads that call `BackingGraph::add_node(work)` with no read/write region
lists, so the dependency tracker never sees them and they are ordered against nothing:

| Overload | `graph.hpp` |
|---|---|
| `add(std::size_t n, F&& fn)` — per-element | 96 |
| `add(std::size_t n, F&& fn)` — range kernel | 118 |
| `add(F&& fn)` — raw kernel | 138 |
| `add(Extent<N>, F&& fn)` | 167 |
| `add(NdRange<N>, F&& fn)` | 199 |

They sit next to tracked overloads of the *same name*, which is what makes this dangerous: the two
differ only by whether a `Reads(...)`/`Writes(...)` pair was passed, and forgetting one is not a
compile error — it silently selects the untracked overload. `TODO.md` records this as an open
decision (Option A: add tracking; Option B: document it).

### Why "add tracking" cannot mean inference

The obvious reading of Option A — make `add()` infer dependencies — is not implementable. A
per-element callable is `void(std::size_t)` and captures raw USM pointers by value; nothing in the
type system exposes what it touches. The runtime cannot infer it, and pretending otherwise would be
worse than the status quo.

So Option A is delivered in the only shape that is true:

1. **Every kernel shape gains a tracked overload**, so a caller who wants ordering always has one.
   Three are missing today and are added:
   - `add(const Reads&, const Writes&, std::size_t n, range_kernel)`
   - `add(const Reads&, const Writes&, raw_kernel)`
   - `add(const Reads&, const Writes&, NdRange<N>, fn)`
2. **The dependency-blind overloads are renamed `add_untracked(...)`.** They keep working — an
   isolated kernel with no ordering requirement is a legitimate thing to write — but choosing one is
   now a deliberate act that reads as such at the call site and in review.

The asymmetry justifying the rename over documentation is the one the request document names: in a
physics solve a mis-ordered node does not crash and does not look obviously wrong; it produces a
subtly wrong, non-reproducible simulation, and the cause lies in a scheduling decision in another
repository. Converting that into an explicit spelling is cheap.

### Cost

`Region` inherits the whole surface, so it follows for free. The sweep touches the tests, the seven
examples, `INTRODUCTION.md` and `ARCHITECTURE.md`. Pre-v1.0.0, so no deprecation shim and no
changelog entry (`CONTRIBUTING.md` §5).

---

## 2. R2 — fixed-order deterministic reductions

### The contract

The signature matters less than the guarantee, which is stated once and tested:

> The combination order is a function of the element layout alone — not of the worker count, the
> device, the steal pattern, or the work-group size.

This is WP-4 item 1 of `SIMULATION_ENGINE_SUBSTRATE_PLAN.md`, unbuilt until now. WP-4's own analysis
is that on a single architecture determinism *reduces* to fixed-order accumulation, which makes this
primitive the whole determinism story rather than a detail of it. Per **L14** it belongs here: the
order in which values are combined is a property of *how* work is scheduled, not of *what* is being
computed. Built once in the runtime, it is not re-derived — and re-broken — once per domain layer.

### The algorithm

A fixed-tile tree reduction. `TILE` is a compile-time constant (256). Level 0 is the input of `n`
elements; each level folds groups of `TILE` consecutive elements with a **sequential left fold**
into one partial, and levels repeat until one element remains:

```
level 0:  n elements                       (the caller's input)
level 1:  ceil(n / TILE) partials          (scratch)
level 2:  ceil(n / TILE²) partials         (scratch)
...
level k:  1 element                        (the caller's output)
```

Every work-item's fold is `((v_a ⊕ v_{a+1}) ⊕ v_{a+2}) ⊕ …` over a fixed half-open index range, and
the tile boundaries depend only on `n` and `TILE`. Nothing in the schedule can reorder a fold, and
nothing about how many work-items ran concurrently changes which values land in which tile. No
`sycl::reduction`, no floating-point `atomic_ref`, no work-group-size dependence.

Each level is one graph node reading the previous level's scratch range and writing its own, so the
existing region tracker orders them with no manual edges.

**Scratch.** The levels after the first need `Σ ceil(n / TILE^i)` elements of scratch, which is
`n/255` at `TILE = 256` — under half a percent of the input. It is allocated once when the task is
*recorded* and owned by the `Graph`, so the pointer is stable across recompiles and across the
`DynamicGraph` composer rebuilding a region from its recorded specs.

### Segmented reduce

`add_segmented_reduce(values, segment_offsets, output, segment_count, op, identity)` is the shape
the physics actually needs: per-body contact-impulse accumulation and per-vertex accumulation across
the tetrahedra sharing a vertex are both scatter-accumulate, and both feed simulation state that a
rollback must reproduce byte-for-byte.

One node, one work-item per segment, a sequential fold over `[offsets[s], offsets[s+1])`. No
atomics, no cross-work-item communication, and therefore trivially fixed-order.

The honest trade-off: a segment with ten thousand elements and a segment with one are folded by two
work-items of wildly different cost. Determinism is the contract and the caller owns the layout, so
this is documented rather than papered over with a work-stealing split that would break the
guarantee.

### Where it lives

- `api/vocabulary/combine.hpp` — `Sum<T>`, `Min<T>`, `Max<T>`. Device-callable functors; a caller
  may pass any binary functor.
- `api/detail/fixed_order_reduce.hpp` — the level planner, the scratch owner, and the two emitters.
- `api/graph/graph.hpp` — two thin members delegating to the emitters. They must be members (C++
  cannot reopen a class), but the algorithm does not have to be, and is not.

---

## 3. R1 — device-driven iteration count

### The mechanism

`Graph::LateBindings` gains a device count source beside `enabled` and `count`:

```cpp
const std::uint32_t* device_count = nullptr;   // host-readable USM, read at dispatch
std::size_t          device_count_index = 0;
```

`emit_dynamic_per_element`'s submit closure prefers it when set. The read happens on the worker at
dispatch, which §0 established is after every predecessor's event completed and was waited on.

The public spelling is a new `Dynamic` factory:

```cpp
API::sized_from_device(const Buffer<std::uint32_t>& counter, std::size_t index = 0)
```

which composes with `when()` exactly as `sized()` does.

### Three things this must not get wrong

**The producer edge is not a documentation note.** A count read at dispatch is only correct if the
node that writes it is a predecessor. `emit_dynamic_per_element` therefore **adds the counter buffer
to the node's read set itself** when a device count is present. The user cannot forget it, and the
ordering guarantee is structural rather than advisory — the same reasoning as R5.

**Overflow throws, it does not clamp.** If the live count exceeds the capacity the node was compiled
for, the buffers behind it are not big enough and the kernel would run off the end. Clamping would
silently drop contacts — precisely the class of failure this work exists to remove. The submit
closure throws; the scheduler catches it, fails the run, and the message names both numbers.

**Residency is checked at record time.** A device-resident counter is not host-addressable, so
reading it at submit would be undefined behaviour. `sized_from_device` throws immediately on a
`Residency::Device` buffer, at the call site rather than mid-run. The rest of the world may stay
device-resident; only the counter must be `Shared`. This is the one explicit constraint the design
imposes, and it costs four bytes.

**Zero is a real count.** A tick with no contacts is ordinary. A zero-width `parallel_for` is not
portable across SYCL backends, so the closure submits an empty single-task instead — the node still
completes and still releases its successors.

### What it buys

The physics tick becomes one composition. Broadphase writes a pair count, narrowphase iterates
exactly that many pairs and writes a manifold count, the solver iterates exactly that many
manifolds — in one `run()`, with `compile_count()` pinned at 1 while all three counts change every
step. A contact pass sized for a 50 000-contact pile-up dispatches 800 lanes on the tick that has
800 contacts.

---

## 4. R3 — sub-range ordering at the region boundary

### The problem, precisely

The intra-region tracker is already sub-range exact: `Core::ResourceRegion` carries a byte interval
and `DependencyTracker::resolve` orders on `overlaps()`. The **boundary** layer is not.
`DynamicGraph::compute_access` reads `r.base` and discards `offset`/`length`
(`api/graph/dynamic_graph.hpp:454-457`), and `rewire_base` then wires every exit-writer of the
lower-keyed region to every entry-reader of the higher-keyed one, whether or not they touch the same
bytes.

This is recorded in `TODO.md` under *Future work*, where its severity is understated. The intended
mapping is one region per simulation island — the L11 shape, `drop()` a sleeping island, compose a
waking one — but islands are **disjoint index ranges of one body column**, not separate
allocations. With whole-allocation boundary keys, every island is ordered against every other island
through that shared buffer and the parallelism the region model exists to provide is gone.

The alternatives are worse. Per-island allocations mean a copy on every island merge and split, and
islands merge and split constantly — that is what contact *does*.

### The change

`BaseAccess`'s four pin lists become lists of `{Node*, std::vector<ResourceRegion>}` instead of bare
`Node*`. `compute_access` records, per node per base, the exact regions that node reads and the
exact regions it writes. `rewire_base` adds a cross edge only when the two pins' regions actually
overlap.

The entry/exit *classification* needs no change: it is derived from intra-region edges, and those
were already produced by the sub-range-exact tracker. Two nodes writing disjoint halves of one
buffer in the same region have no intra edge between them, so both are already correctly entry pins
and exit pins.

### Where the conservatism goes

The producer threading in `rewire_base` is the subtle part. Today a writing region *replaces*
`last_writer`. With sub-ranges it must not: a region that writes bytes 0–99 does not shadow an
earlier writer of bytes 100–199, which must stay live for a later region that overlaps it.

The rule taken is: **a live writer is retired only when a single new write region fully contains
it.** Retiring on the *union* of several new regions would also be correct and would retire more,
but exact interval-union arithmetic is real complexity for a case that does not arise in the target
workload (islands are contiguous slices). Erring the other way keeps a pin live slightly too long,
which can only add an edge, never drop one.

That is the invariant this section rests on and the reason the change is safe to make: **an extra
cross edge costs parallelism; a missing one is a data race.** Every simplification here is chosen to
fall on the first side.

---

## 5. Proof obligations

Each item lands with the test that would fail without it.

| Item | Test | What would fail without the change |
|---|---|---|
| R5 | `test_fluent_api.cpp` — the three new tracked overloads order a write-then-read pair correctly | The read observes the pre-write value |
| R2 | `test_fixed_order_reduce.cpp` — the same input reduced with the worker count varied from 1 to `MAX_WORKER_THREADS` is bit-identical, for a float input whose sum is order-sensitive | Bit patterns differ run to run |
| R2 | segmented reduce over ragged segments, including empty ones | — |
| R1 | `test_device_count.cpp` — node 1 writes a count, node 2 iterates exactly that many elements, in one `run()`, with the count varying every step and `compile_count()` staying at 1 | Node 2 iterates the previous step's count, or the capacity |
| R1 | a count exceeding capacity fails the run with a message naming both numbers | Silent truncation |
| R3 | `test_dynamic_graph.cpp` — two regions writing disjoint element ranges of one buffer are not ordered against each other; overlapping ranges still are | The disjoint pair serializes |

All under `sr test --suite functional`, which goes from 154 to 174 tests, all passing.

**AddressSanitizer: 174/174, no report.** This took making the lane work at all, which is worth
recording because the first read of the failure was wrong.

`sr build --type asan` failed at configure with `clang_rt.asan_dynamic.lib: no such file`, which
looked like "the SYCL toolchain ships no compiler-rt on Windows". It does ship it — the bundle in use
was simply seven months old (2025-12-29, clang 21, no `lib/clang/21/lib/` at all). The current
nightly carries `lib/clang/23/lib/windows/clang_rt.asan_dynamic-x86_64.lib`, so the fix was a newer
bundle, not the 899 MB `clang+llvm-21.1.8` tarball that grafting a matching runtime would have cost —
and grafting would have been a version gamble on the ASan ABI besides.

Three further things blocked the lane, all now fixed in the CLI (`fix(cli): make the AddressSanitizer
lane work on Windows`): `-fsanitize=address` had to become `-Xarch_host -fsanitize=address` because
`-fsycl` also compiles for SPIR-V, where there is no ASan; MSVC's STL container annotations had to be
turned off because `stl_asan.lib` ships with a Visual Studio component rather than the compiler; and
the benchmark suite had to leave the ASan tree because it replaces `operator new`/`delete` to count
allocations, which is what ASan replaces.

TSan remains Linux-only — it has no Windows support upstream and
`cmake/Sanitizers.cmake:17` skips it for `WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang"`.

Also run, before the sanitizer lane existed: the concurrency-sensitive tests (`DynamicGraph`,
`DeviceCount`, `FixedOrderReduce`, `FluentAPI` — 42 cases) under `sr test --repeat 15`, with no
failures. That hunts flakiness rather than memory errors, so it complements ASan rather than
standing in for it.

One test deserves its own note: `DisjointRangeRegionsAreNotOrdered` asserts the *absence* of an edge,
which nothing in the public API can observe directly, so it proves it behaviourally — two region
tasks rendezvous on a shared counter with a bounded deadline and each records whether it saw the
other arrive. It was verified to fail when the overlap test is temporarily coarsened back to
allocation granularity, so it is not a test that would pass either way.

---

## 6. Deliberately not in this change

- **R4 — asynchronous step.** `run()` blocking is correct for a fixed-step simulation thread; what
  it costs is *overlap* with the render/audio extract. Making it asynchronous means owning a driver
  thread and re-deciding how cancellation, the non-reentrancy guard
  (`ExecutionEngine::Impl::run_in_progress`) and the completion latch interact. That is its own
  change with its own risk, and mixing it into a correctness-focused branch would make both harder
  to review. PERF, and it gates nothing.
- **R6 — late-bound base offset.** Convenience, not capability: a graph-coloured constraint slice is
  addressable today through an index-indirection buffer at one extra load per element. It is small
  enough to fold into a later change and does not justify widening this one.
- **R7 — `ThreadLocalMagazine` teardown.** Already tracked as `TODO.md` #29, a v1.0.0 correctness
  blocker in its own right. Nothing about the physics changes its priority.
