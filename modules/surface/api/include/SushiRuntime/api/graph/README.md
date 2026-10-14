# api/graph

The chainable builders of the fluent API: Graph, Region, DynamicGraph and RunHandle.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## Which header holds what

| Header | Holds |
| --- | --- |
| `graph.hpp` | Graph: a Recorder, a PlanCache and the run entries of StepDriver |
| `dynamic_graph.hpp` | DynamicGraph: the regions by key, the pending changes and the run entries of StepDriver |
| `region.hpp` | Region: a Recorder behind the add() surface, and RegionChangeLog |
| `run_handle.hpp` | RunHandle and AsyncRunState |
| `recorder.hpp` | Recorder and Recorder::RuleScope |
| `access.hpp` | Access, what one node reads and writes |
| `device_sized.hpp` | DeviceSized, a launch that reads its live width on the device |
| `node_options.hpp` | NodeOptions: the width, the name, the operation and the parameters of a node |
| `plan_view.hpp` | PlanView and PlanNode: the copy of a plan that Graph::plan() returns |

BuilderSurface, StepDriver, PlanCache, RegionComposer and RegionChangeLog are declared in
`SushiRuntime::Detail`: Graph, DynamicGraph and Region derive from them or hold them, and a
consumer names none of them. DynamicGraph::RegionKey is the public spelling of a region's key.

The headers of BuilderSurface, StepDriver, PlanCache and RegionComposer are in `plumbing/`, and
[plumbing/README.md](plumbing/README.md) holds their sections.

The description of each node shape is in `detail/emit_*.hpp`.

No header of this folder includes a header of the engine. The members that name an engine type
are defined in `modules/surface/api/source/`: `recorder.cpp`, `plan_cache.cpp`,
`region_composer.cpp` and `step_run.cpp`. `boundary_access.hpp` and `recorder_replay.hpp` are
private headers of that folder; their sections below say so.

## graph.hpp

### Graph

Graph is three bricks behind one name. The Recorder owns what add() recorded. PlanCache, a
private base, owns the plan compiled from it. StepDriver, a public base, gives the run entries and
drives the step loop. Graph itself holds the context, forwards rule_registration(), and answers the
questions the driver asks.

### Graph: moves

A Graph moves and does not copy. The source of a move is an empty graph on the same runtime:
size(), node_count() and compile_count() are zero, it has no step hook, a run is refused with
`InvalidGraph`, and add() records onto it again. The plan moves with the rules, so the destination
does not recompile.

A graph must not be moved while a step started with run_async() is pending: the RunHandle holds the
address of the graph that started the step. A debug build asserts on such a move. A release build
gives it the result the "StepDriver: moves" section describes: the move waits for the step, and the
handle then owns nothing.

### rule_registration

A rule is what one add(), add_untracked(), add_task() or add_host() call records, or what one
extend() call or Recorder::rule() scope records. size() counts rules, node_count() counts the nodes
they lowered to, and rule_registration(rule) takes a position below size(); last_rule() is the
position of the rule just added. RunReport numbers a node event by registration index, and every
node of a rule carries the rule's index, so rule_registration() maps a record back to the call that
recorded it. The indices are drawn when the graph compiles, so every rule reports
`K_NO_REGISTRATION` until the first run, and a recompile may renumber them.

DynamicGraph::rule_registration(key, rule) answers the same for a rule of one region. A commit
renumbers every region, so the answer is read from the node at the time of the call.

### plan

plan() returns a PlanView: one PlanNode per recorded node, in recording order. It calls compile()
first, so it builds the plan when none is current and returns compile()'s Error when there is
none to build. A run after it uses the plan it built.

```cpp
auto plan = graph.plan();
if (!plan)
    return report(plan.error());
for (const SushiRuntime::PlanNode& node : plan->nodes)
    std::printf("%s waits on %zu
", node.name.c_str(), node.predecessor_count);
```

| Field | Holds |
| --- | --- |
| `rule` | Position of the rule that recorded the node, the value rule_registration() takes |
| `name` | The label of the node's row in a RunReport |
| `operation` | The OperationID the node's NodeOptions named; a default id when it named none |
| `predecessor_count` | How many nodes of the plan must finish before this one starts |
| `reads`, `writes` | The `BufferRegion`s the node named, in the order it named them |

