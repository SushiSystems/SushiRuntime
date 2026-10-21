# WP-4: Determinism via fixed reduction order

**Problem:** the default scheduler is nondeterministic: work stealing, adaptive
polling, and the always-on rebalancer (findings 5.3) vary the dispatch order run-to-run.
The owner needs **server-authoritative replay/debug determinism on the same
architecture** (L3): re-running recorded inputs reproduces the same state bit-for-bit.

**Key insight (this is the cheap path).** The compiled DAG already guarantees a
**dependency-correct** result regardless of dispatch order (findings 1). The *only* source
of run-to-run variation in the **result** is **order-sensitive floating-point
accumulation** (a parallel reduction whose combination order follows the schedule).
Therefore, on a single architecture, determinism does **not** require a
single-threaded engine or disabling work stealing. It requires:

1. **Fixed-order reduction primitives.** Every sum/min/max/dot/accumulation that
   feeds simulation state uses a deterministic combination order (a fixed-layout
   tree reduction), independent of how many workers ran it or in what order.
2. **No schedule-order leakage.** An audit/lint that no system writes state in a
   way that depends on the order sibling systems ran (e.g. "first writer wins"
   races, non-commutative in-place accumulation). The region-overlap tracker (WP-2)
   makes genuine write-write conflicts explicit, which helps this audit.
3. **Deterministic RNG.** Per-entity / per-cell streams seeded by stable id, not by
   a global counter touched in schedule order.

With those, **work stealing and the rebalancer can stay on** in the deterministic
mode, because they do not affect the result: only the timing. A separate
fixed-order `IExecutionEngine` (the seam at `execution_engine.hpp:121`) becomes a
**fallback/validation tool** (run the same frame single-threaded and diff) rather
than the production path. This preserves throughput (L1) while meeting L3.

**Caveat to honor:** GPU reductions are the hard case: a device `reduce` is not
guaranteed fixed-order. Deterministic state-affecting reductions on GPU must use an
explicit fixed-order kernel (segmented tree reduce over a fixed layout), accepting a
modest performance cost on exactly those reductions. Non-state-affecting reductions
(telemetry, diagnostics) need not be deterministic.

**Files:** a deterministic reduction header in the fluent API; rebalancer made
opt-in (findings 5.3) so a *strict* mode can disable even timing nondeterminism if ever
needed; optional `execution/deterministic_engine.{hpp,cpp}` as the validation
fallback. Cross-architecture determinism is **explicitly out of scope** (L3), so no
fixed-point math is introduced.

**Acceptance:** a physics+ECS frame, run twice under the normal work-stealing
engine, produces bit-identical state; a recorded input stream replays to the same
state; the single-threaded validation engine agrees with the parallel one.
