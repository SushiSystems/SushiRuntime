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
| `node_options.hpp` | NodeOptions |
| `backing_graph.hpp` | BackingGraph and is_graph_emitter_v |
| `builder_surface.hpp` | BuilderSurface: add(), add_untracked(), add_host(), add_emitter(), extend(), name(), size(), node_count() and last_rule() |
| `plan_cache.hpp` | PlanCache: the compiled plan of one recorder |
| `step_driver.hpp` | StepDriver: run(), run_for(), run_until(), run_async(), their try_ forms, cancel(), set_profiling() and the step loop |
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
size(), node_count() and compile_count() are zero, a run is refused with `invalid_graph`, and add()
records onto it again. The plan moves with the rules, so the destination does not recompile.

A graph must not be moved while a step started with run_async() is pending: the RunHandle holds the
address of the graph that started the step. A debug build asserts on such a move. A release build
gives it the result the "StepDriver: moves" section describes: the move waits for the step, and the
handle then owns nothing.

### rule_registration

A rule is what one add(), add_untracked(), add_host() or add_emitter() call records, or what one
Recorder::rule() scope records. size() counts rules, node_count() counts the nodes they lowered to,
and rule_registration(rule) takes a position below size(); last_rule() is the position of the rule
just added. RunReport numbers a node event by registration index, and every node of a rule carries
the rule's index, so rule_registration() maps a record back to the call that recorded it. The
indices are drawn when the graph compiles, so every rule reports `NO_REGISTRATION` until the first
run, and a recompile may renumber them.

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

### Recorder: moves

The source of a move is an empty recorder on the same context: no node, no rule, no scratch, no
observer, and generation() back at zero. It keeps the context so that it can record again. No rule
scope may be open on either side, because a RuleScope holds the recorder's address.

### RuleScope

Every node belongs to one rule. rule() opens a scope, and the nodes recorded until it ends form one
rule; a node recorded outside a scope is a rule of its own. At replay the recorder reserves one
registration index on the backing graph when the first node of a rule is added and stamps it on all
of them. The nodes of a rule are contiguous, so replay() needs two locals and no allocation. An
emitter stamps its own nodes and takes none. A scope that records nothing adds no rule.

### name_last_rule

name_last_rule() names the last rule and copies the text, so the caller's string may be a temporary.
The copies live in a node-based set, one per distinct label: a compiled node keeps a `const char*`
to its name, and an entry of that set never moves when another label is added or the recorder is
moved. The rule keeps the pointer. replay() stamps it on every node the rule lowers to, which is
why a name given after the last node of a scope still reaches the first. A rename advances
generation(), so the next run rebuilds the plan with the new name.

### clear

clear() forgets the nodes, the rules, the labels, the observers, the hooks and the scratch, and
advances generation() instead of resetting it, so a plan built before the call is seen as stale.
The scratch is freed at once: a plan built from the cleared nodes must not run again, which the
owner guarantees by rebuilding whenever generation() changed.

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

replay() adds every recorded node to a backing graph in recording order and stamps each with its
rule's name. PlanCache::build() and RegionComposer::build_region() both call it, so a static graph
and a region are replayed by the same code. The visitor overload reports each node, with the
position of its rule, once it is added; PlanCache uses it to read back the registration index of
each rule, and the composer to remember a node of each rule.

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
Region. Each overload checks its callable, calls one recording member of the builder's Recorder
and returns the builder, so calls chain on the type the caller holds. No overload holds logic of
its own: what a launch does is in `api/detail/emit_launch.hpp`, and what a rule records is in the
Recorder.

### Argument order

Every call reads in one order: modifier, launch, access, callable, options.

| Part | What it is | Spellings |
| --- | --- | --- |
| modifier | changes how the rule is run | `Dynamic`, an `OpID` |
| launch | how many work-items, and their index | a count, `Extent<N>`, `NdRange<N>`; the whole field when a State rule gives none |
| access | the data, and how the kernel gets it | `Reads` and `Writes`, `In`/`Out`/`InOut`, a `State`, `Coupled`, `On` |
| callable | the kernel | a lambda |
| options | scalars sent with an offloaded kernel | `{params...}` |

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

### Which add do I call

