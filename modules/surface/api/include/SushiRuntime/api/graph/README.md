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
| `builder_surface.hpp` | BuilderSurface: add(), add_untracked(), add_task(), add_host(), extend(), name(), size(), node_count() and last_rule() |
| `plan_cache.hpp` | PlanCache: the compiled plan of one recorder |
| `step_driver.hpp` | StepDriver: run(), run_for(), run_until(), run_async(), compile(), before_each_step(), cancel(), set_profiling() and the step loop |
| `boundary_access.hpp` | Pin, BaseAccess and compute_boundary_access() |
| `region_composer.hpp` | RegionComposer: the cached sub-graph of each region and the stitched plan |

The replay callable of each node shape is in `detail/emit_*.hpp`.

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

### Friends

Runtime constructs a Graph. StepDriver\<Graph> reads the plan and calls the private members named
in the step_driver.hpp section. The two test seams read `plan_`. Region and DynamicGraph are not
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
of them. The nodes of a rule are contiguous, so replay() needs two locals and no allocation. A
scope that records nothing adds no rule.

### name_last_rule

name_last_rule() names the last rule and copies the text, so the caller's string may be a temporary.
The copies live in a node-based set, one per distinct label: a compiled node keeps a `const char*`
to its name, and an entry of that set never moves when another label is added or the recorder is
moved. The rule keeps the pointer. replay() stamps it on every node the rule lowers to, which is
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

### refusal

A recording member returns the position of a rule and a builder verb returns the builder, so
neither has a return value that could carry a failure. A recording the recorder refuses is
therefore kept: the recorder stores the `Error` of the first one, records nothing for that call,
and returns rule_count() from it. refusal() returns the stored error, or null when nothing was
refused. Its message opens with the call as the caller wrote it and the position the rule would
have had, such as `add_rule(launch, state, kernel), rule 1: `.

Four checks refuse a recording, each a function that returns `std::optional<Error>`:
launch_refusal(), parameters_refusal() and
host_device_slots_refusal() of `api/detail/`, and the recorder's own placement_refusal(). admit()
keeps what they return. A verb that changes the step machinery beyond its node asks them before
it changes anything, so a refused State rule registers no advance.

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

### replay

replay() adds every recorded node to a backing graph in recording order, names each after its
rule and writes on it what its NodeOptions stated. PlanCache::build() and
RegionComposer::build_region() both call it, so a static graph and a region are replayed by the
same code. The visitor reports each node, with the position of its rule, once it is added;
PlanCache uses it to read back the registration index of each rule, and the composer to remember
a node of each rule.

replay() is private, and PlanCache and RegionComposer are the Recorder's two friends. It is the
one member of the Recorder whose signature names the engine's TaskGraph, and a caller of the
surface has no use for it: every recorded node is one the public members recorded, and each adds
exactly one node to the backing graph.

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
registers the hook that rewrites them before each step, and
`Detail::with_live_fields()` of `api/detail/live_fields.hpp` hands them to the node at replay.
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
a local array are valid arguments. A node carries eight parameters; a longer list is refused with
`InvalidGraph` and records nothing.

A node that names an operation is one another machine may run, and nothing else changes for
it. On a runtime with a cluster attached the node is a candidate for offload when a worker
registered a kernel for that operation and every buffer the node touches is shared. On every
other runtime, and for every other operation, it runs exactly as an unnamed node does, on the
chain layer or on the scheduler.

NodeOptions is an aggregate, and every field has a default member initializer. `NodeOptions{width}`
therefore states a width alone, also in a build that turns `-Wmissing-field-initializers` into an
error; a caller that states a later field alone assigns it to a default-constructed object.

## builder_surface.hpp

### BuilderSurface

BuilderSurface is the overload set of add() and its siblings, written once for Graph and
Region. Each overload checks its callable, calls one recording member of the builder's Recorder
and returns the builder, so calls chain on the type the caller holds. No overload holds logic of
its own: what a launch does is in `api/detail/emit_launch.hpp`, and what a rule records is in the
Recorder. refusal() returns the recorder's refusal: the first recording a verb of this builder had
refused since its recorder was cleared.

### Argument order

Every call reads in one order: modifier, launch, access, callable, options.

| Part | What it is | Spellings |
| --- | --- | --- |
| modifier | changes how the rule is run | `Dynamic` |
| launch | how many work-items, and their index | a count, `Extent<N>`, `NdRange<N>`; the whole field when a State rule gives none |
| access | the data, and how the kernel gets it | `Reads` and `Writes`, `In`/`Out`/`InOut`, a `State`, `Coupled`, `On` |
| callable | the kernel | a lambda |
| options | what the node states about itself | a `NodeOptions` |

