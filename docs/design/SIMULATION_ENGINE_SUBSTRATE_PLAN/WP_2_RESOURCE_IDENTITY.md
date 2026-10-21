# WP-2: Resource identity redesign

**Problem:** base-pointer identity cannot express chunks, sub-grids, or halos
correctly; it both over-serializes (interior vs border) and under-serializes
(distinct bases over overlapping memory).

**Design: region-based resource keys.** Replace the bare `void*` key with a
`ResourceRegion { base, offset, length }` (or `{ resource_id, interval }`):

- The dependency tracker orders two accesses iff their regions **overlap**, not iff
  their base pointers are equal. This makes interior-vs-halo correctly *parallel*
  when they don't overlap and correctly *ordered* when they do.
- Identity becomes a registered `resource_id` (stable, not a raw address), decoupling
  dependency identity from allocation address, which is also what the distributed
  layer already wants (`GlobalBufferID`, `protocol.hpp:71`). Unifying these is a
  deliberate goal: one identity model for local ordering and remote resolution.
- Shard the tracker by `resource_id`, not hashed pointer, removing the spurious-edge
  collision class (findings 4.4) and the 1024-shard ceiling for free.

**Interval overlap** can be kept cheap: per resource, maintain the access history as
today but compare intervals instead of pointer equality; for the common
whole-buffer case the interval is `[0, size)` and the check degenerates to the
current behavior.

**Files:** `dependency_tracker.{hpp,cpp}` (region keys, overlap test, id-sharding),
`buffer.hpp` (handles expose a `resource_id` + sub-region views, e.g.
`buf.region(offset, len)`), `task_graph.cpp` (read/write sets carry regions),
`node.hpp` (`io_reads_/io_writes_` become regions; queue anchoring uses the region's
base).

**Risks:** this touches the correctness core. It must be landed behind exhaustive
tests (overlap/no-overlap, adjacent, nested, whole-buffer regression) and benchmarked
to confirm the whole-buffer path doesn't regress. Biggest single risk in the plan;
also highest leverage.

**Acceptance:** two kernels writing disjoint halves of one buffer run in parallel;
two kernels writing overlapping ranges through different base offsets are ordered;
the existing functional suite passes unchanged.
