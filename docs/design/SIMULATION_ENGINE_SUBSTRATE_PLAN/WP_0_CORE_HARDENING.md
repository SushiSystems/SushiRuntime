# WP-0: Core hardening, the blockers of the findings

**Scope:** findings 3.1 event-list overflow, findings 3.2 error isolation, findings 3.3 cancellation,
findings 3.4 deque overflow, findings 3.5 magazine teardown. Optionally findings 3.6.

**Design:**
- `ActiveEventList`: keep the inline 256 fast path, spill to a heap vector beyond it
  (same pattern as `SmallEventVector`, `node.hpp:56`). `push` never fails.
- Cancellation + error isolation share one run-state object owned by the engine
  per run: `{ atomic<bool> cancelled; atomic<bool> failed; exception_ptr err; }`.
  Workers check `cancelled` in the dispatch loop; a task exception sets `failed`/
  `err` for *this run* and stops new dispatch **without** latching the pool-level
  `shutdown_requested`. `arm_for_execution` resets the run-state.
- Deque: spill-on-overflow or grow; remove the silent-drop branch.
- Magazine: liveness flag checked in the destructor.

**Files:** `event_polling.hpp`, `scheduler.cpp`, `task_scheduler.hpp`,
`execution_engine.cpp`, `memory_pool.hpp`, `usm_allocator.*`.

**Risks:** the spill paths must stay lock-free/contention-light on the hot path; the
run-state must be cache-friendly (one cache line) to avoid false sharing across
workers.

**Acceptance:** (a) 1-worker, 10⁵-leaf fan-out completes; (b) a throwing kernel on
step K leaves the `Runtime` usable for a new graph; (c) a second thread cancels a
`run_until` and `execute()` returns within a task quantum; (d) ASan clean for an
app thread that outlives `RuntimeContext`.