Up to v0.3 the launch came third after Reads and Writes, second after a State, and first before
captures. A caller had to remember which, and a new launch shape needed one overload per access.
The launch and the access are now two independent descriptors, so each launch works with each
access:

| Access | count | Extent | NdRange | whole field | Dynamic |
| --- | --- | --- | --- | --- | --- |
| `Reads`, `Writes` | yes | yes | yes | | yes, with a capacity |
| `In`, `Out`, `InOut` | yes | yes | yes | | |
| `State` | yes | yes | yes | yes | yes |
| `Coupled` | yes | yes | yes | yes | |
| none (add_untracked) | yes | yes | yes | | |

Late binding exists for `Reads` and `Writes` (with a capacity) and for `State` only. The capture
(`In`, `Out`, `InOut`) and `Coupled` forms have no Dynamic overload. For a varying size there, use
`Reads` and `Writes` with the pointers captured by hand; for one State, `add_dynamic(Dynamic, state, kernel)`.

### One verb per kind of node

| Verb | Records | Recorder member |
| --- | --- | --- |
| `add(launch, reads, writes, kernel[, options])`, `add(launch, captures..., kernel[, options])` | A tracked kernel | `kernel` |
| `add_rule(state, kernel[, options])`, `add_rule(launch, state, kernel[, options])` | A rule over one `State` | `state_rule` |
| `add_coupled(Coupled(...), kernel[, options])`, `add_coupled(launch, Coupled(...), kernel[, options])` | A rule over several states | `coupled_rule` |
| `add_dynamic(Dynamic, capacity, reads, writes, kernel[, options])`, `add_dynamic(Dynamic, state, kernel[, options])` | A late-bound window | `dynamic_kernel` |
| `add_task(reads, writes, body[, options])` | A body that takes the queue | `task` |
| `add_host(reads, writes, body[, options])` | Host work | `host` |
| `add_untracked(...)` | A kernel with no declared access | `untracked_kernel` |
| `extend(extension)` | What an extension records through `Recorder&` | (none) |

`add` has three templates: the two tracked forms above and `add(reads, writes, kernel)`, whose
kernel takes the `sycl::handler` and is recorded by `raw_kernel`. A wrong argument to `add`
therefore names at most three candidates in the compiler's message. An offload is not a verb:
`NodeOptions::operation` names the operation on any of them.

### Which verb do I call

| The kernel | Call |
| --- | --- |
| steps one field from its previous value | `add_rule(state, kernel)` |
| steps one field and reads others | `add_coupled(Coupled(out, in0, ...), kernel)` |
| does either over part of the field | the same with a launch first: `add_rule(Extent{...}.at(origin), state, kernel)` |
| reads and writes buffers, and should receive their pointers | `add(launch, In(a), Out(b), kernel)` |
| captures its own pointers | `add(launch, Reads(a), Writes(b), kernel)` |
| has a live size or an on/off switch that changes per step | `add_dynamic(sized(...), capacity, Reads(a), Writes(b), kernel)` or `add_dynamic(sized(...), state, kernel)` |
| may run on another machine | any verb, with `NodeOptions::operation` set to `"name"_op` |
| drives the sycl::handler itself | `add(Reads(a), Writes(b), kernel)` |
| submits several command groups, or calls a library that takes the queue | `add_task(Reads(a), Writes(b), body)` |
| is host C++ and submits no device work | `add_host(Reads(a), Writes(b), body)` |
| shares no data with any other rule | `add_untracked(launch, kernel)`, with `On(buffer)` when the buffer is not on device 0 |
| is recorded by a library through the Recorder | `extend(extension)` |

In/Out/InOut is the form to prefer for buffers: the data is named once, and the pointers the
kernel receives are the ones the runtime orders by. With Reads and Writes the kernel captures its
pointers by hand, and the two lists can get out of step.

### A wrong callable

A callable whose signature does not fit fails one `static_assert` in
`api/detail/callable_checks.hpp`. Its text states the signature the call expects, and it is the
only error: the overload does not instantiate the recording call after a failed check. The
overloads are selected by the launch and the access alone, so a mistyped callable can no longer
empty the overload set.

### add(launch, Reads, Writes, kernel)

The kernel captures raw USM pointers, and Reads(...) and Writes(...) tell the dependency tracker
which Buffers, States or regions those pointers belong to. A count calls `kernel(i)`, an Extent calls
`kernel(sycl::id<N>)` with the origin added, and an NdRange calls `kernel(sycl::nd_item<N>)` or
`kernel(sycl::nd_item<N>, sycl::id<N>)`.

