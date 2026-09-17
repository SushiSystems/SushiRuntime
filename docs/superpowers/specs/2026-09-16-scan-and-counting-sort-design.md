# SushiRuntime: a prefix scan and a counting sort, late-bound

**Status:** designed 2026-09-16; every decision in §E taken by the owner the same day. Nothing is
built. Consumer: SushiEngine's matter device solver (its grid's counting sort and the neighbour
list's prefix sum, sushiengine-30's T3.1), which cannot be bit-equal with the host until these
land. Records follow G1's rules
(`docs/superpowers/specs/2026-09-15-g1-node-event-records-design.md` §F).

## 0. What the tree has today (facts)

- `Graph::add_reduce(input, output, std::size_t n, op, identity)` takes a fixed `n`; the tile
  plan comes from `detail::plan_reduction(n)` at registration
  (`sushiruntime/include/SushiRuntime/api/graph/graph.hpp:817`, `:842`). **It is not late-bound.**
  The brief's "late-bound counts via sized/providers" holds for `add(Dynamic, ...)`
  (`graph.hpp:635`), not for reduce.
- Tile size is `REDUCE_TILE = 256` (`api/detail/fixed_order_reduce.hpp:49`); levels are
  `ceil(count/256)` repeatedly (`fixed_order_reduce.hpp:94`).
- Every level shares one registration token (`graph.hpp:858-859`), resolved to one
  `reserve_registration()` in `emit_per_element_tracked` (`graph.hpp:1780-1807`), reset in
  build_plan (`graph.hpp:2095`, `:2149`). That is G1 §F.5: several records per
  `(node_index, invocation)` (`docs/superpowers/specs/2026-09-15-g1-node-event-records-design.md:238`).
- `add_segmented_reduce(values, Buffer<uint32> offsets, output, std::size_t segment_count, op,
  identity)` (`graph.hpp:955`) is one `add()` with a fixed segment count, one work-item per segment.
- `sized_from_device` requires its slot in a `Residency::Host` buffer (`graph.hpp:1936`, `:1969`).
- Engine seam: `RuntimeBackend::Graph::add_reduce(input, output, std::size_t count, combine,
  identity)` forwards verbatim, no descriptor, no provider (`engine/foundation/execution/include/
  SushiEngine/execution/backend/runtime_backend.hpp:427-432`). Native: `submit_reduce` builds a
  two-access Host `NodeDescriptor` and folds with `fold_fixed_order` (`backend/native_backend.hpp:293-330`,
  `:371-379`). `NodeDescriptor` carries `count`, `base`, `enabled` providers
  (`node_descriptor.hpp:181-193`); `CountProvider = Provider<std::size_t>` (`node_descriptor.hpp:162`).

Consequence: late binding is new work for all three primitives, not a copy of add_reduce. The
prerequisite brick is a **derived level count**: a tiled pass planned for capacity whose live
width each run is `ceil(n / 256^k)` of one late-bound `n`. Call it `detail::TiledDynamic`.

## A. Algorithms

### A1. Exclusive prefix scan, uint32 -> uint32, total out

Tiled two-level (L-level) scan sharing add_reduce's tile plan:

1. Up: level 0 sums each 256-tile of `counts` into `partial1`; level k sums tiles of level k-1.
   Identical to add_reduce's up levels, so it reuses `plan_reduction(capacity)`.
2. Down: top level scanned sequentially in one work-item (<= 256 values). Level k: each
   work-item t writes `base = offsets_k+1[t/256]` then walks its tile left to right, emitting the
   exclusive running value. Level 0 writes `offsets`.
3. `total = top base + top sum`, written to a 1-element buffer.

Levels L = ceil(log256 capacity): 3 for n = 10^6 (256^2 = 65 536 < 10^6 <= 256^3). Passes: 2L-1 = 5.
Work O(n), scratch ~2n/255 uint32 (about 31 KB at 10^6). Blelloch up/down sweep is also O(n)
work but needs 2 log2 n = 40 passes with tiny work-items per pass; the tiled form has one eighth
the passes and matches the existing level machinery. The tiled plan is taken (E1).

Semantics: uint32 addition modulo 2^32 is the same on every backend, so bit-equality holds for
any order. Overflow of the total is a caller contract (counts sum < 2^32); see E6.

### A2. Stable scatter given exclusive offsets

Target: `out[offset[key[i]] + rank(i)] = i`, `rank(i) = #{ j < i : key[j] == key[i] }`.

The offsets do not help compute `rank`. Any parallel `rank` needs the order of equal keys, which
is a stable sort in disguise:

- Tile-partitioned counting (rank = count in earlier tiles + local rank) needs a table
  tiles x keys = 3 907 x 2^20 uint32 = about 16 GB at n = 10^6. Rejected.
- Per-key linked list or pointer jumping: log n passes, data-dependent convergence unless run
  to the fixed bound, 2n scratch, heavy random access. Workable, not preferred.
