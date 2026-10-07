# api/graph

The chainable builders of the fluent API: Graph, Region, DynamicGraph and RunHandle.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## dynamic_graph.hpp

### DynamicGraph

DynamicGraph is the mutable counterpart to the static Graph, built for an open world: work is
grouped into regions keyed by a caller id (a spatial cell, a streaming chunk). region(key) records
tasks with the static graph's add() surface; drop(key) removes one. Mutations are applied at a step
boundary, never against the running DAG, so the lock-free scheduler is untouched.

The composition is **incremental in the changed region and its boundary**, not the world. Each
region compiles to a cached sub-graph whose interior (intra-region) edges are baked once and never
touched again while the region is unchanged. Dependencies that cross a region boundary, where two
regions share a buffer, live in a separate layer on each node (Node::region, a RegionLink) that the
composer maintains. When a region streams in or out only its own nodes are rebuilt and only the
boundary edges of the allocations it shares are re-derived; the interior of every other region, and
every boundary that did not change, is left exactly as it was. Commit therefore costs O(changed
region + affected boundary pins), independent of the size of the untouched world.

Cross-region ordering follows ascending region key: for an allocation two regions share, the
lower-keyed region is the producer. Read/read sharing stays parallel (no edge between two regions
that only read the same buffer); only true RAW/WAR/WAW hazards across regions are serialized. Tasks
within one region keep their add() order.

Sharing is decided on **byte intervals, not allocations**. Two regions that touch disjoint element
ranges of one buffer gain no edge, which is what makes one region per simulation island viable:
islands are disjoint index ranges of a shared entity column, not separate allocations, so an
allocation-granular boundary would serialize islands that never meet. A caller names a slice with
Buffer::region({offset, count}) and passes it to Reads(...)/Writes(...) exactly as within one graph.

Runtime::dynamic_graph() returns a DynamicGraph. It is moveable, not copyable.

### region

The returned region records work with the same add() surface as a static graph. Accessing a region
marks it to be built or rebuilt at the next step boundary, since the caller accesses it to add work.

### drop

drop() takes effect on the structure immediately: region() and has_region no longer see the region.
The boundary it leaves is re-stitched at the next run, so a region is never removed from a DAG that
is mid-step. Dropping an absent region does nothing.

### set_profiling_requested

set_profiling_requested() stamps `plan_.profiling` immediately, so it reads correctly even with no
regions to recompose, and marks every live region for a full recompose so the next commit renumbers
every node's public registration index. The setting is independent of every other graph on the same
runtime.

### run_async

run_async() mirrors Graph::run_async(): the step goes to the worker pool and the call returns a
handle for the step now in flight, so the caller can extract the previous step's state while this
one runs and then settle up through the handle. Any pending structural change (a region added or
dropped) is committed before the step is submitted, exactly as a synchronous run does.

The handle must not outlive this graph, and its destructor waits for the step instead of letting the
plan be destroyed underneath the workers, as the RunHandle section describes.

### finish_async_step

finish_async_step() is the counterpart of Graph::finish_async_step for the composed plan: every live
region's States advance in it, after the step genuinely finished, so an asynchronous step is
observationally identical to a synchronous one. `state` is what run_async() recorded when it
submitted the step.

### Pin

A Pin is one node's participation in a base's boundary, with the exact byte intervals it touches.
Carrying the intervals, and not just the node, is what lets two regions writing disjoint slices of
one allocation stay parallel. Mapping one region to one simulation island depends on that: islands
are index ranges of a shared column, not separate allocations.

### BaseAccess

A BaseAccess is the boundary access summary of one region for one shared allocation. It is computed
once when the region is built and consulted, read-only, by the cross-region wiring of every base the
region shares. Entry pins receive incoming cross-region edges; exit pins are the sources for the
next region's edges. Each pin carries both its read and its write intervals, so the wiring can apply
the right hazard rule (RAW, WAW or WAR) to the right bytes, and two regions that only read the same
buffer still never gain an edge.

### for_each_intra_successor

for_each_intra_successor() walks a node's intra successors (inline buffer, then lock-free spillover)
and calls fn(succ) for each. It is used only at commit, single threaded, so a relaxed load of the
spillover head is fine.