| The kernel | Call |
| --- | --- |
| steps one field from its previous value | `add(state, fn)` |
| steps one field and reads others | `add(Coupled(out, in0, ...), fn)` |
| does either over part of the field | the same with a launch first: `add(Extent{...}.at(origin), state, fn)` |
| reads and writes buffers, and should receive their pointers | `add(launch, In(a), Out(b), fn)` |
| captures its own pointers | `add(launch, Reads(a), Writes(b), fn)` |
| has a live size or an on/off switch that changes per step | `add(sized(...), capacity, Reads(a), Writes(b), fn)` or `add(sized(...), state, fn)` |
| may run on another machine | `add("name"_op, n, Reads(a), Writes(b), local)` or `add("name"_op, state, local)` |
| drives the sycl::handler itself | `add(Reads(a), Writes(b), fn)` |
| is host C++ and submits no device work | `add_host(Reads(a), Writes(b), fn)` |
| shares no data with any other rule | `add_untracked(launch, fn)`, with `On(buffer)` when the buffer is not on device 0 |
| is recorded by a library through the engine's graph | `add_emitter(fn)` |
| is recorded by a library through the Recorder | `extend(fn)` |

In/Out/InOut is the form to prefer for buffers: the data is named once, and the pointers the
kernel receives are the ones the runtime orders by. With Reads and Writes the kernel captures its
pointers by hand, and the two lists can get out of step.

### A wrong callable

A callable whose signature does not fit fails one `static_assert` in
`api/detail/callable_checks.hpp`. Its text states the signature the call expects, and it is the
only error: the overload does not instantiate the recording call after a failed check. The
overloads are selected by the launch and the access alone, so a mistyped callable can no longer
empty the overload set.

### add(launch, Reads, Writes, fn)

The kernel captures raw USM pointers, and Reads(...) and Writes(...) tell the dependency tracker
which Buffers, States or regions those pointers belong to. A count calls `fn(i)`, an Extent calls
`fn(sycl::id<N>)` with the origin added, and an NdRange calls `fn(sycl::nd_item<N>)` or
`fn(sycl::nd_item<N>, sycl::id<N>)`.

A count also takes a range kernel, `fn(begin, end, sycl::handler&)`, which drives the handler over
[0, n) itself. That is the form for a kernel the per-element shape cannot express while its
regions still order it.

### add(Reads, Writes, raw_kernel)

`fn(sycl::handler&)` owns the whole command group (its own parallel_for, local accessors, a
memcpy) while the declared regions still order it. It has no launch because the runtime submits
none. The shaped overloads are the better choice wherever one fits, since they cannot get the
launch geometry and the declared data out of step.

### add(launch, In/Out/InOut..., fn)

The caller passes the buffers a kernel touches as In(buf), Out(buf) or InOut(buf). The runtime
registers the reads and writes and hands `fn` the raw element pointers, in argument order, after
the index. Up to v0.3 this form existed for an Extent only, so 1-D code wrote `Extent{n}` to get
it; a count and an NdRange now take it too.

The first capture is a parameter of its own, which guarantees at least one. The rest is one
trailing pack so that the compiler can deduce it: its last element is the callable and the others
are further captures. add() splits the pack, checks that everything before the callable is a
capture, and the call site reads `In(a), Out(c), fn` with `fn` last.

### add(state, fn)

This is the canonical stepped rule: it reads the State's current field and writes its next field,
and the dependency tracker keys on those two buffers. The runtime supplies the current and next
pointers to `fn` at execution time, so under run(n) the same compiled node follows the
double-buffer swap with no recompilation. A State takes exactly one update rule per step.

### add(launch, state, fn)

The launch form runs the same rule over a count, an Extent or an NdRange: a prefix of the field,
a sub-box for boundary or halo handling, or a tiled launch. `fn` receives the launch's index
before the two pointers.

The launch is compared with the State when the rule is added. A launch that reaches past the
State throws `invalid_graph` at the add() call, naming the rule, the launch with its origin and
the State's size; `api/detail/README.md` says what "reaches" means for each launch.

### add(Coupled, fn) and add(launch, Coupled, fn)