The view is a copy. It holds no pointer into the plan, so it stays readable after the graph
recompiles or is destroyed, and it shows the plan as it was when plan() returned. A State rule
reads the field that is current when the plan is built, so its regions change with a rebuild
after an odd number of steps. A test reads the order of a graph from here and needs no engine
header.

DynamicGraph has no plan(): its plan is stitched from regions, and a node there also waits on
nodes of other regions.

### Friends

Runtime constructs a Graph. StepDriver\<Graph> reads the plan and calls the private members named
in the step_driver.hpp section. The two test seams read compiled_plan(). Region and DynamicGraph are not
friends: a Region owns its own Recorder, and DynamicGraph reads a region through
Region::recorder().

## recorder.hpp

### Recorder

A Recorder is the single owner of recorded work: the nodes in recording order, the rules they
belong to and the rules' names, one liveness observer per allocation the nodes name, the USM scratch
they use and their per-step hooks. Graph and Region each hold one. Every recording member builds a
replay callable and ends in the private record(), which appends the node, attaches it to the open
rule or starts a rule for it, takes the observers and advances generation(). A domain library
records through the public members and needs no friendship.

Every recording member returns the position of the rule its node belongs to, the value
rule_registration() takes. A library that records several operations keeps the positions and
reads each operation's node events back after a run, with no count of its own to maintain. The
nodes recorded inside one rule scope return the same position. The builder verbs return the
builder, so a chain reads the same value from last_rule().

### Recorder: moves

Only a Graph and a Region start a Recorder and move one: the constructor from a ContextHandle, the
move constructor and the move assignment are private, with those two as friends. Graph::recorder()
and Region::recorder() hand out a reference a consumer records on, and a consumer that could
assign one recorder into another would replace the nodes under a plan its graph still holds.

The source of a move is an empty recorder on the same context: no node, no rule, no scratch and no
observer. It keeps the context so that it can record again. No rule scope may be open on either
side, because a RuleScope holds the recorder's address.

generation() never returns to a value the recorder had. The source's advances by one, so a plan
built from it before the move is stale. The destination takes the source's generation when that is
larger than its own, which holds for every move construction from a recorder that has recorded, so
the plan that moves with a Graph stays current. Otherwise the destination's advances by one and
its owner rebuilds once.

### RuleScope

Every node belongs to one rule. rule() opens a scope, and the nodes recorded until it ends form one
rule; a node recorded outside a scope is a rule of its own. At replay the recorder reserves one
registration index on the backing graph when the first node of a rule is added and stamps it on all
of them. The nodes of a rule are contiguous, so the replay needs two locals and no allocation. A
scope that records nothing adds no rule.

### name_last_rule

name_last_rule() names the last rule and copies the text, so the caller's string may be a temporary.
The copies live in a node-based set, one per distinct label: a compiled node keeps a `const char*`
to its name, and an entry of that set never moves when another label is added or the recorder is
moved. The rule keeps the pointer. The replay stamps it on every node the rule lowers to, which is
why a name given after the last node of a scope still reaches the first. A node that stated a
name through NodeOptions keeps its own. A rename advances generation(), so the next run rebuilds
the plan with the new name.

### clear

clear() forgets the nodes, the rules, the labels, the observers, the hooks, the scratch and the
refusal, and advances generation() instead of resetting it, so a plan built before the call is
seen as stale. The scratch is freed at once: a plan built from the cleared nodes must not run
again, which the owner guarantees by rebuilding whenever generation() changed.

clear() may be called with a rule scope open, which is how an extension that extend() handed the
recorder reaches it. The scope stays open, and the next node recorded inside it starts a new rule.

Graph::clear() calls it and PlanCache::discard(), which marks the plan stale and forgets the
registration of every rule, so rule_registration() answers `K_NO_REGISTRATION` until the next
build. The plan itself is freed by that build or with the graph. Region::clear() calls it
through recorder(), which lists the region as written to.