- Merge-path merge sort of `(key, i)`: log2 n = 20 passes, O(n log^2 n) search work, 2n scratch.
  Deterministic but the most work.
- **LSD radix argsort on the key, 8-bit digits, each digit a tiled counting pass.** Per digit:
  (a) per-tile 256-bin histogram, tiles x 256 = 1 M uint32 (4 MB) at 10^6; (b) a column scan over
  tiles per bin plus a scan over bins gives each tile's per-bin write base, using A1; (c) each
  tile writes its elements in ascending order into `base[t][bin]++`. LSD is stable per digit, so
  the output is stable in `i`. Digits = ceil(key_bits/8): 3 for 2^20. Work O(d(n + T*256)),
  memory 2n index + 4 MB table + scan scratch. No host step: tile count derives from `n`. The
  capacity key bits fix how many digit blocks are registered; each run a host plan node derives
  the live digit count from the live key range, gives the digits above it zero work, and picks
  each active digit's direction between `indices` and one scratch so the last active digit writes
  `indices`.

Once the argsort exists, the scatter is `out[p] = sorted[p]` and the offsets are redundant: the
sorted order already is the CSR payload. `offset` then serves only as a checked input (assert
`total == n`), or is dropped.

### A3. Composite counting sort

`counts`, `offsets` and `indices` from `keys` alone: radix argsort (A2) then per-bucket
`offset[k] = lower_bound(sorted keys, k)` as one parallel work-item per bucket (2^20 x 20 compares
= 2*10^7 at the ceiling), `counts[k] = offset[k+1] - offset[k]`. This removes the race between a
separate histogram and the scatter, and the consumer's host counting loop. Cost at the ceiling:
three radix digits plus one bucket search; memory as A2 plus the K+1 offset array the consumer
already owns.

A1 (scan) and A3 (counting sort, with the argsort inside) ship as the public verbs, and the
radix digit as an internal brick. A standalone "scatter given offsets" is not offered, because
stable order has to be recovered through a sort anyway.

## B. SushiRuntime public API

New header `api/detail/fixed_order_scan.hpp` (tile plan for scan and radix, beside
`fixed_order_reduce.hpp`). Signatures on `Graph`; `DynamicGraph` gets the same through
`region(key)`, as add_reduce already does in the engine seam (`runtime_backend.hpp:535`).

```cpp
struct LateCount {                        // one of three, mutually exclusive
    std::size_t capacity;                 // plan size; live n must be <= capacity
    std::function<std::size_t()> host;    // or
    const Buffer<std::uint32_t>* device;  // Residency::Host slot, like sized_from_device
    std::size_t device_index = 0;
};

Graph& add_prefix_scan(const Buffer<std::uint32_t>& counts,
                       const Buffer<std::uint32_t>& offsets,   // >= capacity + 1
                       const Buffer<std::uint32_t>& total,     // >= 1
                       LateCount n);

Graph& add_counting_sort(const Buffer<std::uint32_t>& keys,
                         const Buffer<std::uint32_t>& indices,  // >= capacity
                         const Buffer<std::uint32_t>& offsets,  // >= key_capacity + 1
                         const Buffer<std::uint32_t>& total,    // >= 1
                         LateCount n,
                         LateCount key_range,                  // capacity = max bucket count
                         const Buffer<std::uint32_t>* counts = nullptr);

Graph& add_segmented_reduce(values, offsets, output, LateCount segment_count, op, identity);  // new overload
```

Exclusive form writes `offsets[n] = total` too, so CSR consumers need no second read.
`add_counting_sort`'s `total[0]` is the live element count, the same value as `offsets[key_range]`.

Residency: `counts`, `keys`, `indices`, `offsets` on one device, the same one; scratch inherits
`device_index()` and `residency()` from the first input, as add_reduce does (`graph.hpp:845`).
`total` may live anywhere. To read it mid-run on the host, or to feed it to a later
`sized_from_device`, it must be `Residency::Host` (the rule at `graph.hpp:1969`).

Errors (`Errc::invalid_graph` at registration): capacity > buffer sizes; offsets smaller than
capacity + 1; buffers on different devices; `key_range.capacity` of zero or above 2^32. At run:
live `n` > capacity or live key range > key capacity fails the run, the same way an out-of-window
`based_at` does (`graph.hpp:620-625`). A key >= live key range is undefined in release and fails a
debug check pass in tests only; per-element key checks in a kernel add a branch on the hot path.

NodeEventRecord: every internal pass of one call (scan up levels, down levels, total write; radix
histogram, column scan, placement per digit; bucket search) shares one registration token, so
all records carry one `node_index` and the step's `invocation`. Scan at 10^6 emits 5-6 records per
step, counting sort about 3*(3 + 2L) + 1. The engine sink groups by the pair (§F.5).

## C. Execution seam

Symmetric with add_reduce, but **with providers**, which add_reduce lacks today:

```cpp
Graph& add_prefix_scan(const Buffer<std::uint32_t>& counts,
    const Buffer<std::uint32_t>& offsets, const Buffer<std::uint32_t>& total,
    std::size_t capacity, CountProvider count);
Graph& add_counting_sort(const Buffer<std::uint32_t>& keys, const Buffer<std::uint32_t>& indices,
    const Buffer<std::uint32_t>& offsets, const Buffer<std::uint32_t>& total,
    std::size_t capacity, CountProvider count,
    std::size_t key_capacity, CountProvider key_range);
template <typename T, typename Combine>
Graph& add_segmented_reduce(const Buffer<T>& values, const Buffer<std::uint32_t>& offsets,
    const Buffer<T>& output, std::size_t capacity, CountProvider segment_count,
    Combine combine, T identity);
```

`add_counting_sort`'s `total[0]` is the live element count, the same value as `offsets[key_range]`.

Same three on `Region` for both backends. An unbound provider means capacity, the
`NodeDescriptor::count` rule (`node_descriptor.hpp:189`). A device-slot count stays a runtime-only
overload until the seam has a device-count provider type; today it has none.

Runtime backend: `to_dynamic`-style adapter from `CountProvider` to `LateCount` (next to
`to_dynamic`, `runtime_backend.hpp:134`), then forward.

Native backend: one `submit_*` helper per verb beside `submit_reduce` (`native_backend.hpp:293`),
each a Host node with declared HostRead/HostWrite intervals over capacity windows, `count` in the
descriptor. Kernels, sequential:
- scan: `acc = 0; for i < n: offsets[i] = acc; acc += counts[i]; offsets[n] = total[0] = acc`.
- counting sort: histogram into `counts` over K, the scan above, then
  `for i < n: indices[cursor[key[i]]++] = i`. Stable by construction and bit-equal to A2/A3,
  since integer results do not depend on order.
- segmented reduce: per segment, `fold_fixed_order` over `[off[s], off[s+1])`, which must match
  runtime's `fold_tile` from the first element (the runtime folds a segment as one tile,
  `graph.hpp:982`), not the tiled fold. That needs a `fold_segment` helper in
  `fixed_order_fold.hpp` for segments over 256 values. Verify the equivalence in a test before
  sharing the helper.

## D. Tests

sr test (sushiruntime) and se test (execution seam, both backends through one typed suite):

1. Scan bit-equality vs a host loop at n in {0, 1, 255, 256, 257, 65 536, 65 537, 10^6}; the
   tile edges catch an off-by-one in the level plan.
2. Scan exclusive form: `offsets[0] == 0`, `offsets[n] == total`; fails if inclusive slips in.
3. Counting sort vs `std::stable_sort` of `(key, i)` on random keys; the same test with
   `std::sort` as reference must be able to diverge (seeded duplicates), proving stability is tested.
4. Stability alone: all keys equal -> `indices == iota`.
5. Empty: n = 0 writes `total = 0`, `offsets` all zero over the live key range, no out-of-range write
   (guard canary after capacity).
6. Single element, and key = key_range - 1 with key_range = 2^20: catches a digit-count
   or mask error at the top bit.
7. Late-bound change: compile once, run n = 1 000 then 700 000 then 3; key range 2^10 then 2^20;
   assert `compile_count() == 1` and each result matches the reference.
8. Per-substep: `run(steps = 4)` with a kernel that changes keys between steps and a
   `sized_from_device` consumer of `total`; assert per-step correctness and no host read (runtime
   test uses a Residency::Host total only).
9. Events: record count per step equals the planned pass count, all with one `node_index`,
   `invocation` 0..steps-1.
10. Backend equality: native and runtime outputs compared bytewise on the same inputs.
11. Segmented reduce: a float Sum segment of 300 values, native vs runtime bytewise (catches a tiled
    vs flat fold mismatch).

## E. Owner decisions, 2026-09-16

1. **Scan form:** exclusive, and `offsets[n]` receives the total as well.
2. **Scatter shape:** one composite `add_counting_sort`. There is no public scatter taking
   precomputed offsets, because a stable parallel scatter needs each element's rank among earlier
   equal keys, which is a stable sort in disguise (§A2).
3. **Argsort:** internal to the counting sort. It becomes public when a second consumer needs it.
4. **Key width:** `std::uint32_t` only.
5. **A shared tiled late-bound pass comes first**, and `add_reduce` and `add_segmented_reduce`
   move onto it and gain `LateCount` overloads. Their fixed-count overloads stay and forward, so
   no caller changes.
6. **Digit width:** eight bits. `key_range.capacity` fixes the registered digit blocks; the live
   key range decides how many run (2026-09-17, owner option A). Every digit node declares both
   `indices` and the one scratch, and reads its direction from the run's plan, so no copy pass is
   needed whichever digit ends up last.
7. **Scratch belongs to the graph**, sized at registration from the capacities, so no run
   allocates.
8. **Out-of-range keys and a total that overflows `std::uint32_t`** are documented preconditions,
   checked in debug builds only.