A count also takes a range kernel, `kernel(begin, end, sycl::handler&)`, which drives the handler over
[0, n) itself. That is the form for a kernel the per-element shape cannot express while its
regions still order it.

### add(Reads, Writes, raw_kernel)

`kernel(sycl::handler&)` owns the whole command group (its own parallel_for, local accessors, a
memcpy) while the declared regions still order it. It has no launch because the runtime submits
none. The shaped overloads are the better choice wherever one fits, since they cannot get the
launch geometry and the declared data out of step.

### add(launch, In/Out/InOut..., kernel)

The caller passes the buffers a kernel touches as In(buf), Out(buf) or InOut(buf). The runtime
registers the reads and writes and hands `kernel` the raw element pointers, in argument order, after
the index. Up to v0.3 this form existed for an Extent only, so 1-D code wrote `Extent{n}` to get
it; a count and an NdRange now take it too.

The first capture is a parameter of its own, which guarantees at least one. The rest is one
trailing pack so that the compiler can deduce it: it ends in the callable, or in the callable and
a NodeOptions, and the elements before the callable are further captures. add() splits the pack,
checks that everything before the callable is a capture, and the call site reads
`In(a), Out(c), kernel` or `In(a), Out(c), kernel, options`. The options must be written as a
`NodeOptions` object there, because a braced list cannot be deduced into a pack.

### add_rule(state, kernel)

This is the canonical stepped rule: it reads the State's current field and writes its next field,
and the dependency tracker keys on those two buffers. The runtime supplies the current and next
pointers to `kernel` at execution time, so under run(n) the same compiled node follows the
double-buffer swap with no recompilation. A State takes exactly one update rule per step.

### add_rule(launch, state, kernel)

The launch form runs the same rule over a count, an Extent or an NdRange: a prefix of the field,
a sub-box for boundary or halo handling, or a tiled launch. `kernel` receives the launch's index
before the two pointers.

The launch is compared with the State when the rule is added. A launch that reaches past the
State is refused with `InvalidGraph`, naming the rule, the launch with its origin and
the State's size; `api/detail/README.md` says what "reaches" means for each launch.

### add_coupled(Coupled, kernel) and add_coupled(launch, Coupled, kernel)

A coupled rule writes the next field of one State from its own current field and the current
field of one or more others. It is the building block for coupled systems such as
reaction-diffusion with several species. All reads see the previous step's data (a Jacobi
update): every State advances only after all of a step's kernels have run. Each field takes
exactly one update rule per step.

The whole-field form iterates the size of the State written, and add() refuses the rule with
`InvalidGraph` when a State read is smaller than that. The launch form compares the launch with
every State
named.

### add_dynamic(Dynamic, capacity, Reads, Writes, kernel)

The rule is compiled once, and the Dynamic built from when() and sized() lets its iteration size
and whether it runs vary every step with no recompile. The buffers are allocated for `capacity`
elements; sized() iterates the live count each step, and when() can skip the rule.

sized_from_device() takes the count from a slot a predecessor kernel wrote instead of from the
host, which is what lets a produce-then-consume chain (broadphase to narrowphase to solver) run in
one composition with no worst-case launch width. The counter is registered as one of this rule's
reads, so the producer is ordered before it.

based_at() and based_at_device() move where the rule starts, for a slice whose offset shifts
between steps, such as a graph colour. The callable still receives absolute indices, and the
window must fit the capacity: one that does not fails the run.

### add_dynamic(Dynamic, state, kernel)

This is the late-bound form of add_rule(state, kernel). The State's full allocation is the capacity;
sized() iterates a prefix of it (a changing number of active cells, for example), and when() can
skip the step's kernel. The double-buffer swap is followed as in the static form.

The State still advances (swaps current and next) on a step its when() disabled, so a skipped
step leaves the un-written next field current. This overload suits size variation; sleeping a
field across steps belongs to the streaming layer, not to when().

### add_untracked(launch, kernel)

add_untracked() records no read or write region: the rule is both a root and a leaf, ordered
against nothing. That is correct for self-contained work (seeding a buffer no other rule of this
graph reads, a benchmark kernel) and wrong the moment another rule shares data with it. Whenever
ordering matters, name the data and use a tracked verb.

The launch is a count, an Extent or an NdRange, and the kernel receives the same index as in
add(). A count also takes a range kernel, and add_untracked(kernel) takes a raw kernel.

### add_untracked(launch, On, kernel)

