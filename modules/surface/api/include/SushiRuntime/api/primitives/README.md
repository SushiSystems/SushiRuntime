# api/primitives

This folder leaves the repository in the next step: the runtime orchestrates and owns no math, so
the fixed-order reduction, the segmented reduction, the prefix scan and the counting sort move to
SushiBLAS, with `detail/fixed_order_reduce.hpp`, `detail/fixed_order_scan.hpp` and
`vocabulary/combine.hpp`.

Until then each primitive is a free function over the public interface of Recorder
(`graph/recorder.hpp`): it validates its buffers, takes scratch from Recorder::scratch(), opens one
rule with Recorder::rule() and records its nodes with Recorder::kernel() and Recorder::resolve().
None of them reaches a private member of Recorder, Graph or Region.

| Header | Holds |
| --- | --- |
| `reduce.hpp` | reduce(), with a fixed and a late-bound count |
| `segmented_reduce.hpp` | segmented_reduce(), with a fixed and a late-bound segment count |
| `prefix_scan.hpp` | prefix_scan() |
| `counting_sort.hpp` | counting_sort() |
| `primitive_members.hpp` | PrimitiveMembers: add_reduce(), add_segmented_reduce(), add_prefix_scan() and add_counting_sort() as members that forward to the free functions |

The sections below record why the code is the way it is. They are named after the member a caller
writes; the free function of the same name without `add_` holds the code.

## reduce.hpp

### add_reduce(input, output, n, op, identity)

add_reduce() reduces the first `n` elements of `input` into `output[0]` with a result that is
bit-identical across runs, worker counts, devices and steal patterns. A replayable simulation needs
that property from every accumulation that feeds its state, and a schedule-driven parallel reduce
cannot give it.

It works by folding fixed-size tiles: each work-item combines a contiguous run of at most 256 values
left to right, the partials are folded the same way, and that repeats until one value remains. Which
values meet which therefore depends only on `n` and that tile size, never on how the work was
scheduled. The intermediate levels use scratch this graph allocates and owns, on the same device and
with the same residency as `input`.

`identity` is the result when `n` is zero; it never enters the arithmetic otherwise.

### add_reduce: empty input

When `n` is zero there is nothing to fold and the identity is the whole answer. add_reduce still
records a real node that writes it, so downstream readers order after that node as they would after
any other writer.

### add_reduce(input, output, n, op)

This overload is the shorthand for the built-in combiners (Sum, Minimum, Maximum), which each
declare the value their reduction produces over no elements. A combiner without a static identity(),
such as a lambda, must use the overload that takes the identity explicitly. `input` must hold at
least `n` elements.

### add_reduce(input, output, LateCount, op, identity)

This overload plans the same fixed-tile tree as the `std::size_t` overload, but for `n.capacity`,
and resolves the live count once per run instead of once at registration. The fold order for any
live count that arrives is exactly the order the fixed-count overload would produce for that same
count (spec 2026-09-16-scan-and-counting-sort-design.md A1): every level beyond the one where the
live chain reaches one value degenerates to a single-element pass-through, so results stay
byte-identical whichever way the count was bound.

`Combine` is a binary functor and must be trivially copyable. `identity` is the result when the live
count is zero.

### add_reduce: one rule

Every node this call records (the resolver, every level, the trailing zero-count write) sits in
one rule scope, so they share one registration index, as the levels of the fixed-count overload do.

## segmented_reduce.hpp

### add_segmented_reduce(values, segment_offsets, output, segment_count, op, identity)

This is the scatter-accumulate shape a simulation actually needs: per-body contact impulses,
per-vertex contributions from the elements sharing that vertex. `segment_offsets` is the usual
CSR-style boundary array of `segment_count` + 1 entries, so segment s covers values [offsets[s],
offsets[s+1]) and `output[s]` receives its fold.

One work-item per segment folds that segment left to right, so there are no atomics and no
cross-work-item combining and the result is reproducible by construction. The trade-off is load
balance: a segment of ten thousand values and a segment of one cost one work-item each. The caller
owns the layout, and determinism is the contract, so the imbalance is left visible and not traded
away for a split that would break the ordering guarantee.