### add_cross_edge

add_cross_edge() adds one cross-region edge from->to for base b. A node that gains an exported
successor stops being a leaf, so it drops its sentinel link; a node that gains a cross-region
predecessor stops being a root.

### remove_cross_edge

Removing an edge can take away the last exported successor of `from`. An intra-leaf with no
cross-region successor left is a true leaf again, so remove_cross_edge links it back to the
sentinel; the sentinel must wait for it.

### covered_by_any

covered_by_any() is true when one interval in `covers` fully contains `r`. Every byte of `r` is
then rewritten by that writer, and any later access to `r` is transitively ordered behind it.

### retire_covered

retire_covered() drops from `pins` every interval a later write fully covers, and the pin itself
once nothing of it is left live. Retiring only fully covered intervals is deliberately conservative:
a partially overwritten pin stays live, which can only add a cross edge, never drop one. The whole
boundary layer errs that way, because an extra edge costs parallelism and a missing one is a data
race.

### rewire_base

rewire_base() derives, or derives again, the cross-region edges for one shared allocation. It is
self contained: it first un-wires the base's current edges, then walks the regions touching it in
ascending key order, threading the live writers and the pending readers through so RAW, WAR and WAW
are honoured and read/read stays parallel. Every hazard test compares byte intervals, so two regions
touching disjoint slices of one allocation gain no edge at all. A base whose endpoints include a
torn-down node is un-wired earlier, while those nodes are still alive; the undo inside rewire_base()
then runs on an empty list and does nothing.

### rewire_base: live writers and pending reads

The live writers are the writes no later region has fully overwritten yet, and the pending reads are
the reads no later region has overwritten yet. Both accumulate and are not replaced wholesale,
because a region that writes one slice does not shadow an earlier writer of a different slice.

### rewire_base: one edge for RAW and WAW

A read-after-write and a write-after-write hazard share a source, an earlier write, and differ only
in what the consumer does with those bytes. One edge per producer and consumer pair therefore covers
both.

### compute_access

compute_access() fills a region's per-base boundary summary. For each allocation the region touches
it classifies the region's accessor nodes into entry pins (no intra predecessor touching an
overlapping range of the base) and exit pins (whatever an intra successor's write does not fully
cover), keeping each pin's exact byte intervals.

Both classifications test overlap and not base equality, and both err toward keeping a pin. A node
wrongly excluded from entry would lose an incoming edge, and one wrongly treated as shadowed would
lose an outgoing edge. Either is a data race, while an extra pin only costs an edge.

### teardown_region

teardown_region() removes a region's contribution to the shared structures: it drops its nodes from
the root set and the sentinel, and unregisters it from the bases it touched, marking each dirty. Its
cross-region edges must already have been undone. It then frees the region's node objects.

### commit: undo before free

commit() undoes every cross-region edge on a dirty base while all endpoints are still alive, so
freeing torn-down nodes leaves no dangling pointer in any other node's boundary list.

### commit: registration indices

commit() groups registration indices by value, not by consecutive run, so one add() call's local
index maps to exactly one global index even if its nodes are not contiguous in st.nodes.

### rearm

rearm() re-arms every node's live in-degree from the two-layer baseline and refreshes the per-step
State and late-binding updaters. It costs O(total) per step, which is inherent to replaying the
whole plan.

### drive: pending asynchronous step

drive() settles a pending asynchronous step before anything touches the plan: rearm() rewrites every
node's in-degree, which would corrupt a step the workers are still executing. Graph::drive() does
the same.

### refresh_updaters

refresh_updaters() rebuilds the aggregated per-step updater lists from every live region. It is
cheap (proportional to the number of stateful tasks, which are iterated every step anyway) and runs
in the composer so a region added or dropped is reflected before the next run.

### DynamicGraph::steps_

steps_ is the per-step lifecycle aggregated from every live region before each run. It is the same
detail::StepMachinery the static Graph uses, so both drivers run the per-step hooks in the same
order.

### regions_

