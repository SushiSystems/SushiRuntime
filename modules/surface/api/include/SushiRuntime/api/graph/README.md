# api/graph

The chainable builders of the fluent API: Graph, Region, DynamicGraph and RunHandle.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## Which header holds what

| Header | Holds |
| --- | --- |
| `graph.hpp` | Graph: a Recorder, a PlanCache and the run entries of StepDriver |
| `dynamic_graph.hpp` | DynamicGraph: the regions by key, the pending changes and the run entries of StepDriver |
| `region.hpp` | Region: a Recorder behind the add() surface |
| `run_handle.hpp` | RunHandle and AsyncRunState |
| `recorder.hpp` | Recorder and Recorder::RuleScope |
| `access.hpp` | Access, what one node reads and writes |
| `device_sized.hpp` | DeviceSized, a launch that reads its live width on the device |
| `node_options.hpp` | NodeOptions |
| `backing_graph.hpp` | BackingGraph and is_graph_emitter_v |
| `builder_surface.hpp` | BuilderSurface: add(), add_untracked(), add_emitter(), add_host() and name_last() |
| `plan_cache.hpp` | PlanCache: the compiled plan of one recorder |
| `step_driver.hpp` | StepDriver: run(), try_run(), run_for(), run_until(), run_async(), cancel() and the step loop |
| `boundary_access.hpp` | Pin, BaseAccess and compute_boundary_access() |
| `region_composer.hpp` | RegionComposer: the cached sub-graph of each region and the stitched plan |

The replay callable of each node shape is in `detail/emit_*.hpp`.

## graph.hpp

### Graph

Graph is three bricks behind one name. The Recorder owns what add() recorded. PlanCache, a
private base, owns the plan compiled from it. StepDriver, a public base, gives the run entries and
drives the step loop. Graph itself holds the context, forwards size() and rule_registration(), and
answers the questions the driver asks.

### rule_registration

RunReport numbers a node event by registration index, and the several nodes one add() lowers to
share one index, so rule_registration() is how a caller maps a record back to the call that recorded
it. The indices are drawn when the graph compiles, so every rule reports `no_registration` until the
first run, and a recompile may renumber them.

### Friends

Runtime constructs a Graph. StepDriver\<Graph> reads the plan and calls the private members named
in the step_driver.hpp section. The two test seams read `plan_`. Region and DynamicGraph are not
friends: a Region owns its own Recorder, and DynamicGraph reads a region through
Region::recorder().

## recorder.hpp

### Recorder

A Recorder is the single owner of recorded work: the nodes in recording order, the liveness
observers of the handles they name, the USM scratch they use and their per-step hooks. Graph and
Region each hold one. Every recording member builds a replay callable and ends in the private
record(), which appends the node, attaches it to the open rule, takes the observers and advances
generation(). A domain library records through the public members and needs no friendship.

### RuleScope

rule() opens a scope, and the nodes recorded until it ends form one rule: at replay the recorder
reserves one registration index on the backing graph when the first of them is added and stamps it
on all of them. The nodes of a scope are contiguous, so replay() needs two locals and no
allocation. A node recorded outside a scope draws its own index. An emitter stamps its own nodes
and takes none.

### record: liveness

record() takes the liveness observers of the typed handles a node names, so a run can detect a
handle freed before it ran. An Access built from Reads and Writes carries them; one built from
bare regions carries none, because naming data by raw pointer opts out of lifetime tracking, by
design.

### has_dangling_resource

has_dangling_resource() is true when the allocation behind any recorded handle has been freed. The
graph checks it once per run, so a graph that outlives its data fails cleanly instead of dispatching
against released memory.

### replay

replay() adds every recorded node to a backing graph in recording order and stamps each name.
PlanCache::build() and RegionComposer::build_region() both call it, so a static graph and a region
are replayed by the same code. The visitor overload reports each node once it is added; PlanCache
uses it to read back the registration index.

### kernel(DeviceSized, ...)

A DeviceSized launch always runs `capacity` work-items, so its width is part of the compiled plan
and the live width can come from a word a predecessor wrote in the same step. A work-item at or
past `*live_width` returns at once; a null `live_width` lets every item run. The kernel's arity
picks what it receives after its index:

| Callable | Receives |
| --- | --- |
| `fn(t)` | The index |
| `fn(t, w0)` | The index and `live_words[0]` |
| `fn(t, w0, w1)` | The index, `live_words[0]` and `live_words[1]` |

The words are read on the device, so the Access must name the regions `live_width` and
`live_words` lie in; that read is what orders the launch after the node that wrote them.

### resolve

resolve() records one single-task node that turns K LateCounts into live values and hands them to
`fill` on the device, in the order of `counts`. A host count is read when the node is submitted. A
device count is read inside the kernel, and its slot is added to the node's reads, so the node
runs after the producer of the count. `fill` writes the values where the later nodes of the rule
read them, usually words from scratch() that a DeviceSized launch names.

A count is checked twice. resolve() rejects a device slot outside Residency::Host and a capacity
too wide for a uint32 word before it records anything. At run, a host count above its capacity
throws at submission, and a device count above its capacity is replaced by zero and written to a
fault record resolve() allocated; the step driver reads that record when the step ends and fails
the run.

### scratch

scratch() hands out USM the recorder owns. It is allocated when the node is recorded, not when the
plan compiles, so the pointer a kernel captured stays valid across a recompile and across the
composer rebuilding a region. Fault records come from the same store.

### steps

steps_ holds the per-step lifecycle: the State double-buffer views, the advancers, the late-binding
refreshers and the distributed live-IO re-pointers. It owns the ordering invariant that runs around
each replayed step. The DynamicGraph composer uses the same type, so both drive the loop
identically.

### state_rule(Dynamic, State, fn)

The late-bound State rule mirrors kernel(Dynamic, ...) but reads the State's current and next
pointers fresh each submit, following the swap, and registers the once-per-step advance, so a
stepped field can vary its active count or sleep entirely with no recompile.

### offload

offload() is the single funnel for both the raw-pointer and the Reads/Writes offload overloads.
Setting op_id is what makes compile() consider the node offloadable; otherwise the local
parallel_for runs.

## builder_surface.hpp

### BuilderSurface

BuilderSurface is the overload set of add() and its siblings, written once for Graph and
Region. Each overload turns its arguments into an Access and calls one recording member of the
builder's Recorder, then returns the builder, so calls chain on the type the caller holds.

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

In a rule of several nodes, name_last() names the node recorded last.

### add(State out, State in0, rest..., fn): the trailing pack

`Rest` is one trailing parameter pack so that the compiler can deduce it. Its last element is the
callable and the others are further read States. add() splits the pack inside its body, which keeps
the call site reading `add(out, a, b, c, fn)`.

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

## plan_cache.hpp

### PlanCache

PlanCache holds the plan compiled from one recorder. It is stale until the first build, after a
change of the profiling request, and whenever the recorder's generation() differs from the one the
plan was built at. `plan_` is protected because the test seams of the owning Graph read it.

### build

build() builds a fresh backing graph by replaying the recorder, compiles it, and caches the plan.
The compiled plan snapshots its own replay baseline (in-degrees) so the fluent layer never touches
Node's scheduler-internal state. compile() drains its builder, so a fresh BackingGraph is built
whenever the topology changes.

### rearm

rearm() restores the cached plan's per-run state so it can run again. The scheduler
re-arms itself; rearm() resets the DAG-side state a previous run consumed, which
CompiledGraph owns next to the Node definition.

### set_profiling_requested

set_profiling_requested() marks the plan dirty so the next run recompiles with the new
`CompiledGraph::profiling` flag stamped in. The setting is independent of every other graph on the
same runtime.

### compiles_

compiles_ counts the times the backing graph has been compiled. A late-bound graph proves its value
by holding it at 1 across a multi-step run whose sizes and enable flags change every step.

## step_driver.hpp

### StepDriver

StepDriver is the run surface and the step loop, written once for Graph and DynamicGraph. The
owner derives from StepDriver\<Owner> and befriends it. The driver owns nothing but the marker of a
pending asynchronous step.

### What the owner supplies