### refusal

A recording member returns the position of a rule and a builder verb returns the builder, so
neither has a return value that could carry a failure. A recording the recorder refuses is
therefore kept: the recorder stores the `Error` of the first one, records nothing for that call,
and returns rule_count() from it. refusal() returns the stored error, or null when nothing was
refused. Its message opens with the call as the caller wrote it and the position the rule would
have had, such as `add_rule(launch, state, kernel), rule 1: `.

Three checks refuse a recording, each a function that returns `std::optional<Error>`:
launch_refusal() and parameters_refusal() of `api/detail/`, and the recorder's own
empty_state_refusal(), which answers `InvalidAccess` for a rule over a State that owns no
memory. admit() keeps what they return. A verb that changes the step machinery beyond its node
asks them before it changes anything, so a refused State rule registers no advance.

A refusal is for what the caller cannot know when it writes the call: the size a handle has at
run time, how many parameters a domain library passes on. A rule the caller broke stops the
process instead, as it does in Runtime::buffer(): a placement on a device index the runtime does
not drive stops in placed_device() through `Detail::require_device()`, and a device slot that
is not host USM of this runtime stops in require_device_slots().

The refusal stays until clear(), and a later refusal does not replace it. Every run entry of the
owning graph returns it before anything else is checked and runs nothing; see entry_refusal. A
caller that wants to know earlier reads refusal() after the verbs.

### record: liveness

record() takes the liveness observers of the typed handles a node names, so a run can detect a
handle freed before it ran. An Access built from Reads and Writes carries them; one built from
bare regions carries none, because naming data by raw pointer opts out of lifetime tracking, by
design.

The recorder keeps one observer per allocation. The list is sorted by owner (the control block two
observers of one handle share), so a node that names a handle already watched costs one binary
search and adds nothing. A graph of 10 000 nodes over three buffers holds three observers.
watched_allocation_count() reports the length.

### has_dangling_resource

has_dangling_resource() is true when the allocation behind any recorded handle has been freed. The
graph checks it once per run, so a graph that outlives its data fails cleanly instead of dispatching
against released memory. The walk is as long as the number of distinct allocations.

### RecorderReplay

RecorderReplay::replay() of the private header `source/recorder_replay.hpp` adds every recorded
node to a backing graph in recording order, names each after its rule and writes on it what its
NodeOptions stated. PlanCache::build() and the composer's build_region() both call it, so a
static graph and a region are replayed by the same code. The visitor reports each node, with the
position of its rule, once it is added; PlanCache uses it to read back the registration index of
each rule, and the composer to remember a node of each rule.

RecorderReplay is the Recorder's one friend. It is the one place that reads the recorded
descriptions and names the engine's TaskGraph, and it is not installed: every recorded node is one
the public members recorded, and each adds exactly one node to the backing graph.

### record

record() is where every recording member ends. It takes a NodeDescription
(detail/README.md), which the member's header template built from the caller's kernel, and
appends it with its stamp and its rule. It is compiled into the library, as are the members that
ask the runtime for something: scratch_block() behind scratch(), the placement and device slot
refusals, and the fault records.

### kernel(DeviceSized, ...)

A DeviceSized launch always runs `capacity` work-items, so its width is part of the compiled plan
and the live width can come from a word a predecessor wrote in the same step. A work-item at or
past `*live_width` returns at once; a null `live_width` lets every item run. The kernel's arity
picks what it receives after its index:

| Callable | Receives |
| --- | --- |
| `kernel(t)` | The index |
| `kernel(t, w0)` | The index and `live_words[0]` |
| `kernel(t, w0, w1)` | The index, `live_words[0]` and `live_words[1]` |

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
is written to the resolver's fault record at submission and the fill does not run, and a device
count above its capacity is replaced by zero and written to the same record; the step driver
reads that record when the step ends and returns `InvalidGraph` from the run.

### scratch

scratch() hands out USM the recorder owns. It is allocated when the node is recorded, not when the
plan compiles, so the pointer a kernel captured stays valid across a recompile and across the
composer rebuilding a region. Fault records come from the same store. scratch() returns
`Result<T*>`, with `AllocationFailed` when the device has no room. A recording verb that needs a
fault record and cannot get one keeps that error as the recorder's refusal.

