# USM allocation: one seam

**Status:** approved 2026-08-18. Closes BB-6 item 4 (the pool decision), which
`ENGINE_BACKBONE_REFACTOR.md` lists as required before the v1 freeze.

## The problem, measured

The runtime has two USM allocation paths and only one of them is used.

| | Public path | Pooled path |
|---|---|---|
| Entry | `RuntimeContext::malloc_shared_on` / `malloc_device_on` | `RuntimeContext::get_allocator()` |
| Mechanism | direct `sycl::malloc_*` plus an ownership map | per-thread slots, magazines, remote-free Treiber stack |
| Locking | a process-global mutex per allocation | lock-free |
| Size limit | none | 4 KB — buckets 128 / 256 / 1024 / 4096 |
| Callers | the entire fluent API | two benchmarks |

`Buffer` and `State` reach `malloc_shared_on` directly, so `BitmaskMemoryPool`,
`ThreadLocalMagazine` and everything under them are unreachable from the public
API. The pool has exactly one instantiation in the tree, inside `USMManagerImpl`.

**The deciding question was whether USM allocation happens during a run, since
that is the only workload pooling would amortise.** It does not. Instrumenting
all four allocation entry points and running representative graphs:

```
static graph
  Runtime::create()                    allocs +0
  2 buffers + 1 state                  allocs +4
  graph description                    allocs +0
  first run (compiles)                 allocs +0
  1000 further steps                   allocs +0

add_reduce
  add_reduce description                allocs +1     (the partials column)
  first run                             allocs +0
  500 further steps                     allocs +0

dynamic graph, streaming
  32 regions streamed in + run          allocs +0
  20 drop + re-add + run cycles         allocs +0
```

USM is allocated once per `Buffer`/`State` the caller constructs, at setup, and
never again — not per step, and not when regions stream in and out, because a
region *describes work over* buffers the caller already owns rather than
allocating any. Total allocations in a session are the number of buffers the
caller makes: tens, at setup.

Two conclusions follow. Pooling has no workload here; and the global mutex on the
public path, which looked like the real scalability defect, is taken a handful of
times at startup and is not one.

The measurement was taken on a single-device CPU backend. Device count does not
change the shape of the result — allocation is still one per buffer at setup —
but the GPU numbers are the owner's to confirm.

## Decision 1 — delete the parallel path

Remove, entirely:

- `include/SushiRuntime/memory/memory_pool.hpp` (`BitmaskMemoryPool`,
  `ThreadLocalMagazine`, `MemoryPage`, `ThreadPoolSlot`, the flush callback)
- `include/SushiRuntime/memory/usm_allocator.hpp` (`USMAllocator`,
  `IAllocatorStrategy` and its `Shared`/`Device`/`Host` strategies,
  `create_allocator`)
- `src/memory/usm_allocator.cpp` (`USMManagerImpl`, `LockFreePageMap`, the
  allocator registry)
- `RuntimeContext::get_allocator()`, both overloads
- `tests/functional/unit/test_memory_pool.cpp`,
  `tests/benchmark/performance/test_allocator_perf.cpp`,
  `tests/benchmark/stress/test_memory_stress.cpp`
- the corresponding includes in `SushiRuntime.h` and `runtime_context.hpp`

Not "moved to an internal header". Moving it hides dead code rather than removing
it, and leaves a reader to discover for themselves that nothing calls it. The
ideas worth keeping — per-thread slots, the remote-free stack that avoids the
foreign-pointer flush spike — stay legible in git history, which is where a
design that has no current use belongs.

This deletes three of the smaller items on the debt list along with it: the
4 KB bucket ceiling, `__builtin_clzll` bypassing the project's own MSVC shim, and
`LockFreePageMap` masking 50 virtual-address bits (which aliases on LA57 hosts).
All three live only in the removed files.

**This is a public API break.** `USMAllocator`, `IAllocatorStrategy` and
`create_allocator` are `SUSHIRUNTIME_API` in installed headers. The project is
0.2.0, pre-freeze, and has no out-of-tree consumers; SushiEngine references none
of them. Recorded in the changelog under Removed, and the version's minor bump
carries it.

### What is kept

`include/SushiRuntime/memory/epoch_reclamation.hpp` stays. It is not part of the
pool: `src/graph/dependency_tracker.cpp` and `src/scheduler/scheduler.cpp` both
use it, so EBR is on the live path.

`Regression_Sentinel.MagazineFlushSurvivesRuntimeTeardown` stays, renamed. Its
premise is already wrong — it allocates with `rt.buffer<int>(4096)`, which goes
down the public path and populates no magazine — but what it actually exercises
is a thread that outlives the `Runtime` and unwinds after it, which still matters
because EBR keeps thread-local state. It becomes
`Regression_Sentinel.AThreadOutlivingTheRuntimeTearsDownCleanly`, with the
comment corrected to say what it guards.

## Decision 2 — no replacement interface

An abstraction is not introduced in the pool's place. One implementation with no
second implementation asked for is a speculative seam: it costs indirection and a
reader's attention, and buys an extension point nobody has requested. If a
workload with high-frequency small USM allocation ever appears, the measurement
above is the trigger to design for it then, against real numbers.

The four `RuntimeContext::malloc_*` entry points remain the single seam.

## Decision 3 — extract `Memory::UsmRegistry`

The SOLID gain is not in the pool; it is in what `RuntimeContext` currently mixes.
It manages queues, holds the device registry, allocates USM, and tracks which
device owns each pointer. The last of those is separable and is the only one with
its own invariant.

`Memory::UsmRegistry` owns:

- the `void* → device_index` map and the mutex guarding it
- `record(ptr, device_index)` on allocation, `take(ptr)` on free (erase and
  return the owner in one locked step, which the free path already needs)
- `owner_of(ptr)`, backing `RuntimeContext::context_index_for_ptr`, which the
  cross-context guard calls on a node's first dispatch

`RuntimeContext` keeps the SYCL calls and delegates ownership bookkeeping. The
type has one invariant — a pointer is recorded from allocation until it is freed,
exactly once — which makes it directly unit-testable, unlike today's version
where testing ownership means standing up a `Runtime`.

### Data flow

```
Buffer<T> ctor
  └─ RuntimeContext::malloc_shared_on(bytes, device)
       ├─ sycl::malloc_shared(bytes, queue_for(device))
       └─ UsmRegistry::record(p, device)

node first dispatch (cross-context guard)
  └─ RuntimeContext::context_index_for_ptr(p)
       └─ UsmRegistry::owner_of(p)

Buffer<T> dtor
  └─ RuntimeContext::free(p)
       ├─ UsmRegistry::take(p)  → device index
       └─ sycl::free(p, queue_for(device))
```

## Testing

- The existing suites are the regression net: 217 functional, 21 benchmark,
  2 package. Deleting the pool must not move any of them.
- `Unit_UsmRegistry`, new: record/owner_of/take round trip, `take` twice reports
  the pointer as unknown, `owner_of` on an unrecorded pointer, and concurrent
  record/take from several threads under ASan.
- `Integration_CrossContextGuard` already covers `owner_of`'s real consumer and
  must stay green.
- The ASan lane runs as well, since this touches allocation and teardown.

## Out of scope

Bucketed pooling for large allocations, NUMA-aware page binding (the removed
allocator's `aligned_alloc_shared` never bound anything either), and any change to
how `Buffer`/`State` allocate. This work removes a path and extracts a
responsibility; it does not change what the public API does.