A coupled rule writes the next field of one State from its own current field and the current
field of one or more others. It is the building block for coupled systems such as
reaction-diffusion with several species. All reads see the previous step's data (a Jacobi
update): every State advances only after all of a step's kernels have run. Each field takes
exactly one update rule per step.

The whole-field form iterates the size of the State written, and add() throws `invalid_graph`
when a State read is smaller than that. The launch form compares the launch with every State
named.

### add(Dynamic, capacity, Reads, Writes, fn)

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

### add(Dynamic, state, fn)

This is the late-bound form of add(state, fn). The State's full allocation is the capacity;
sized() iterates a prefix of it (a changing number of active cells, for example), and when() can
skip the step's kernel. The double-buffer swap is followed as in the static form.

The State still advances (swaps current and next) on a step its when() disabled, so a skipped
step leaves the un-written next field current. This overload suits size variation; sleeping a
field across steps belongs to the streaming layer, not to when().

### add(OpID, n, reads, writes, local, params)

This overload is the fluent counterpart to a distributed dispatch. `op` names a kernel registered
(by the same OpID) in this binary's KernelRegistry on every node; when the runtime is a master
with a live worker that supports `op`, the scheduler may ship this task over the wire. Otherwise
(a standalone build, no worker, or the policy declining) `local` runs it on this machine, so the
same graph is correct with or without a cluster.

`reads` and `writes` are Reads(...) and Writes(...), or two lists of base pointers. The dependency
tracker orders the task by them, and the distributed layer resolves them to network buffer
handles. `params` are scalar arguments carried in TaskMetadata, element counts for example.

### add(OpID, state, local, params)

This overload is the distributed counterpart of add(state, fn): the State's step may ship to a
worker or run its local fallback, with the same graph correct either way. Because a State rotates
its allocations each step, the node tracks the step's read and write buffers through live pointer
lists the runtime re-points before every step, so the compiled plan stays replayable while the
offload follows the double-buffer swap. The State must be registered with State::distributed()
first so both allocations can cross to a worker.

### add_untracked(launch, fn)

add_untracked() records no read or write region: the rule is both a root and a leaf, ordered
against nothing. That is correct for self-contained work (seeding a buffer no other rule of this
graph reads, a benchmark kernel) and wrong the moment another rule shares data with it. Whenever
ordering matters, name the data and use add().

The launch is a count, an Extent or an NdRange, and the kernel receives the same index as in
add(). A count also takes a range kernel, and add_untracked(fn) takes a raw kernel.

### add_untracked(launch, On, fn)

The runtime places a rule on the device that owns the first region it writes. An untracked rule
names no region, so it states its device: On(buffer) and On(state) name the device of a handle
and watch the handle, On(DeviceIndex) names a device. Without an On the rule runs on device 0,
where Runtime::buffer() and Runtime::state() allocate by default. An On that names a device the
runtime does not have throws `invalid_access` at the call.

### add_host(Reads, Writes, fn)

add_host(Reads, Writes, fn) is for host C++ that submits no device work: gameplay logic, AI,
navigation, the CPU half of a broadphase. The caller names the data it touches so the tracker
orders it against the rest of the graph; the runtime then runs `fn` on a worker thread, with no
SYCL queue submission, and releases its successors through the same path as any kernel. The
shared USM it reads is visible because the task runs only after its predecessors complete.

### add_host(fn)

add_host(fn) is the dependency-free form: with no Reads or Writes the task is both a root and a
leaf, so it is for self-contained host work (telemetry, a per-step callback) that needs no
ordering against other tasks.

### add_emitter

add_emitter(emitter) records work the fluent verbs do not shape: `emitter` receives the engine's
SushiRuntime::Graph::TaskGraph and adds its own nodes with add_node(), add_task() or add_native().
It is for a library above the runtime that already records through the lower-level graph and wants
that work inside a Graph or a Region without restating each task as an add() overload. The runtime
learns nothing about that library; it calls `emitter` and takes the nodes.

`emitter` is called at every build of the plan, so in a DynamicGraph it runs again each time its
region is rebuilt and never while the region is left alone. It must therefore be repeatable, and it
only adds nodes: compiling the graph, taking its nodes or keeping a reference to it breaks the
build that called it.