### steps

steps_ holds the per-step lifecycle: the resync and the advance of each State view recorded, the
late-binding refreshers and the hooks that re-point the live fields of a rule that names an
operation. Each view belongs to its State. It owns the ordering invariant that runs around
each replayed step. The DynamicGraph composer uses the same type, so both drive the loop
identically.

### dynamic_kernel(Dynamic, State, kernel)

The late-bound State rule mirrors dynamic_kernel(Dynamic, capacity, ...) but reads the State's
current and next
pointers fresh each submit, following the swap, and registers the once-per-step advance, so a
stepped field can vary its active count or sleep entirely with no recompile.

### A rule that names an operation

A node names its operation in `NodeOptions::operation`, on any verb; there is no verb of its
own for it. state_rule(), dynamic_kernel(Dynamic, State, kernel) and coupled_rule() do one thing
more for such a rule: live_fields_of() builds the lists of the fields the rule reads and writes,
registers the hook that rewrites them before each step, and the node's description carries
them as `Detail::LiveFields` (`api/detail/node_description.hpp`) to the replay.
A rule that names no operation registers nothing.

## node_options.hpp

### NodeOptions

NodeOptions is what a caller may state about one node beyond its launch and its access. Every
verb of the Recorder and of a builder takes one as its last argument, and a default one states
nothing.

| Field | Lands | When it is not stated |
| --- | --- | --- |
| `width` | `launched_width` and `live_width` of the node's event records | The shape's own width, listed in `api/detail/README.md` |
| `name` | `NodeTiming::name` of a profiled run | The name of the node's rule, or "unnamed_task" |
| `operation` | `NodeTiming::operation`, and the engine's metadata of the node | No operation |
| `parameters` | The engine's metadata of the node | No parameter |

The recorder copies `name` and `parameters` when the node is recorded, so a temporary string and
a local array are valid arguments. A node carries `NodeOptions::K_MAX_PARAMETERS` parameters,
eight; a longer list is refused with `InvalidGraph` and records nothing. A domain library that
fills the list from its own table asserts against the constant at compile time.

A node that names an operation is one another machine may run, and nothing else changes for
it. On a runtime with a cluster attached the node is a candidate for offload when a worker
registered a kernel for that operation and every buffer the node touches is shared. On every
other runtime, and for every other operation, it runs exactly as an unnamed node does, on the
chain layer or on the scheduler.

NodeOptions is an aggregate, and every field has a default member initializer. `NodeOptions{width}`
therefore states a width alone, also in a build that turns `-Wmissing-field-initializers` into an
error; a caller that states a later field alone assigns it to a default-constructed object.

## dynamic_graph.hpp

### DynamicGraph

DynamicGraph is the mutable counterpart to the static Graph, built for an open world: work is
grouped into regions keyed by a caller id (a spatial cell, a streaming chunk). region(key) records
tasks with the static graph's add() surface; drop(key) removes one. Mutations are applied at a step
boundary, never against the running DAG, so the lock-free scheduler is untouched.

A commit has two cost terms. Building and wiring cost O(changed regions + the boundary pins of the
allocations they share): each region compiles to a cached sub-graph whose interior (intra-region)
edges are baked once and stay as they are while the region is unchanged, and dependencies that cross
a region boundary, where two regions share a buffer, live in a separate layer on each node
(Node::region, a RegionLink) that the composer maintains. When a region streams in or out only its
own nodes are rebuilt and only the boundary edges of the allocations it shares are derived again.
The second term covers the whole graph: every commit renumbers the registration index of every node
of every live region, O(total nodes) with one map allocated per region, and copies the root list,
O(roots). A graph that requests profiling also lists every node again.

Cross-region ordering follows ascending region key: for an allocation two regions share, the
lower-keyed region is the producer. Read/read sharing stays parallel (no edge between two regions
that only read the same buffer); only true RAW/WAR/WAW hazards across regions are serialized. Tasks
within one region keep their add() order.