| Member | Answers |
| --- | --- |
| `run_context()` | The context whose engine runs the steps |
| `has_work()`, `K_NOTHING_TO_RUN` | Whether anything is recorded, and the error text when nothing is |
| `has_dangling_resource()`, `K_DANGLING_RESOURCE` | Whether a recorded handle was freed, and the error text |
| `plan_is_stale()`, `rebuild_plan()` | Whether the plan must be built, and the build; only the build is timed as compile time |
| `prepare_run()` | What must happen after the build and before the first step |
| `run_plan()`, `run_steps()` | The plan a step runs and the per-step hooks around it |
| `rearm_plan()` | Restores the plan's per-run state before each step |
| `runs_chained()` | Whether the steps run as an in-order chain from the calling thread |
| `size()` | The task count a report multiplies by the steps run |

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

On a DynamicGraph a pending structural change (a region added or dropped) is committed before the
step is submitted, exactly as a synchronous run does.

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

### finish_async_step

finish_async_step() is the tail of drive_impl's loop body plus its report building, split out
because those happen in a different call from the submission. Advancing every State in it, and not
at submit, is what makes an asynchronous step observationally identical to a synchronous one: the
just-written field becomes current only after the step has genuinely finished. `state` is what
run_async() recorded when it submitted the step.

On a DynamicGraph every live region's States advance here, because the driver runs the hooks
aggregated from all of them.

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

### async_step_pending_

async_step_pending_ is true between run_async() and the settling of its handle. It guards the plan
against a second run re-arming nodes the workers are still walking, and drive() checks it.

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

### refresh_updaters

refresh_updaters() rebuilds the aggregated per-step updater lists from every live region. It is
cheap (proportional to the number of stateful tasks, which are iterated every step anyway) and runs
before every run, so a region added or dropped is reflected before the next run.

### DynamicGraph::steps_

steps_ is the per-step lifecycle aggregated from every live region before each run. It is the same
detail::StepMachinery the static Graph uses, so both drivers run the per-step hooks in the same
order.

### regions_

regions_ holds the live regions (the recorders), ordered by key for deterministic composition.
unique_ptr keeps Region addresses stable, so the per-step hooks a region owns can be referenced
by address across a recompose.

## region_composer.hpp

### RegionComposer

RegionComposer owns the compiled sub-graph of every region, the cross-region edges per shared
allocation and the stitched plan. commit() receives the changed and the dropped keys and a callable
that returns a region's Recorder, so the composer depends on Recorder alone. `plan_` is protected
because the test seam of the owning DynamicGraph reads it.

### add_cross_edge

add_cross_edge() adds one cross-region edge from->to for base b. A node that gains an exported
successor stops being a leaf, so it drops its sentinel link; a node that gains a cross-region
predecessor stops being a root.

### remove_cross_edge

Removing an edge can take away the last exported successor of `from`. An intra-leaf with no
cross-region successor left is a true leaf again, so remove_cross_edge links it back to the
sentinel; the sentinel must wait for it.

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

## boundary_access.hpp

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

### covered_by_any

covered_by_any() is true when one interval in `covers` fully contains `r`. Every byte of `r` is
then rewritten by that writer, and any later access to `r` is transitively ordered behind it.

### compute_boundary_access

compute_boundary_access() returns a region's per-base boundary summary. For each allocation the
region touches it classifies the region's accessor nodes into entry pins (no intra predecessor
touching an overlapping range of the base) and exit pins (whatever an intra successor's write does
not fully cover), keeping each pin's exact byte intervals.

Both classifications test overlap and not base equality, and both err toward keeping a pin. A node
wrongly excluded from entry would lose an incoming edge, and one wrongly treated as shadowed would
lose an outgoing edge. Either is a data race, while an extra pin only costs an edge.

## region.hpp

### Region

A region is the unit of mutation for the dynamic graph: a streaming open world groups its work into
spatial cells, and a cell is added or dropped as it streams in or out without recompiling the rest
of the world. A Region is a Recorder behind the add() surface of BuilderSurface, so there is one way
of recording tasks. It has no run() family: its owning DynamicGraph composes every live region's
nodes into one plan and steps them together. DynamicGraph::region() returns one.

### Shared surface

Region and Graph take every add() overload (per-element, N-D, State, capture, tracked, dynamic,
offload), the untracked, emitter and host forms and name_last() from BuilderSurface, so a region
records work exactly as a static graph does. A call on a region returns the Region. Both expose
recorder(), so a function written over a Recorder records onto either.

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