An emitter is one rule, so size() counts it once however many nodes it adds. Ordering comes from
the regions each node declares, exactly as for nodes the fluent verbs emit; the call takes no
Reads or Writes of its own. The nodes keep the names and registration indices they were recorded
with, one index per node, so a profile shows each kernel. name() and rule_registration() address
the last node only.

Device work an emitter records must wait on the dependency events it is handed
(`h.depends_on(deps)`); a chained plan relies on that for its order.

The parameter type was spelled `BackingGraph` up to v0.3. The alias named nothing a caller could
look up, so the contract now names the engine type.

### extend

extend(extension) calls `extension(Recorder&)` once, when it is called, and the nodes the
extension records form one rule. It is how a library that records through the public Recorder
joins a chain: `graph.add(...).extend([&](Recorder& r) { record_my_nodes(r); }).name("mine")`.
An emitter is replayed at every build and sees the engine's graph; an extension runs once and
sees the recorder, so what it records is replayed like any other rule.

### name

Without a name a node reports as "unnamed_task" in RunReport::NodeTiming, which makes a profile of
a real graph a list of identical labels. Chaining name() onto any add() names the rule that call
added:

```cpp
sim.add(n, API::Reads(a), API::Writes(b), integrate).name("integrate");
```

A trailing name argument would have been shorter, but the capture form ends in a parameter pack
whose last element is the callable, and a defaulted trailing name cannot be expressed there. One
spelling that works for every shape, including any added later, beats a trailing argument on most
of them and an exception for the rest.

name() copies the label, so a temporary string is a valid argument. Every node of the rule
reports the name; an emitter is the exception, because the backing graph can name only the last
node an emitter added. An empty label returns the rule to the default name, and so does a null
`const char*`.

### Deprecated spellings

v0.4.0 keeps each spelling it replaces as a `[[deprecated]]` member that forwards to the new one
and holds no code of its own.

| Deprecated | Replacement |
| --- | --- |
| `add(Reads, Writes, launch, fn)` | `add(launch, Reads, Writes, fn)` |
| `add(state, Extent, fn)` | `add(Extent, state, fn)` |
| `add(out, in0, ..., fn)` | `add(Coupled(out, in0, ...), fn)` |
| `add(Dynamic, Reads, Writes, capacity, fn)` | `add(Dynamic, capacity, Reads, Writes, fn)` |
| `add(vector<void*>, vector<void*>, n, fn)`, and the same after a `Dynamic` | `Reads(pointer...)` and `Writes(pointer...)`: a base pointer converts to the region of its whole buffer |
| `name_last(label)` | `name(label)` |

## plan_cache.hpp

### PlanCache

PlanCache holds the plan compiled from one recorder. It is stale until the first build, after a
change of the profiling request, and whenever the recorder's generation() differs from the one the
plan was built at. `plan_` is protected because the test seams of the owning Graph read it. It
keeps one registration index per rule. The source of a move has no plan, is stale, and counts zero
compiles.

### build

build() builds a fresh backing graph by replaying the recorder, compiles it, and caches the plan.
The compiled plan snapshots its own replay baseline (in-degrees) so the fluent layer never touches
Node's scheduler-internal state. compile() drains its builder, so a fresh BackingGraph is built
whenever the topology changes.

### rearm

rearm() restores the cached plan's per-run state so it can run again. The scheduler
re-arms itself; rearm() resets the DAG-side state a previous run consumed, which
CompiledGraph owns next to the Node definition.

### set_profiling

set_profiling() marks the plan dirty so the next run recompiles with the new
`CompiledGraph::profiling` flag stamped in. The setting is independent of every other graph on the
same runtime.

### compiles_

compiles_ counts the times the backing graph has been compiled. A late-bound graph proves its value
by holding it at 1 across a multi-step run whose sizes and enable flags change every step.

## step_driver.hpp

### StepDriver

StepDriver is the run surface and the step loop, written once for Graph and DynamicGraph. The
owner derives from StepDriver\<Owner> and befriends it. The driver owns nothing but the marker of a
pending asynchronous step and the number of the last step it started.

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