Sharing is decided on **byte intervals, not allocations**. Two regions that touch disjoint element
ranges of one buffer gain no edge, which is what makes one region per simulation island viable:
islands are disjoint index ranges of a shared entity column, not separate allocations, so an
allocation-granular boundary would serialize islands that never meet. A caller names a slice with
Buffer::region({offset, count}) and passes it to Reads(...)/Writes(...) exactly as within one graph.

Runtime::dynamic_graph() returns a DynamicGraph. It moves and does not copy. The source of a move
has no region and a compose count of zero on the same runtime, and takes regions again; the regions
and the log they report to move together. As for Graph, it must not be moved while a step started
with run_async() is pending; the "StepDriver: moves" section gives the result in each build.

### region

The returned region records work with the same add() surface as a static graph. region(key) is not
a mutation: reading a region (`world.region(k).size()`) between two runs leaves compose_count()
unchanged. A region is rebuilt at the next run when it was created, when something was recorded on
it, when it was cleared or when one of its rules was given another name.

### plan_is_stale

A Region lists itself in the graph's RegionChangeLog the first time its recorder is reached for
writing after a run. plan_is_stale(), which the driver calls once per run entry, walks that list
and marks dirty each listed region whose recorder generation differs from the generation the
composer built. The cost follows the regions written to since the last run, not the size of the
world, and nothing runs per step. A recording through a `Region&` held across runs is therefore
seen, because it passes through Region::recorder().

### drop

drop() takes effect on the structure immediately: region(), has_region() and region_count() no
longer see the region, and region(key) after it creates a new, empty one. Dropping an absent region
does nothing.

The Region object is not destroyed by the call. drop() moves it into `retired_`, and the boundary
it leaves is re-stitched at the next run, so drop() may be called while a step started with
run_async() is pending. Until that next run the composed plan still holds the region's nodes, and
those nodes and the pending step read four things the Region owns: the scratch and the fault
records its kernels captured, the labels its nodes name by `const char*`, and the StepMachinery
that `steps_` lists by address and that settling the step walks for take_fault() and
after_step().

rebuild_plan() frees the retired regions after commit() has returned, through
free_retired_regions(). By then no step is pending, because entry_refusal() refuses a run entry
while one is, and the plan holds no node of a dropped region. A commit that throws leaves them
retired until one succeeds. free_retired_regions() first empties the aggregation in `steps_`,
which still lists the retired regions' hooks; prepare_run() gathers it again from the live regions
before the first step. A graph that drops regions and never runs keeps every one of them.

Region::clear() frees the same scratch at once, so it keeps its precondition: no step started with
run_async() on the owning graph is pending.

### request_profiling

request_profiling() stores the request behind set_profiling() in the composer, which is its only
owner: RegionComposer::set_profiling() keeps it and publish_plan() stamps it into the plan at the
next commit. A value that differs from the stored one also sets a marker that makes the next build
mark every live region dirty, so that commit renumbers every node's public registration index. The
marking waits for the build because it allocates and the setter is `noexcept`. The value already
stored changes nothing and composes nothing. The setting is independent of every other graph on the
same runtime.

RegionComposer's pair and StepDriver's pair have the same names, so DynamicGraph names StepDriver's
with two using-declarations, as Graph does.

### refresh_updaters

refresh_updaters() clears the aggregated list in `steps_` and appends one pointer per live region,
whether or not the region has a hook. prepare_run() calls it once per run entry, so a run entry
costs R appends for R live regions, into storage the list keeps between runs. Each step then walks
the R pointers three times, for before_step(), take_fault() and after_step(), and each run entry
once more for resync(); a region with no hook costs a call that iterates empty lists. The cost has
not been measured.

### DynamicGraph::steps_

steps_ is the per-step lifecycle aggregated from every live region before each run. It is the same
Detail::StepMachinery the static Graph uses, so both drivers run the per-step hooks in the same
order.

### regions_

regions_ holds the live regions (the recorders), ordered by key for deterministic composition.
unique_ptr keeps Region addresses stable, so the per-step hooks a region owns can be referenced
by address across a recompose.

### retired_