The runtime places a rule on the device that owns the first region it writes. An untracked rule
names no region, so it states its device: On(buffer) and On(state) name the device of a handle
and watch the handle, On(DeviceIndex) names a device. Without an On the rule runs on device 0,
where Runtime::buffer() and Runtime::state() allocate by default. An On that names a device the
runtime does not have is refused with `InvalidAccess`.

### add_task(Reads, Writes, body)

A raw kernel is one command group: the runtime calls `queue.submit()` and hands `kernel` the handler.
A task body is handed the queue itself, `kernel(sycl::queue&, const std::vector<sycl::event>&)`, so
it may submit any number of command groups, or pass the queue to a library that submits its own.
It is the form a domain library records an operation with, and what `Recorder::task` records.

The body waits for nothing and returns the event that ends its work; the runtime releases the
node's successors when that event completes. The queue belongs to the device that owns the
regions the node names. The body orders its own submissions: on the scheduler the queue is out of
order, so a second command group that reads what the first wrote calls `depends_on()` with the
first one's event. The events the body is handed are those its node must run after, and a body
passes them to the first command group it submits. A node the Recorder records has none, because
its order comes from its regions.

The node is ordered by Reads and Writes like any other, runs once per step, and is taken by the
chain layer as a kernel is.

### add_host(Reads, Writes, body)

add_host(Reads, Writes, body) is for host C++ that submits no device work: gameplay logic, AI,
navigation, the CPU half of a broadphase. The caller names the data it touches so the tracker
orders it against the rest of the graph; the runtime then runs `kernel` on a worker thread, with no
SYCL queue submission, and releases its successors through the same path as any kernel. The
shared USM it reads is visible because the task runs only after its predecessors complete.

### add_host(body)

add_host(body) is the dependency-free form: with no Reads or Writes the task is both a root and a
leaf, so it is for self-contained host work (telemetry, a per-step callback) that needs no
ordering against other tasks.

### extend

extend(extension) calls `extension(Recorder&)` once, when it is called, and the nodes the
extension records form one rule. It is how a library that records through the public Recorder
joins a chain: `graph.add(...).extend([&](Recorder& r) { record_my_nodes(r); }).name("mine")`.
The extension runs once, inside the extend() call, and never when a plan is built: what it
records is replayed like any other rule. A body that needs the queue is recorded with
`Recorder::task`. An extension that records nothing adds no rule, so a name() chained onto it
names the rule before it.

### name

Without a name a node reports as "unnamed_task" in RunReport::NodeTiming, which makes a profile of
a real graph a list of identical labels. Chaining name() onto any add() names the rule that call
added:

```cpp
sim.add(n, Reads(a), Writes(b), integrate).name("integrate");
```

name() names a rule, and `NodeOptions::name` names one node. A rule recorded by one add() call
has one node, so either spelling names it. An extension records several nodes into one rule, and
there the two differ: name() gives all of them one label and `NodeOptions::name` tells them apart.

name() copies the label, so a temporary string is a valid argument. Every node of the rule that
stated no name of its own reports the rule's name. An empty label returns the rule to the default
name, and so does a null `const char*`.

## plan_cache.hpp

### PlanCache

PlanCache holds the plan compiled from one recorder. It is stale until the first build, after a
change of the profiling request, and whenever the recorder's generation() differs from the one the
plan was built at. `plan_` is protected because the test seams of the owning Graph read it. It
keeps one registration index per rule. compile_count() is its one public member; the profiling
pair is protected, so the pair a Graph shows is StepDriver's. The source of a move has no plan, is stale, and counts zero
compiles.

### build

build() builds a fresh backing graph by replaying the recorder, compiles it, and caches the plan.
The compiled plan snapshots its own replay baseline (in-degrees) so the fluent layer never touches
Node's scheduler-internal state. compile() drains its builder, so a fresh TaskGraph is built
whenever the topology changes.

### rearm

rearm() restores the cached plan's per-run state so it can run again. The scheduler
re-arms itself; rearm() resets the DAG-side state a previous run consumed, which
CompiledGraph owns next to the Node definition.

### set_profiling

set_profiling() stores the request. A value that differs from the stored one marks the plan dirty,
and the next run recompiles with the new `CompiledGraph::profiling` flag stamped in. The value
already stored changes nothing, so a frame loop that calls `set_profiling(true)` before every run
compiles once. The setting is independent of every other graph on the same runtime.

### compiles_

compiles_ counts the times the backing graph has been compiled. It stays at 1 across a multi-step
run of a late-bound graph whose sizes and enable flags change every step.

## step_driver.hpp

### StepDriver