| Argument | Rule |
| --- | --- |
| `Combine` | A binary functor; must be trivially copyable |
| `segment_offsets` | Holds `segment_count` + 1 non-decreasing entries, the last no larger than the size of `values` |
| `output` | Holds `segment_count` entries |
| `identity` | The result for an empty segment |

### add_segmented_reduce(values, segment_offsets, output, LateCount, op, identity)

This overload launches one work-item per segment, planned for `segment_count.capacity` and resolved
once per run. A live segment count below capacity simply launches fewer work-items, so the fold
order per segment is unaffected and byte-identical to the fixed-count overload for that same live
count.

| Argument | Rule |
| --- | --- |
| `Combine` | A binary functor; must be trivially copyable |
| `segment_offsets` | Holds `segment_count.capacity` + 1 non-decreasing entries |
| `output` | Holds `segment_count.capacity` entries |
| `identity` | The result for an empty segment |

## prefix_scan.hpp

### add_prefix_scan

add_prefix_scan() plans add_reduce()'s 256-tile tree over `n.capacity` and up-sweeps it exactly as
add_reduce() does, but keeps every level's tile sums instead of discarding them. It then down-sweeps
from the top: each level's tile hands its running base to the level beneath it, which walks its own
tile left to right emitting the exclusive running sum. Level 0's down-sweep writes `offsets`
directly, and the top pass's sum is copied into both `total` and `offsets[n]`, so a CSR consumer
never needs a second read. No host step runs between the two sweeps.

| Argument | Rule |
| --- | --- |
| `counts` | Holds at least `n.capacity` elements |
| `offsets` | Holds at least `n.capacity` + 1 elements |
| `total` | Holds at least 1 element; receives the grand sum |

### add_prefix_scan: scratch

A single level (capacity 1 to 256) needs neither scratch allocation: the one down-sweep pass reads
counts and writes offsets directly. The allocation is guarded exactly as add_reduce() guards its own
scratch, since sycl::malloc_host(0, q) returns null and the allocator treats a null result as a
failure.

### add_prefix_scan: up-sweep

The up-sweep is the same tile fold add_reduce() runs, over every level but the top. It writes each
level's tile sums into scratch for the down-sweep to read as its bases. The top level's own sum is
never folded in the up-sweep, because the down-sweep's top pass walks it directly and computes that
sum as a side effect.

### add_prefix_scan: down-sweep

The down-sweep runs top to bottom. The top level has no level over it, so it scans its own tile of
at most 256 values sequentially from a base of zero. That scan also yields the grand sum, so it
always runs: an empty live count still has to publish total = 0.

### add_prefix_scan: closing write

Level 0's down-sweep only reaches offsets[live n - 1]. The top pass already settled total, so the
closing write copies it into offsets[live n] without re-deriving it.

## counting_sort.hpp

### add_counting_sort

docs/archive/superpowers/specs/2026-09-16-scan-and-counting-sort-design.md §A3 gives the algorithm,
the digit count and the derivation of offsets and counts.

| Argument | Rule |
| --- | --- |
| `keys` | Values to sort by; holds at least `n.capacity` elements |
| `indices` | Receives the stable sort order; holds at least `n.capacity` elements |
| `offsets` | Receives the exclusive bucket starts; holds at least `key_range.capacity` + 1 elements |
| `total` | Receives the live element count; holds at least 1 element |
| `n` | Capacity plus the live element count's source |
| `key_range` | Capacity plus the live bucket count's source |
| `counts` | Optional per-bucket counts; holds at least `key_range.capacity` elements when not null |

### add_counting_sort: offsets

The offsets pass runs one work-item per bucket, plus the sentinel, and each is a plain lower_bound
over the sorted keys. It reads no live bucket count, since a search past it still lands on the live
element count and the sentinel falls out for free (design doc §A3).

### add_counting_sort: counts

A count is the difference of two already-settled offsets, so the counts pass needs no live scalar of
its own, key range included: past the live range both neighbours are the live element count and the
difference is zero, which is the correct bucket count there too.