regions_ holds the live regions (the recorders), ordered by key for deterministic composition.
unique_ptr keeps Region addresses stable and lets the composer use Region's private constructor.

## graph.hpp

### LateCount

A LateCount plans a tiled pass for `capacity` at registration; the live count read each run must not
exceed it. Leaving `host` empty and `device` null makes the live count `capacity` itself, matching a
fixed-count call. `host` takes precedence over `device` when both are set, which callers should not
do; exactly one should be set.

A fixed-count call spells its plain capacity as `LateCount lc; lc.capacity = n;`. Plain brace init
(`LateCount{n}`) trips `-Wmissing-field-initializers`, and this type stays an aggregate, so it takes
no constructor. The designator form (`LateCount{.capacity = n}`) reads better but is a C++20
feature. On the non-MSVC compiler path this tree builds with (clang++, `-pedantic -Werror`,
`cmake/CompilerWarnings.cmake`), it fails as `-Wc++20-designator`, confirmed against that compiler,
so it is not offered as the idiom.

### resolve_late_count

resolve_late_count() is the one resolution point every verb built on LateCount shares: a host
provider wins when set, then a device slot, and with neither the live count is the capacity, so a
LateCount left at its default behaves like a fixed count. The result is never larger than
`lc.capacity`.

### validate_late_count

validate_late_count() mirrors require_host_device_slots(): a device slot must be Residency::Host,
checked before add() mutates the graph and not inside the run loop. It also rejects a capacity too
wide for the uint32 live-count slots the late-bound verbs resolve into.

### add_untracked(n, fn)

add_untracked(n, fn) is the dependency-blind form of add(Reads, Writes, n, fn). A per-element
callable is void(std::size_t) capturing raw USM pointers, so nothing in its type says what it
touches and the runtime cannot infer it. This overload therefore records no read or write regions:
the task is both a root and a leaf, ordered against nothing. That is correct for self-contained work
(seeding a buffer no other task in this graph reads, a benchmark kernel) and wrong the moment
another task shares data with it. Whenever ordering matters, name the data with
Reads(...)/Writes(...) and use add().

### add_emitter

add_emitter(emit) records work the fluent verbs do not shape: `emit` receives the backing
TaskGraph and adds its own nodes with add_node(), add_task() or add_native(). It is for a library
above the runtime that already records through the lower-level graph and wants that work inside a
Graph or a Region without restating each task as an add() overload. The runtime learns nothing
about that library; it calls `emit` and takes the nodes.

`emit` is called at every build of the plan, so in a DynamicGraph it runs again each time its
region is rebuilt and never while the region is left alone. It must therefore be repeatable, and it
only adds nodes: compiling the graph, taking its nodes or keeping a reference to it breaks the
build that called it.

An emitter is one rule, so size() counts it once however many nodes it adds. Ordering comes from
the regions each node declares, exactly as for nodes the fluent verbs emit; the call takes no
Reads or Writes of its own. The nodes keep the names and registration indices they were recorded
with, one index per node, so a profile shows each kernel. name_last() and rule_registration()
address the last node only.

Device work an emitter records must wait on the dependency events it is handed
(`h.depends_on(deps)`); a chained plan relies on that for its order.

### add_untracked(Extent, fn)

add_untracked(Extent, fn) is the 2-D and 3-D counterpart of add_untracked(std::size_t, fn): the
runtime submits a parallel_for over the Extent's box and invokes `fn` with the global sycl::id of
each work-item. When the Extent carries an origin offset the iteration covers only that sub-box, so
a kernel can skip a halo or touch just a ghost layer. Plain 1-D counts keep using the std::size_t
overloads; a 1-D Extent is only needed for an origin offset. The overload records no regions, so a
task that must be ordered uses add(Reads, Writes, Extent, fn).

### add_untracked(NdRange, fn)

add_untracked(NdRange, fn) submits a parallel_for over the NdRange and hands `fn` a
sycl::nd_item\<N>, exposing the work-group model (barriers, local shared memory, sub-groups) that a
flat parallel_for cannot. It is for blocked algorithms such as tiled GEMM and shared-memory
stencils, where neighbouring work-items must cooperate. The overload records no regions, so a task
that must be ordered uses add(Reads, Writes, NdRange, fn).