StepDriver is the run surface and the step loop, written once for Graph and DynamicGraph. The
owner derives from StepDriver\<Owner> and befriends it. The driver owns three things: the record of
a pending asynchronous step, the number of the last step it started, and the count of its runs in
flight that cancel() reads.

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
| `node_count()` | The count a report multiplies by the steps run |
| `request_profiling()`, `profiling_request()` | Stores and returns the profiling request behind set_profiling() |

### Run entries

Graph and DynamicGraph take every run entry from the driver, so the two cannot differ:

| Entry | Returns | Runs |
| --- | --- | --- |
| `run()`, `run(steps)` | `Result<RunReport>` | One step, or a fixed number of steps |
| `run(reuse)`, `run(steps, reuse)` | `Result<RunReport*>` | The same, into a report the caller owns |
| `run_for(budget)`, `run_for(budget, reuse)` | As `run` | Steps until the wall-clock budget elapses; at least one |
| `run_until(done)`, `run_until(done, reuse)` | As `run` | Steps until `done()` is true |
| `run_async()` | `Result<RunHandle<Owner>>` | One step, without waiting |

Every entry is `noexcept` and returns a `Result`: the report, a pointer to the caller's report, or
the RunHandle. There is one form of each. A caller that cannot go on after a failed run writes
`graph.run().value()`, which stops the process with the error's message.
`modules/surface/api/tests/test_run_driver.cpp` holds the list as `static_assert`s.

No entry throws, also when memory runs out while it builds an Error: every Error of the driver
and of RunHandle is built by Detail::error_without_throwing() of `core/error.hpp`, which returns the
code with an empty message when the text cannot be allocated. run_until() moves its stop test,
so the stop test must be nothrow move constructible, and a `static_assert` says so.

run_for(budget) stops at `now + budget`. A budget the steady clock cannot add to now, such as
`std::chrono::hours::max()`, saturates at the clock's last time point, and a budget of zero or less
ends at now, so the run is one step.

### guarded

guarded() is where an exception of the plan build or of a step becomes an Error. It calls
Detail::translate_exceptions() of `api/detail/error_translation.hpp` with `KernelFailed` for a
device failure; `api/detail/README.md` describes the ladder.

| Thrown | While the plan is built | Once a step may have started |
| --- | --- | --- |
| `sycl::exception` | `KernelFailed` | `KernelFailed` |
| Any other exception, such as one a host task throws | `CompileFailed` | `KernelFailed` |

The engine returns its own failures as `Result<void>` from execute(), submit() and complete(), and
`ChainRun` from submit_step(), wait() and finish(); the driver forwards those with their codes.
What the ladder still catches is thrown by a hook, by the plan build or by SYCL on this thread.

The build phase is plan_is_stale(), rebuild_plan() and prepare_run(). Everything after it is the
step phase, and RunHandle::wait() settles a step through the same ladder, so a throwing host
task is `KernelFailed` from run() and from wait() alike. The try blocks surround the whole
loop, not one step.

### entry_refusal

entry_refusal() answers before anything touches the plan. A graph whose recorder refused a
recording returns that refusal first, whatever else is true of it: the refused rule may be the
only one, and "nothing to run" would hide the reason. A DynamicGraph returns the refusal of the
region with the lowest key that has one. A graph with nothing recorded is `InvalidGraph`. A graph
with a pending asynchronous step is `InvalidState`: a synchronous run re-arms every node's
in-degree, and doing that while the workers walk the DAG would rewrite the scheduling state under
them. A graph whose recorded handle was freed is `DanglingResource`.

A runtime has one engine, so at most one run is in flight on it. While the engine reports a run in
progress, every run entry of every graph on that runtime is `InvalidState`: another graph's
pending run_async() step, another graph's blocking run on another thread, or a run started from
inside a task of a run. The caller settles the run in flight first, by letting its blocking call
return or by waiting on its RunHandle. Without the refusal the second run would clear the first
one's cancellation and, on the chain, submit beside it. The test is
ExecutionEngine::run_in_progress(), one atomic load per run entry and none per step. Between two
steps of a multi-step scheduled run the engine reports no run, so a stop test may still run another
graph.

### compile

compile() does what a run does before its first step and stops there: it asks entry_refusal(),
then builds the plan if it is stale. It returns the recorder's refusal, `InvalidGraph` for a
graph with nothing recorded, `InvalidState` while a run is in flight, `DanglingResource`, or the
failure of the build. A plan that is current is left alone, so compile() followed by run()
builds once, and compile_count() or compose_count() counts that one build. Graph and DynamicGraph
both take it from the driver.