retired_ holds the regions drop() removed since the last commit, in drop order. A key dropped and
recorded again before the next run has its new Region in `regions_` and its old one here. The "drop"
section says what still reads a retired region and when the list is emptied. A move takes the list
with the regions.

### free_retired_regions

free_retired_regions() empties the aggregation in `steps_` and then destroys the retired regions,
in that order, so `steps_` never lists a freed StepMachinery.

### changes_

changes_ is held by unique_ptr because every region keeps its address, and a DynamicGraph moves.
It is created with the first region.

## boundary_access.hpp

A private header of `source/`.

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
region touches it gives every accessor node an entry pin and an exit pin, each holding the intervals
of the node that the rules below keep. A node whose intervals are all dropped has no pin. Both rules
look at direct intra neighbours only and count an interval as covered only when one interval of a
neighbour fully contains it; an interval is never split.

Entry (entry_pin()). A read of the node is dropped when a direct intra predecessor reads or writes
an interval of the base that contains it. A write of the node is dropped only when a direct intra
predecessor writes an interval that contains it. The predecessor then carries the cross-region edges
the node needs: its containing access is pinned itself, or is covered in turn by one of its own
predecessors, and the node runs after all of them. A predecessor that only reads does not cover a
write, because a write must also follow the readers of earlier regions and a reader has no edge
from them. With `p` writing `b[0,10)` and `node` reading `b[5,20)` in one region, `node` keeps a read pin
for `[5,20)`, so a writer of `b[15,20)` in an earlier region gains an edge to `node`.

Exit (exit_pin()). A read or a write of the node is dropped when a direct intra successor writes an
interval that contains it. A later region that overlaps the dropped interval overlaps the
successor's write too and is ordered behind the successor, which already runs after the node.

Both rules keep a pin when in doubt: an indirect predecessor, or two neighbours that cover an
interval only between them, leave the pin in place. That adds an edge at most. A pin dropped in
error would remove an edge and leave two tasks racing on the same bytes.

For one base the function costs O(E) to list the neighbours, with E the intra edges that leave the
base's accessor nodes, and then compares every interval of a node with every interval of each direct
neighbour. It reads the nodes of one region and nothing else.

## region.hpp

### Region

A region is the unit of mutation for the dynamic graph: a streaming open world groups its work into
spatial cells, and a cell is added or dropped as it streams in or out without recompiling the rest
of the world. A Region is a Recorder behind the add() surface of BuilderSurface, so there is one way
of recording tasks. It has no run() family: its owning DynamicGraph composes every live region's
nodes into one plan and steps them together. DynamicGraph::region() returns one.

A Region is neither copyable nor movable: its graph holds it by address, and so do the per-step
hooks the graph aggregates. clear() empties it so that its work can be recorded afresh without
drop() and region(). clear() frees the scratch at once and must not be called while a step started
with run_async() on the owning graph is pending; drop() may be.

### RegionChangeLog

RegionChangeLog is the line from a region to its graph. Region::recorder(), the non-const one that
every add() and clear() goes through, lists the region's key in the log, once per interval between
two runs; each region keeps the interval it was last listed in, so a second recording costs one
compare. The const recorder() lists nothing, which is why size() and node_count() are free, and
DynamicGraph reads every region through its private recorded(), which takes the region as const. A
`Recorder&` taken from a region must not be kept across a run: a recording through it is not listed.
A Region built without a log belongs to no graph and lists nothing.

### Shared surface

Region and Graph take every verb (add, add_rule, add_coupled, add_dynamic, add_untracked,
add_task, add_host, extend) and name() from BuilderSurface, so a region
records work exactly as a static graph does. A call on a region returns the Region. Both expose
recorder(), so a function written over a Recorder records onto either.

## run_handle.hpp

### AsyncRunState

A synchronous run reads its "before" counters and its start time as locals, because the whole run
happens inside one function. An asynchronous run does not: the step is submitted in one call and
settled in another, so the same values have to travel in the handle. Every member of AsyncRunState
is a plain value. `serial` is the number the driver gave the step; the handle compares it with the
number in the graph's AsyncStepRecord to learn whether its step is still pending.