### add(Reads, Writes, n, fn)

This overload is the ergonomic form of the explicit-tracking one: the caller passes the Buffers and
States the task reads and writes as Reads(...)/Writes(...) instead of building a std::vector\<void*>
by hand. The dependency tracker still orders the task by the keys those wrappers gathered.

### add(Reads, Writes, NdRange, fn)

This overload is the ordered counterpart of add_untracked(NdRange, fn). It has the same work-group
execution model (barriers, local memory, sub-groups), but the task is ordered against the rest of
the graph by the regions it declares, so a tiled kernel is no longer forced to give up dependency
tracking to get an nd_item.

### add(Reads, Writes, n, range_kernel)

This overload is the ordered counterpart of add_untracked(n, range_kernel). `fn` still drives the
sycl::handler over [0, n) itself, which is the escape hatch for a kernel the per-element form cannot
express, but the regions it declares order it against the rest of the graph.

### add(Reads, Writes, raw_kernel)

This overload is the ordered counterpart of add_untracked(raw_kernel). `fn` owns the whole command
group (its own parallel_for, local accessors, a memcpy) while the declared regions still order it.
It is for a launch shape no other overload fits; the shaped overloads are the better choice
otherwise, since they cannot get the launch geometry and the declared data out of step.

### add(Extent, In/Out/InOut..., fn)

This is the most direct form: the caller passes the buffers a kernel touches as
In(buf)/Out(buf)/InOut(buf), and the runtime both registers the reads and writes and hands `fn` the
raw element pointers, in argument order, after the index. The caller writes the data flow once and
gets pointer capture and dependency ordering together. `Cap0` is the first capture wrapper; a
separate parameter for it guarantees at least one.

### add(Extent, In/Out/InOut..., fn): the trailing pack

`Args` is one trailing parameter pack so that the compiler can deduce it. Its last element is the
callable and the others are further captures. add() splits the pack inside its body, which keeps the
call site reading `In(a), Out(c), fn` with `fn` last.

### add(State, fn)

This is the canonical stepped task: it reads the state's current field and writes its next field,
with the dependency tracker keying on those two buffers automatically. The runtime supplies the
current/next pointers to `fn` at execution time, so under run(n) the same compiled node follows the
double-buffer swap with no recompilation. A State takes exactly one update rule per step.

### add(State, Extent, fn)

This is the 2-D and 3-D form of add(State, fn): the runtime iterates the Extent box (the whole
field, or a sub-box for boundary or halo handling) and hands `fn` the global index alongside the
current/next pointers. The double-buffer swap is followed exactly as in the 1-D form. The rank `N`
is 1, 2 or 3.

### add(State out, State in0, rest..., fn)

This overload writes the next field of `out` from the current field of `out` and the current field
of one or more other States. It is the building block for coupled systems such as reaction-diffusion
with several species. All reads see the previous step's data (a Jacobi update): every State advances
only after all of a step's kernels have run, so writing one field never changes what another reads
in the same step. Each field must have exactly one update rule per step.

### add(vector\<void*> reads, vector\<void*> writes, n, fn)

This overload is the advanced escape hatch for callers who manage their own data and want the
dependency tracker to order the task against others by the pointers it touches. The State and Buffer
overloads are the better choice; this one is only for cases where handle-based tracking does not
fit.

### add(Dynamic, Reads, Writes, capacity, fn)

This overload is the dynamic counterpart of the Reads/Writes per-element add: the task is compiled
once, but `dyn`, the late-binding controls built from when() and sized(), lets its iteration size
and whether it runs vary every step with no recompile. The touched buffers are allocated for the
maximum of `capacity` elements; sized() then iterates only the live count each step, and when() can
skip the task entirely.

sized_from_device() takes the count from a slot a predecessor kernel wrote instead of from the host,
which is what lets a produce-then-consume chain (broadphase to narrowphase to solver) run in one
composition with no worst-case launch width. The counter is registered as one of this task's reads,
so the producer is necessarily ordered before it.