### run(steps)

The graph is compiled on the first run (and after any mutation), then the compiled plan is replayed
for each step: node dependency state is reset, the plan re-executed, and every State advanced to its
next field. Compilation happens at most once per call.

### A failed run and its States

A step advances the States only after it has been checked, so a failed run of one step leaves every
State untouched on both layers. A failed run of several steps has advanced the steps before the
failing one. On the chain, run(steps) submits its steps back to back and waits once, so step k+1
reads what step k wrote only if the States advance between the two submissions. A device fault
recorded by any step of such a run is therefore seen at the end, and by then every step but the
last has advanced. run_for() and run_until() wait and check after every step on both layers.

### run(Internal::Observability::RunReport&)

run(Internal::Observability::RunReport&) is the allocation-free form of run(). A fixed-step loop keeps one report alive
and passes it every tick; the report is reset and refilled in place, so after the first run the
per-run vectors are reused and not reallocated:

```cpp
Internal::Observability::RunReport report;
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
Internal::Observability::RunReport report = step.wait();
```

It runs one step only. The multi-step drivers (run(n), run_for, run_until) stay synchronous, because
their per-step host work has to happen between steps and there is nothing for the caller to overlap
with in the middle of a batch. A step is one tick, which is the granularity a frame actually wants.

The handle's destructor waits for the step, and so does the graph's: whichever dies first ends the
step, so the plan is never destroyed underneath the workers. A handle that outlives its graph owns
no step, as the "RunHandle: the owner's address" section describes.

On a DynamicGraph a pending structural change (a region added or dropped) is committed before the
step is submitted, exactly as a synchronous run does.

run_async() returns the handle or the Error that kept the step from starting: the refusals of
entry_refusal(), `Unsupported` from an engine that cannot start a run without blocking, and a build
or submission failure mapped by guarded(). run_async() throws that Error.

### run_async: pre-step order

run_async() follows the same pre-step order as the synchronous driver: point the State views at
their buffers, clear a previous run's cancellation, zero the profiling accumulators, then refresh
the late-bound controls. A different order would make an asynchronous step mean something subtly
different from a synchronous one.

### before_each_step

before_each_step(hook) adds a hook the graph calls before each step it replays, on the thread
that called the run entry: `hook(std::size_t finished_steps)`. It is how a caller feeds a
compiled plan between two replays, by writing a host word a kernel reads or a value a `sized()`
provider returns, and it replaces a list of callbacks a library would keep beside the graph.
Hooks run in the order they were added, before the runtime refreshes the late-bound controls of
the step, so a provider reads what a hook set.

`finished_steps` counts the steps the run entry in progress replayed before this one. It is 0 for
the first step of every run(), run_for() and run_until() call and for a run_async() step, and 7
for the last step of run(8). A caller that counts across runs keeps the count in its hook.

A hook records nothing and changes no plan: the graph compiles once however many hooks it has and
however often they run. A hook must not record on the graph or start a run. An exception a hook
throws ends the run as `KernelFailed` before that step is submitted, and the States have
advanced once per step before it.

A hook may write memory the kernels of the next step read, so a graph that has a hook ends each
step before it calls the hooks of the next. On the chain layer this turns the single wait of
run(steps) into one wait per step, the pace run_for() and run_until() already have; a failed step
then shows at once and not at the end of the run. A graph with no hook pays one emptiness test
per run.

Hooks belong to the graph and stay for its life. Recorder::clear() leaves them, and a move takes
them to the destination and leaves the source with none. Detail::BeforeStepHooks stores them; its
section of `api/detail/README.md` says how.

### cancel

cancel() is safe to call concurrently with a blocking run(), run_for() or run_until() executing on
another thread: the active step drains its remaining tasks without launching new kernels and
returns, the driver stops before the next step, and the resulting RunReport has cancelled set. It
also stops this graph's pending run_async() step, whose report then has cancelled set. Cancellation
is a clean stop, not an error, and cancel() is `noexcept`: the engine sets one atomic flag.

cancel() acts only on the graph it is called on. The engine's flag belongs to the whole runtime, so
the driver counts its own runs in flight, a blocking run from before its cancellation reset until
it returns and an asynchronous step from submission until it is settled, and cancel() does nothing
while the count is zero. cancel() on an idle graph therefore leaves another graph's run alone, and
a cancel() sent before a run starts is not remembered. The count is one atomic that cancel() loads
from any thread; a run entry adds to it once and takes it back once. A cancel() that races the end
of its own run can still reach the engine after the next run on the runtime has started.

### set_profiling

