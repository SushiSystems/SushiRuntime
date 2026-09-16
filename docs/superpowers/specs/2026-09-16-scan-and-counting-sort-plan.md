# Implementation plan: the late-bound tiled pass, a prefix scan and a counting sort

**Status:** written 2026-09-16; nothing dispatched.

**Spec:** `docs/superpowers/specs/2026-09-16-scan-and-counting-sort-design.md`. Every task reads
the spec sections it names; exact signatures and rules live there, not here.

## Global constraints

- Every subagent prompt opens with the five-line dispatch block from `~/.claude/CLAUDE.md`.
- Implementers `sonnet`, reviewers `opus`, effort at most `medium`.
- No subagent runs `sr build`, `sr test`, `cmake`, `ninja` or `ctest`, edits a CMake file, or runs
  git stash, checkout, reset, clean, add or commit. The controller wires CMake and commits.
- The owner builds with `sr build` after each wave; the controller runs `sr test`, after pinging
  sushiengine-30 and sushiengine-39.
- Results are bit-equal across backends and across worker counts: integer arithmetic, and an order
  that is a function of the element count and the tile size alone.
- Nothing allocates per run. Scratch is sized at registration from the capacities.
- Every internal pass of one public call shares that call's registration index (G1 §F5).
- Public surface changes carry their `docs/INTEGRATION.md`, `docs/ARCHITECTURE.md` and
  `docs/CHANGELOG.md` lines in the same commit.

## Wave 1: the late-bound tiled pass

### Task 1: `LateCount` and the tiled plan

- **Files:** create `include/SushiRuntime/api/detail/fixed_order_scan.hpp` (the tile plan shared by
  scan, radix and the existing reduce), `include/SushiRuntime/api/graph/graph.hpp` (the `LateCount`
  type and its resolution), `include/SushiRuntime/api/fluent.hpp` (promotes `LateCount` into
  `SushiRuntime`, as `Sum`/`Minimum`/`Maximum` already are), create `tests/functional/unit/test_late_count.cpp`.
- **Waits on:** nothing.
- **Content:** spec §0 and §B. `LateCount` carries a capacity plus one of: nothing (the capacity is
  the count), a host provider, or a `Residency::Host` device slot. One resolution point per run,
  reused by every verb. The tile plan states, for a capacity, how many levels a 256-tile tree has
  and which slice each level folds, and answers the same question for a live count at run time.
- **Test:** level counts at 1, 255, 256, 257, 65 536, 65 537 and 10^6; a live count below capacity
  uses the same tile boundaries as that count would at capacity; a live count above capacity is
  rejected.

### Task 2: `add_reduce` and `add_segmented_reduce` move onto it

- **Files:** `include/SushiRuntime/api/graph/graph.hpp`, `tests/functional/unit/test_fixed_order_reduce.cpp`,
  `tests/functional/unit/test_registration_index.cpp` (a late-bound add_reduce grouping test,
  mirroring the fixed-count one already there).
- **Waits on:** Task 1.
- **Content:** spec §E5. Both verbs gain `LateCount` overloads and plan from the capacity; the
  fixed-count overloads stay and forward. The fold order for a live count `n` is the order the
  existing fixed-count path produces for that same `n`, which is what keeps today's results.
- **Test:** for counts 1, 255, 256, 257, 65 537, the late-bound result is byte-identical to the
  fixed-count result; a graph whose live count changes between runs does not recompile
  (`compile_count()` stays 1).

**Controller after the wave:** wire the new test, syntax-check, one `opus` review, commit.
**Owner build:** `sr build`.

## Wave 2: the prefix scan

### Task 3: `add_prefix_scan`

- **Files:** `include/SushiRuntime/api/graph/graph.hpp`, `api/detail/fixed_order_scan.hpp`,
  create `tests/functional/integration/test_prefix_scan.cpp`.
- **Waits on:** wave 1.
- **Content:** spec §A1 and §B. Exclusive scan over `counts` into `offsets`, with `offsets[n]` and
  `total` receiving the sum. Up-sweep and down-sweep over the tile plan, no host step.
- **Tests:** against a host reference, exact, at 0, 1, 255, 256, 257, 65 537 and 10^6 elements; the
  total matches `offsets[n]`; a live count that changes between runs needs no recompile; inside
  `run(4)` every step scans the current counts; one `node_index` across the passes with the step as
  the invocation.

**Controller and owner build** as after wave 1.

## Wave 3: the counting sort

### Task 4: `add_counting_sort`

- **Files:** `include/SushiRuntime/api/graph/graph.hpp`, `api/detail/fixed_order_scan.hpp`,
  create `tests/functional/integration/test_counting_sort.cpp`.
- **Waits on:** wave 2.
- **Content:** spec §A3 and §B. A stable LSD radix argsort over 8-bit digits, the digit count taken
  from the live key range, then the bucket offsets. Outputs stable `indices` and exclusive
  `offsets` with `offsets[key_range] = total`, and the optional per-key `counts`.
- **Tests:** stability against `std::stable_sort` on seeded duplicate keys; all keys equal; one
  element and none; the maximum key of a 2^20 range; a live key range of 2^16 taking two digit
  passes and 2^20 taking three; changing the count and the key range between runs without a
  recompile; per-substep inside `run(4)`; the record count and one shared `node_index`.

**Controller and owner build** as before.

## Wave 4: the Execution seam, in SushiEngine

### Task 5: seam wrappers

- **Files (repo D:\Projects\sushiengine):** `engine/foundation/execution/include/SushiEngine/execution/backend/runtime_backend.hpp`,
  `.../backend/native_backend.hpp`, `.../node_descriptor.hpp` if a provider shape is missing,
  create `tests/integration/test_execution_scan_sort.cpp`.
- **Waits on:** wave 3, and a SushiRuntime release or a sibling checkout the engine builds against.
- **Content:** spec §C. `add_prefix_scan`, `add_counting_sort` and the `add_segmented_reduce`
  wrapper on both backends, shaped like the existing `add_reduce` wrapper: capacity plus a
  `CountProvider`. The native backend runs each as a sequential host node, which is bit-equal for
  integers.
- **Tests:** the two backends agree byte for byte on the same inputs; stability holds on the native
  path; the engine seam test mirrors the runtime's cases at one size.

**Controller after wave 4:** review, commit, tell sushiengine-30 the seam signatures are final.