based_at() and based_at_device() move where the node starts, for a slice whose offset shifts between
steps, such as a graph colour. The callable still receives absolute indices, and the window must fit
the capacity: one that does not fails the run instead of reading past the end.

### add(Dynamic, State, fn)

This overload is the dynamic counterpart of add(State, fn): the same read-current, write-next
stepped task, but `dyn` lets its active-element count and whether it runs vary every step with no
recompile. The State's full allocation is the capacity; sized() iterates a prefix of it (a changing
number of active cells, for example), and when() can skip the step's kernel. The double-buffer swap
is followed as in the static form. A State takes exactly one update rule per step.

The State still advances (swaps current and next) on a step its when() disabled, so a skipped step
leaves the un-written next field current. This overload suits size variation; truly sleeping a field
across steps belongs to the streaming layer, not when().

### add_host(Reads, Writes, fn)

add_host(Reads, Writes, fn) is for arbitrary host C++ that submits no device work: gameplay logic,
AI, navigation, the CPU half of a broadphase. The caller names the data it touches with
Reads(...)/Writes(...) so the tracker orders it against the rest of the graph; the runtime then runs
`fn` natively on a worker thread, with no SYCL queue submission, and releases its successors through
the same path as any kernel task. The shared USM it reads is visible because the task runs only
after its predecessors complete.

### add_host(fn)

add_host(fn) is the dependency-free form of add_host: with no Reads or Writes the task is both a
root and a leaf, so it is for self-contained host work (telemetry, a per-step callback) that needs
no ordering against other tasks. It runs natively, with no SYCL submission.

### name_last

Without a name a node reports as "unnamed_task" in RunReport::NodeTiming, which makes a profile of a
real graph a list of identical labels. Chaining name_last() onto any add() fixes that:

```cpp
sim.add(API::Reads(a), API::Writes(b), n, integrate).name_last("integrate");
```

A trailing name argument would have been shorter, but two launch shapes end in a parameter pack
whose last element is the callable: add(Extent, captures..., fn) and add(State, States..., fn). A
defaulted trailing name cannot be expressed there. One spelling that works for every shape,
including any added later, beats a trailing argument on most of them and an exception for the rest.

For a task that expands to several nodes, such as a reduction, name_last() names the last one, the
final fold.

### add(State out, State in0, rest..., fn): the trailing pack

`Rest` is one trailing parameter pack so that the compiler can deduce it. Its last element is the
callable and the others are further read States. add() splits the pack inside its body, which keeps
the call site reading `add(out, a, b, c, fn)`.

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

### add_reduce: registration token

One token is shared by every node this call emits (the resolver, every level, the trailing
zero-count write), as the fixed-count overload shares one across its levels (G1 spec §F5).

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

### add(OpID, n, reads, writes, local, params)

This overload is the fluent counterpart to a distributed dispatch. `op` names a kernel registered
(by the same OpID) in this binary's KernelRegistry on every node; when the runtime is a Master with
a live worker that supports `op`, the scheduler may ship this task over the wire. Otherwise (a
standalone build, no worker, or the policy declining) `local` runs it on this machine, so the same
graph is correct with or without a cluster.

`reads` and `writes` are the buffers the task touches, given as dependency keys
(Buffer/State::dependency_key() or read_/write_key()). The dependency tracker orders the task by
them, and the distributed layer resolves them to network buffer handles. `params` are scalar
arguments carried in TaskMetadata, element counts for example.

### add(OpID, State, local, params)

This overload is the distributed counterpart of add(State, fn): the State's step may ship to a
worker (when `op` is registered cluster-wide and a worker is willing) or run its local fallback,
with the same graph correct either way. Because a State rotates its allocations each step, the node
tracks the step's read and write buffers through live pointer lists the runtime re-points before
every step, so the compiled plan stays replayable while the offload follows the double-buffer swap.
The State must be registered with State::distributed() first so both allocations can cross to a
worker.

### rule_registration

RunReport numbers a node event by registration index, and the several nodes one add() lowers to
share one index, so rule_registration() is how a caller maps a record back to the call that recorded
it. The indices are drawn when the graph compiles, so every rule reports `no_registration` until the
first run, and a recompile may renumber them.