set_profiling() asks for per-node device timing on this graph and returns the graph, so it chains
with the calls around it. It is `noexcept` on both graph types and allocates nothing: the owner
stores the request, and a request that changed makes the next run rebuild the plan with it; the
value already stored rebuilds nothing. is_profiling_requested() reads the
request back.

### StepDriver: moves

The marker of a pending step never moves, and the driver's move operations leave no step pending
on either side. Moving a graph whose step is pending breaks a precondition, so a debug build
asserts. A release build waits for the step in the move, advances the States as wait() would, and
drops the outcome, which is what destroying the handle does. Both graphs are then runnable. The
handle still names the source, and its wait() returns `InvalidState`, because the step it was
given is over.

Copying the marker to the destination, which the defaulted move did, left a destination that
refused every run for good. Leaving the marker behind without waiting would let the destination
re-arm a plan the workers still walk. The driver's move runs before the plan and the recorder
move, so the step ends on an intact graph.

### end_pending_step

end_pending_step() waits for a pending step, checks its faults, advances the States and drops the
outcome. The move operations call it, and so do ~Graph() and ~DynamicGraph() as their first
statement. Destroying a graph whose step is pending is allowed and asserts nothing: the destructor
returns once the step has run, so the kernels' writes are in the buffers. The call sits in the
derived destructors because ~StepDriver() runs after the plan, the recorder and the regions are
gone.

### settle_async_step

settle_async_step() is what a handle calls to wait, after the handle has checked that its step is
still the one pending. It clears the record and the count of runs in flight before the blocking
call, because the step is over however it ends and the graph must be runnable again; one throwing
kernel must not make the graph refuse every later run. The wait and finish_async_step() run under
guarded().

### finish_async_step

finish_async_step() is the tail of the synchronous loop body plus its report building, split out
because those happen in a different call from the submission. Advancing every State in it, and not
at submit, is what makes an asynchronous step observationally identical to a synchronous one: the
just-written field becomes current only after the step has genuinely finished. `state` is what
run_async() recorded when it submitted the step.

On a DynamicGraph every live region's States advance here, because the driver runs the hooks
aggregated from all of them.

### Offloaded count

`tasks_offloaded_to_network` in a report is the number of dispatches a worker accepted during that
run. The coordinator's counter is cumulative, so the driver reads it before the first step and the
report builder subtracts. The reading happens once per run, and it is one null check when no
coordinator is installed.

### drive: report reset

drive() resets the report before any early return. A caller reusing one report across ticks must
never read the previous run's numbers back out of a run that failed; fill_run_report sets the flag
back on success.

### run_chained

The per-step host work runs between submissions exactly as on the scheduler; it only moves host
pointers and host-set controls, so it never needs the device to have finished (spec §C, layer 1).
`keep_going` is called with the count of submitted steps before each step, and `engine` is consulted
only for cancellation.

Under `AtEnd` a step's States advance at the start of the next iteration, and the last step's after
finish() and take_fault() have returned. Under `EveryStep` each step waits, is checked and then
advances, so the stop test reads the fields the step just wrote. "A failed run and its States"
gives the rule a caller sees.

### async_record_

async_record_ is the driver's record of its pending asynchronous step: a shared
Detail::AsyncStepRecord that holds the number of the pending step, or `K_NO_STEP`. run_async()
writes the number, and settling the handle, moving the graph or destroying it writes `K_NO_STEP`.
async_step_pending() reads it, and it guards the plan against a second run re-arming nodes the
workers are still walking. Every handle of the graph holds the same record, so a handle learns that
its step is over without reading the graph. The record is allocated once per graph, by its first
run_async(); a graph that only runs synchronously allocates none, and a frame loop that calls
run_async() every tick allocates one. async_step_serial_ numbers the steps and skips `K_NO_STEP`
when it wraps.

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
it, when it was cleared or when one of its rules was renamed.

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

## region_composer.hpp

### RegionComposer

RegionComposer owns the compiled sub-graph of every region, the cross-region edges per shared
allocation and the stitched plan. commit() receives the changed and the dropped keys and a callable
that returns a region's Recorder, so the composer depends on Recorder alone. `plan_` is protected
because the test seam of the owning DynamicGraph reads it.

The composer owns the profiling request, as PlanCache does for a static graph: a constructor
argument seeds it, the protected set_profiling() and is_profiling_requested() write and read it,
and commit() reads it when it publishes the plan. A move copies it to the destination and leaves it
on the source.

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
once nothing of it is left live. A partially overwritten pin stays live, which can add a cross edge
and cannot drop one. The whole boundary layer errs that way, because an extra edge costs
parallelism and a missing one is a data race.