### RunHandle

`run()` blocks for the whole step, which is correct for a fixed-step simulation thread but costs
**overlap**: the next tick cannot begin while the previous one's state is being extracted for a
renderer or an audio system, even though that extract needs no worker threads. `run_async()` returns
a RunHandle instead. The work is already proceeding on the worker pool, so the caller can do that
extract and then settle up:

```cpp
auto step = g.run_async().value();

// The caller is still running: this is the work a frame would overlap.
released.store(true, std::memory_order_release);

const RunReport report = step.wait().value();
```

The block is from `tests/surface/api/graph/test_run_handle.cpp`.

The handle is move-only and owns the obligation to finish the step: its destructor waits if `wait()`
was not called, because the compiled plan must not be destroyed while workers are still touching its
nodes. That makes the safe usage the natural one: declare the handle after the graph and ordinary
scoping does the rest. It also means a discarded handle discards the step's outcome, **including a
task exception**. Call `wait()` (or `wait()`) whenever the result matters, which is nearly
always.

Exactly one run may be in flight per runtime. While a handle is pending, every run entry of every
graph on the runtime is refused with `InvalidState`, as the entry_refusal section describes.

### operator=(RunHandle&&)

Assigning over a pending handle would otherwise abandon a running step with nobody left to wait for
it. The moved-from handle owns nothing afterwards.

### RunHandle: the owner's address

The handle keeps the address of the graph that started the step and a share of that graph's
AsyncStepRecord. It reads the graph only while the record names the handle's own step as pending,
and the graph clears the record before it moves or dies, so a handle never reads a dead graph.

A graph must not be moved while its handle is pending: Graph and DynamicGraph state it as a
precondition of their move operations, and the "StepDriver: moves" section says what a move does
when it is broken. A graph may be destroyed while its handle is pending. Its destructor waits for
the step, advances the States and drops the report, as destroying the handle would.

After either, the handle owns no step: pending() is false, ready() is true, its destructor does
nothing, and wait() returns `InvalidState` with a message that says the graph was
moved or destroyed and the report dropped. The handle costs one shared pointer more than the
values it carries, and no allocation: the record is the graph's.

### pending

pending() is true while the handle's step is the one pending on its graph. It turns false when the
step is waited for, when the handle is moved from, and when the graph ends the step by being moved
or destroyed.

### ready

ready() answers "has the graph finished", so a frame loop can keep overlapping work instead of
blocking early. It is not a substitute for wait(): the pool still has bookkeeping to drain and the
step's state advance has not happened yet. It asks the engine only while the handle owns its step;
a handle that owns none is ready, whatever run the engine holds.

### wait()

wait() completes the step's lifecycle, in which every State advances to the field it just wrote, and
consumes the handle, so a second call returns `InvalidState` instead of waiting on a step that is no
longer running. A task exception arrives as `KernelFailed` carrying the task's text, the same as
from run(), and the state advance is skipped in that case, exactly as a failed synchronous run of
one step leaves its states untouched.

wait() resets the report, consumes the handle, checks that the handle still owns its step and asks
the driver to settle it. It is `noexcept` and returns the report or the Error that ended the step.

### wait(RunReport&)

wait(RunReport&) is the allocation-free counterpart of wait(), for the same reason
Graph::run(RunReport&) exists: an overlapped frame loop settles a step every tick, and a report it
already owns costs nothing to refill. The report is reset before it is written, so a stale field
from the previous step can never survive into this one.

### wait: consuming the handle

wait() consumes the handle before the blocking call: if a task threw, the step is still over,
and a destructor that tried to wait again would block forever on a latch nobody will signal.

### wait: clearing the report

wait() clears the report before anything else, so a step that ends in a task exception, or a
handle that owns no step, cannot leave the previous step's numbers readable in a report the caller
reuses. finish_step sets the flag back on success.

### settle

settle() finishes a step nobody waited for by calling wait() into a report it discards. A
destructor may not throw, so the outcome, including a task exception, is dropped; that is the
documented cost of discarding a handle, and it is still far better than returning while workers
hold pointers into the plan.