### set_profiling_requested

set_profiling_requested() marks the plan dirty so the next run recompiles with the new
`CompiledGraph::profiling` flag stamped in. The setting is independent of every other graph on the
same runtime.

### run(steps)

The graph is compiled on the first run (and after any mutation), then the compiled plan is replayed
for each step: node dependency state is reset, the plan re-executed, and every State advanced to its
next field. Compilation happens at most once per call.

### run(Core::RunReport&)

run(Core::RunReport&) is the allocation-free form of run(). A fixed-step loop keeps one report alive
and passes it every tick; the report is reset and refilled in place, so after the first run the
per-run vectors are reused and not reallocated:

```cpp
Core::RunReport report;
while (running)
    simulation.run(report);
```

It is identical to run() in every other respect. The steady state is allocation-free only with
profiling off; a profiling run rebuilds its per-node labels by design.

### run_async

run_async() is the asynchronous counterpart of run(). The step is handed to the worker pool and the
call returns immediately, so the caller can do work that needs no workers, such as extracting the
previous step's state for a renderer or an audio system, while this one runs, then settle up through
the returned handle:

```cpp
auto step = simulation.run_async();
extract_previous_frame();
Core::RunReport report = step.wait();
```

It runs one step only. The multi-step drivers (run(n), run_for, run_until) stay synchronous, because
their per-step host work has to happen between steps and there is nothing for the caller to overlap
with in the middle of a batch. A step is one tick, which is the granularity a frame actually wants.

The handle must not outlive this graph, and its destructor waits for the step instead of letting the
plan be destroyed underneath the workers, as the RunHandle section describes.

### run_async: pre-step order

run_async() follows the same pre-step order as the synchronous driver: point the State views at
their buffers, clear a previous run's cancellation, zero the profiling accumulators, then refresh
the late-bound controls. A different order would make an asynchronous step mean something subtly
different from a synchronous one.

### cancel

cancel() is safe to call concurrently with a blocking run(), run_for() or run_until() executing on
another thread: the active step drains its remaining tasks without launching new kernels and
returns, the driver stops before the next step, and the resulting RunReport has cancelled set. It
does nothing when no run is active; the flag is cleared when the next run starts. Cancellation is a
clean stop, not an error, and it does not throw.

### Friends Region and DynamicGraph

A Region is a recorder: it derives from Graph to inherit every add() overload, so it needs the
constructor that friendship protects and access to the recorded specs and updaters. The DynamicGraph
composer reads those same members to build one combined plan.

### finish_async_step

finish_async_step() is the tail of drive_impl's loop body plus its report building, split out
because those happen in a different call from the submission. Advancing every State in it, and not
at submit, is what makes an asynchronous step observationally identical to a synchronous one: the
just-written field becomes current only after the step has genuinely finished. `state` is what
run_async() recorded when it submitted the step.

### drive: report reset

drive() resets the report before any early return. A caller reusing one report across ticks must
never read the previous run's numbers back out of a run that failed; fill_run_report sets the flag
back on success.

### drive: pending asynchronous step

drive() settles a pending asynchronous step before anything touches the plan. A synchronous run
re-arms every node's in-degree, so letting one start while an asynchronous step is still executing
would rewrite the scheduling state of the DAG the workers are walking. That is a much worse failure
than the engine's own reentrancy rejection, which only fires later, after the damage.

### run_chained

The per-step host work runs between submissions exactly as on the scheduler; it only moves host
pointers and host-set controls, so it never needs the device to have finished (spec §C, layer 1).
`keep_going` is called with the count of submitted steps before each step, and `engine` is consulted
only for cancellation.

### observe

observe() records the liveness observers of the typed handles a task touches, so a run can detect a
handle freed before it ran. Raw-pointer and bare-region overloads contribute none: naming data by
raw pointer opts out of lifetime tracking, by design.

### LiveRange

A LiveRange is the half-open element window a late-bound node iterates this step: where it starts
and how many elements it covers. The two are resolved and validated together, because only their
combination says whether the node stays inside the buffers it was compiled for.

### submit_live_range