| Throwing | Returning a value | Runs |
| --- | --- | --- |
| `run()`, `run(reuse)` | `try_run()`, `try_run(reuse)` | One step |
| `run(steps)`, `run(steps, reuse)` | `try_run(steps)`, `try_run(steps, reuse)` | A fixed number of steps |
| `run_for(budget)`, `run_for(budget, reuse)` | `try_run_for(budget)`, `try_run_for(budget, reuse)` | Steps until the wall-clock budget elapses; at least one |
| `run_until(done)`, `run_until(done, reuse)` | `try_run_until(done)`, `try_run_until(done, reuse)` | Steps until `done()` is true |
| `run_async()` | `try_run_async()` | One step, without waiting |

A try_ form is `noexcept` and returns a `Result`: the report, a pointer to the caller's report, or
the RunHandle. The throwing form calls its try_ form and throws the Error it got as a
`Core::SushiException`, so both report a failure with the same code and text.
`modules/surface/api/tests/test_run_driver.cpp` holds the list as `static_assert`s over both types.

### guarded

guarded() is the one place an exception becomes an Error. It runs a callable inside a catch ladder
and builds the Error in the handler that caught, with no rethrow: on the Windows intel-llvm
toolchain a rethrow from a helper called inside a catch block crashes the process.

| Thrown | While the plan is built | Once a step may have started |
| --- | --- | --- |
| `Core::SushiException` | Its own Error | Its own Error |
| `sycl::exception` | `kernel_failed` | `kernel_failed` |
| Any other exception, such as one a host task throws | `compile_failed` | `kernel_failed` |

The build phase is plan_is_stale(), rebuild_plan() and prepare_run(). Everything after it is the
step phase, and RunHandle::try_wait() settles a step through the same ladder, so a throwing host
task is `kernel_failed` from try_run() and from try_wait() alike. The try blocks surround the whole
loop, not one step.

### entry_refusal

entry_refusal() answers before anything touches the plan. A graph with nothing recorded is
`invalid_graph`. A graph with a pending asynchronous step is `invalid_state`: a synchronous run
re-arms every node's in-degree, and doing that while the workers walk the DAG would rewrite the
scheduling state under them. A graph whose recorded handle was freed is `dangling_resource`.

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

try_run_async() returns the handle or the Error that kept the step from starting: the refusals of
entry_refusal(), `unsupported` from an engine that cannot start a run without blocking, and a build
or submission failure mapped by guarded(). run_async() throws that Error.

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
clean stop, not an error, and cancel() is `noexcept`: the engine sets one atomic flag.

### set_profiling

set_profiling() asks for per-node device timing on this graph and returns the graph, so it chains
with the calls around it. It is `noexcept` on both graph types and allocates nothing: the owner
stores the request, and the next run rebuilds the plan with it. is_profiling_requested() reads the
request back. set_profiling_requested() and profiling_requested() are the deprecated spellings and
forward to these two.

### StepDriver: moves

The marker of a pending step never moves, and the driver's move operations leave no step pending
on either side. Moving a graph whose step is pending breaks a precondition, so a debug build
asserts. A release build waits for the step in the move, advances the States as wait() would, and
drops the outcome, which is what destroying the handle does. Both graphs are then runnable. The
handle still names the source, and its wait() returns `invalid_state`, because the step it was
given is over.

Copying the marker to the destination, which the defaulted move did, left a destination that
refused every run for good. Leaving the marker behind without waiting would let the destination
re-arm a plan the workers still walk. The driver's move runs before the plan and the recorder
move, so the step ends on an intact graph.

### settle_async_step

settle_async_step() is what a handle calls to wait. It first checks that the handle's step is the
one pending here: each run_async() numbers its step, the handle carries the number, and a handle
whose step a move already ended gets `invalid_state`. It then clears the marker before the blocking
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

### async_step_pending_

async_step_pending_ is true between run_async() and the settling of its handle. It guards the plan
against a second run re-arming nodes the workers are still walking, and entry_refusal() checks it.
async_step_serial_ counts the steps run_async() started; settle_async_step() compares it with the
number in the handle.

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

drop() takes effect on the structure immediately: region() and has_region no longer see the region.
The boundary it leaves is re-stitched at the next run, so a region is never removed from a DAG that
is mid-step. Dropping an absent region does nothing.

### request_profiling