### rewire_base

rewire_base() derives, or derives again, the cross-region edges for one shared allocation. It is
self contained: it first un-wires the base's current edges, then walks the regions touching it in
ascending key order, threading the live writers and the pending readers through so RAW, WAR and WAW
are honoured and read/read stays parallel. Every hazard test compares byte intervals, so two regions
touching disjoint slices of one allocation gain no edge at all. A base whose endpoints include a
torn-down node is un-wired earlier by retire_regions(), while those nodes are still alive; the undo
inside rewire_base() then runs on an empty list and does nothing.

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

### commit: build first, change second

commit() works in two phases. It first builds every dirty live region into local storage with
build_region(), which replays the region's recorder and can throw. Up to that point the only member
it writes is the plan index counter, so a throw from a build leaves the regions, the cross edges,
the roots and the plan as the last commit left them, and the caller repeats the commit once the
recording is repaired. A region built before the one that threw is built again by that commit.

The second phase un-wires the allocations of the retired regions (retire_regions()), installs the
built ones (install_region()), wires every dirty allocation again (rewire_base()) and publishes the
plan (publish_plan()). These steps insert into sets and vectors and can throw `std::bad_alloc`
halfway. A RecomposeGuard covers them: when one throws, invalidate_plan() empties the plan's root
list, makes composed() return false and sets `recompose_everything_`. The next commit then builds
every region that is still live, frees all region state, edges and roots together with the old
sentinel (discard_every_region()), and installs and wires the whole graph from nothing.

The composer finds the live regions of that rebuild among the keys it still holds and the dirty
keys it is given, so the caller must pass the dirty and dropped keys of the commit that threw.
DynamicGraph::rebuild_plan() clears its two sets and frees the retired regions only after commit()
returns.

Both the old and the new nodes of a dirty region are alive between the two phases.

### commit: cost

Building costs O(nodes of the dirty regions), and wiring costs O(boundary pins of the allocations
those regions and the dropped ones touch). Two steps cover the whole graph at every commit:
renumber_registration_indices() walks every node of every live region and allocates one map per
region, and publish_plan() copies the root list, O(roots). When profiling is requested
publish_plan() also lists every node again in `plan_.all_nodes`.

### commit: undo before free

retire_regions() undoes every cross-region edge on a dirty base while all endpoints are still
alive, so freeing torn-down nodes leaves no dangling pointer in any other node's boundary list.

### commit: registration indices

renumber_registration_indices() groups registration indices by value, not by consecutive run, so
one add() call's local index maps to exactly one global index even if its nodes are not contiguous
in st.nodes.

### RegionState: generation and rule_nodes

build_region() stores the recorder generation it built from, which is_current() compares, and the
last node of each rule, from which registration_of() reads the index that the latest commit wrote.
It also gives each node a plan index from a counter that only rises, so the indices of a composed
plan have gaps; nothing in the composed plan indexes a table by them.

### RegionComposer: moves

The source of a move has no region state, no plan and no sentinel; commit() creates a sentinel when
it finds none, so the source composes again.

### rearm

rearm() re-arms the live in-degree of every node, and of the sentinel, from the two-layer baseline:
the intra in-degree plus the boundary in-degree. It walks every node of every live region, O(total
nodes) per step. The per-step State and late-binding updaters are not its work; the step driver
runs them from the StepMachinery that DynamicGraph::refresh_updaters() gathers.

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
auto step = simulation.run_async();
extract_previous_frame();          // overlaps with the step
Internal::Observability::RunReport report = step.wait();
```

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

### wait(Internal::Observability::RunReport&)

wait(Internal::Observability::RunReport&) is the allocation-free counterpart of wait(), for the same reason
Graph::run(RunReport&) exists: an overlapped frame loop settles a step every tick, and a report it
already owns costs nothing to refill. The report is reset before it is written, so a stale field
from the previous step can never survive into this one.

### wait: consuming the handle

wait() consumes the handle before the blocking call: if a task threw, the step is still over,
and a destructor that tried to wait again would block forever on a latch nobody will signal.

### wait: clearing the report

wait() clears the report before anything else, so a step that ends in a task exception, or a
handle that owns no step, cannot leave the previous step's numbers readable in a report the caller
reuses. finish_async_step sets the flag back on success.

### settle

settle() finishes a step nobody waited for by calling wait() into a report it discards. A
destructor may not throw, so the outcome, including a task exception, is dropped; that is the
documented cost of discarding a handle, and it is still far better than returning while workers
hold pointers into the plan.