submit_live_range() submits a late-bound node's kernel over its live size. A step with nothing to do
is ordinary (a tick with no contacts), and a zero-width parallel_for is not portable across SYCL
backends, so an empty count submits an empty kernel instead: the node still completes and still
releases its successors.

### submit_live_range: base offset

submit_live_range() applies the offset itself and does not leave it to the caller, so a kernel
written against absolute indices stays correct whether or not its node happens to carry a base.

### require_host_slot

require_host_slot() asks the slot's own address first. When that answers anything but host, it
resolves the address to the allocation containing it and asks again: a caller offsetting into a plan
array hands over an interior pointer, which SYCL reports as unknown. It runs while add() builds a
node, so the containment search never meets a dispatch. A null slot passes.

### adopt_device_bindings

adopt_device_bindings() copies a Dynamic's device count source onto the node's bindings and
registers that slot as a read. Registering inside adopt_device_bindings(), and not asking the caller
for it, is what makes "the producer runs first" a property of the graph instead of a documented
convention: the read region orders this node behind whoever writes the count.

### register_device_slot

register_device_slot() names one four-byte slot as a read of the node that will consume it. It names
just the one element, not the whole buffer, so two nodes reading different slots out of one buffer
stay unordered against each other.

### emit_dynamic_state

emit_dynamic_state() emits a 1-D State update rule with late-bound size and enablement. It mirrors
emit_dynamic_per_element but reads the State's current/next pointers fresh each submit, following
the swap, and registers the once-per-step advance, so a stepped field can vary its active count or
sleep entirely with no recompile.

### emit_offload_per_element

emit_offload_per_element() emits a 1-D offloadable per-element task with a local fallback. It is the
single funnel for both the raw-pointer and the Reads/Writes offload overloads. Setting op_id is what
makes compile() consider the node offloadable; otherwise the local parallel_for runs.

### build_plan

build_plan() builds a fresh backing graph from the recorded specs, compiles it, and caches the plan.
The compiled plan snapshots its own replay baseline (in-degrees) so the fluent layer never touches
Node's scheduler-internal state. compile() drains its builder, so a fresh BackingGraph is built
whenever the topology changes.

### reset_plan_state

reset_plan_state() restores the cached plan's per-run state so it can run again. The scheduler
re-arms itself; reset_plan_state() resets the DAG-side state a previous run consumed, which
CompiledGraph owns next to the Node definition.

### has_dangling_resource

has_dangling_resource() is true when the allocation behind any recorded handle has been freed. The
graph checks it once per run, so a graph that outlives its data fails cleanly instead of dispatching
against released memory.

### steps_

steps_ holds the per-step lifecycle: the State double-buffer views, the advancers, the late-binding
refreshers and the distributed live-IO re-pointers. It owns the ordering invariant that runs around
each replayed step. The DynamicGraph composer uses the same type, so both drive the loop
identically.

### NodeSpec

NodeSpecs are the recorded task specifications: each replays one add() onto a fresh backing graph.
The fluent Graph is the source of truth so it can rebuild and recompile when mutated. A spec is
recorded as {emitter, name} and not as a bare callable: the name is handed to the emitter when the
plan is built, not captured when the task is recorded, which is what lets name_last() attach one
after the add() call has already returned.

### NodeSpec converting constructor

The converting constructor is a template and does not take a std::function parameter. Taking the
function type directly would make lambda -> std::function -> NodeSpec two user-defined conversions,
and only one is allowed implicitly, so every push_back of a spec would need spelling out.

### reduce_scratch_

reduce_scratch_ is the USM scratch owned by the reduction tasks recorded on this graph. It is
allocated when the task is recorded, not when the plan compiles, so the pointer its kernels captured
stays valid across a recompile and across the DynamicGraph composer rebuilding a region from its
specs.

### async_step_pending_

async_step_pending_ is true between run_async() and the settling of its handle. It guards the plan
against a second run re-arming nodes the workers are still walking, and drive() checks it.

### compiles_

compiles_ counts the times the backing graph has been compiled. A late-bound graph proves its value
by holding it at 1 across a multi-step run whose sizes and enable flags change every step.