request_profiling() stores the request behind set_profiling(). It stamps `plan_.profiling` at once,
so the flag reads correctly even with no region to compose, and sets a marker that makes the next
build mark every live region dirty, so that commit renumbers every node's public registration
index. The marking waits for the build because it allocates and the setter is `noexcept`. The
setting is independent of every other graph on the same runtime.

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

### changes_

changes_ is held by unique_ptr because every region keeps its address, and a DynamicGraph moves.
It is created with the first region.

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

### RegionState: generation and rule_nodes

build_region() stores the recorder generation it built from, which is_current() compares, and the
last node of each rule, from which registration_of() reads the index that the latest commit wrote.

### RegionComposer: moves

The source of a move has no region state, no plan and no sentinel; commit() creates a sentinel when
it finds none, so the source composes again.

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

A Region is neither copyable nor movable: its graph holds it by address, and so do the per-step
hooks the graph aggregates. clear() empties it so that its work can be recorded afresh without
drop() and region().

### RegionChangeLog

RegionChangeLog is the line from a region to its graph. Region::recorder(), the non-const one that
every add() and clear() goes through, lists the region's key in the log, once per interval between
two runs; each region keeps the interval it was last listed in, so a second recording costs one
compare. The const recorder() lists nothing, which is why size() and node_count() are free, and
DynamicGraph reads every region through its private recorded(), which takes the region as const. A
`Recorder&` taken from a region must not be kept across a run: a recording through it is not listed.
A Region built without a log belongs to no graph and lists nothing.

### Shared surface

Region and Graph take every add() overload (per-element, N-D, State, capture, tracked, dynamic,
offload), the untracked, emitter and host forms and name() from BuilderSurface, so a region
records work exactly as a static graph does. A call on a region returns the Region. Both expose
recorder(), so a function written over a Recorder records onto either.

## run_handle.hpp

### AsyncRunState

A synchronous run reads its "before" counters and its start time as locals, because the whole run
happens inside one function. An asynchronous run does not: the step is submitted in one call and
settled in another, so the same values have to travel in the handle. Every member of AsyncRunState
is a plain value, so the handle owns no resources and stays cheap to move. `serial` is the number
the driver gave the step, by which the driver recognises the handle when it waits.

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

### RunHandle: the owner's address

The handle keeps the address of the graph that started the step, so that graph must neither be
destroyed nor moved while the handle is pending. Graph and DynamicGraph state it as a precondition
of their move operations, and the "StepDriver: moves" section says what a move does when it is
broken.

### ready

ready() answers "has the graph finished", so a frame loop can keep overlapping work instead of
blocking early. It is not a substitute for wait(): the pool still has bookkeeping to drain and the
step's state advance has not happened yet.

### wait()

wait() completes the step's lifecycle, in which every State advances to the field it just wrote, and
consumes the handle, so a second call throws `invalid_state` instead of waiting on a step that is no
longer running. wait() calls try_wait() and throws the Error it got as a `Core::SushiException`. A
task exception therefore arrives as `kernel_failed` carrying the task's text, the same as from
run(), and the state advance is skipped in that case, exactly as a failed synchronous run leaves
its states untouched.

### try_wait

try_wait() is the implementation of every wait: it resets the report, consumes the handle and asks
the driver to settle the step. It is `noexcept` and returns the report or the Error that ended the
step.

### wait(Core::RunReport&)

wait(Core::RunReport&) is the allocation-free counterpart of wait(), for the same reason
Graph::run(RunReport&) exists: an overlapped frame loop settles a step every tick, and a report it
already owns costs nothing to refill. The report is reset before it is written, so a stale field
from the previous step can never survive into this one.

### wait: consuming the handle

try_wait() consumes the handle before the blocking call: if a task threw, the step is still over,
and a destructor that tried to wait again would block forever on a latch nobody will signal.

### wait: clearing the report

try_wait() clears the report before anything else, so a step that ends in a task exception, or a
handle that owns no step, cannot leave the previous step's numbers readable in a report the caller
reuses. finish_async_step sets the flag back on success.

### settle

settle() finishes a step nobody waited for by calling try_wait() into a report it discards. A
destructor may not throw, so the outcome, including a task exception, is dropped; that is the
documented cost of discarding a handle, and it is still far better than returning while workers
hold pointers into the plan.