## region.hpp

### Region

A region is the unit of mutation for the dynamic graph: a streaming open world groups its work into
spatial cells, and a cell is added or dropped as it streams in or out without recompiling the rest
of the world. To avoid a second way of recording tasks, Region is implemented in terms of the static
Graph by private inheritance and re-exposes only the task-building surface (add(), size()). It does
not run itself. A region is driven by its owning DynamicGraph, which composes every live region's
nodes into one plan and steps them together, so Graph's run() family is intentionally hidden.
DynamicGraph::region() returns one; it is not copyable or movable and lives in the DynamicGraph that
owns it.

### Inherited surface

Region inherits the whole fluent task-building surface unchanged from Graph. Every add() overload
(per-element, N-D, State, capture, tracked, dynamic, offload), the untracked, emitter and host forms,
the reductions, the prefix scan, the counting sort and size() come through verbatim, so a region records
work exactly as a static graph does. Only the run() family is hidden, because a region is stepped by
the DynamicGraph that owns it.

## run_handle.hpp

### AsyncRunState

A synchronous run reads its "before" counters and its start time as locals, because the whole run
happens inside one function. An asynchronous run does not: the step is submitted in one call and
settled in another, so the same values have to travel in the handle. Every member of AsyncRunState
is a plain value, so the handle owns no resources and stays cheap to move.

### RunHandle

`run()` blocks for the whole step, which is correct for a fixed-step simulation thread but costs
**overlap**: the next tick cannot begin while the previous one's state is being extracted for a
renderer or an audio system, even though that extract needs no worker threads. `run_async()` returns
a RunHandle instead. The work is already proceeding on the worker pool, so the caller can do that
extract and then settle up:

```cpp
auto step = simulation.run_async();
extract_previous_frame();          // overlaps with the step
Core::RunReport report = step.wait();
```

The handle is move-only and owns the obligation to finish the step: its destructor waits if `wait()`
was not called, because the compiled plan must not be destroyed while workers are still touching its
nodes. That makes the safe usage the natural one: declare the handle after the graph and ordinary
scoping does the rest. It also means a discarded handle discards the step's outcome, **including a
task exception**. Call `wait()` (or `try_wait()`) whenever the result matters, which is nearly
always.

Exactly one step may be in flight per runtime; starting another while a handle is pending is
rejected rather than silently interleaved.

### operator=(RunHandle&&)

Assigning over a pending handle would otherwise abandon a running step with nobody left to wait for
it. The moved-from handle owns nothing afterwards.

### ready

ready() answers "has the graph finished", so a frame loop can keep overlapping work instead of
blocking early. It is not a substitute for wait(): the pool still has bookkeeping to drain and the
step's state advance has not happened yet.

### wait()

wait() completes the step's lifecycle, in which every State advances to the field it just wrote, and
consumes the handle, so a second call throws instead of waiting on a step that is no longer running.
A task exception propagates out of wait(), and the state advance is skipped in that case, exactly as
a failed synchronous run leaves its states untouched.

### wait(Core::RunReport&)

wait(Core::RunReport&) is the allocation-free counterpart of wait(), for the same reason
Graph::run(RunReport&) exists: an overlapped frame loop settles a step every tick, and a report it
already owns costs nothing to refill. The report is reset before it is written, so a stale field
from the previous step can never survive into this one.

### wait: consuming the handle

wait() consumes the handle before the blocking call: if a task threw, the step is still over, and a
destructor that tried to wait again would block forever on a latch nobody will signal.

### wait: clearing the report

wait() clears the report before the blocking call, so a step that ends in a task exception cannot
leave the previous step's numbers readable in a report the caller reuses. finish_async_step sets the
flag back on success.

### wait: releasing the owner

The step is over either way. wait() releases the owner's in-flight marker before propagating, or one
throwing kernel would leave the graph refusing every later run. The failure of a step must not
become the failure of the graph.

### settle

settle() finishes a step nobody waited for. A destructor may not throw, so the outcome, including a
task exception, is dropped; that is the documented cost of discarding a handle, and it is still far
better than returning while workers hold pointers into the plan.
