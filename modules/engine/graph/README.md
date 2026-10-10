# graph

The task graph below the fluent API: nodes, the dependency tracker and the per-step late bindings.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## dependency_tracker.hpp

A private header: it sits in `source/`, nothing outside the module includes it, and it is
not installed.

### get_shard_idx

get_shard_idx() shards on the owning allocation (its base), not the sub-region, so every access to
one allocation lands in the same chain and can be compared by interval overlap. It applies a fast
SplitMix64 bitwise hash to distribute allocations uniformly.

### ~DependencyTracker

compile() normally calls reset() explicitly on its success path. A graph destroyed without ever
compiling, or one whose compile() failed and was then abandoned, would otherwise leak its per-access
AccessNodes. reset() is idempotent, so running it in the destructor as well is safe.

## late_bindings.hpp

### LateBindings

The compiled DAG fixes a node's topology and which resources it touches, but a per-frame engine must
vary two things every step without paying a recompile: how many elements the kernel iterates
(entities spawned or destroyed, a changing count of active cells) and whether the node runs at all
(a sleeping island, an LOD-disabled system). Both live in LateBindings, behind a stable shared_ptr
the fluent layer re-points between steps and the scheduler reads when it dispatches the node. A node
with no bindings keeps the compile-time behaviour at the cost of a single null-pointer load.

The fields are atomic only to be free of torn reads. The writes happen on the driver thread at a
step boundary, after the previous step's completion barrier, and the reads happen on a worker after
the task is handed off through the scheduler's acquire/release submit path. That machinery supplies
the happens-before, so relaxed ordering suffices.

### device_count

The `count` field answers "how many elements does the host already know we want". device_count
answers the harder question a producer and consumer chain asks: how many did the device just
produce. A broadphase writes its pair count, and the narrowphase that consumes it must iterate
exactly that many. That number does not exist yet when the step boundary polls the host-side
providers.

The node's kernel reads it on the device, runs at its capacity and returns past the live count, so
no host read happens between nodes (docs/design/GRAPH_REPLAY.md §D). It is null on every ordinary
node, and the atomic `count` is used then.

device_count is written once when the task is recorded, before any run, and only read afterwards, so
unlike `enabled` and `count` it needs no atomicity. The allocation must be Residency::Host (pinned
host USM); the API that sets it rejects a device-resident buffer at the call site.

### base

`count` says how many and `base` says from where. Graph colouring is the case that needs it: each
colour is a contiguous slice of one constraint buffer, and the slices shift every time the
constraint set changes, so a node pinned to a baked offset would need an index-indirection buffer
and an extra load per element to find its own work. `base` is zero for an ordinary node, which then
iterates from the start.

### device_base

device_base is the device-sourced counterpart of base and the companion of device_count: a pass that
computes both a colour's offset and its length on the device should be able to hand over both the
same way. The kernel reads it as it reads device_count, under the same host-USM requirement. It is
null on every ordinary node, and the atomic `base` is used then.

## node.hpp

### RegionLink

The dynamic region graph splits a node's dependencies into a stable intra-region interior, baked
once when the region compiles, and a recomputable cross-region boundary layer the composer maintains
as neighbouring regions stream in and out. A static-graph node leaves every field at its default and
never reads this layer.

### NodeProfiling

The NodeProfiling fields are written only when the runtime was built with profiling on; otherwise
the dispatch and completion paths never touch them. They are relaxed atomics: each is a monotonic
counter accumulated as steps replay and read once at the run boundary, with no ordering dependency
on other state. A given step's completion is reaped by exactly one worker, but different steps may
be reaped by different workers, so the adds are atomic.

### live_reads and live_writes

A State node rotates which allocation it reads and writes every step, so the fluent layer re-points
these lists before each step and the coordinator resolves handles from them at dispatch. The
compiled plan stays replayable while it follows the swap. Both are null for every ordinary node,
whose fixed handles the coordinator keeps in the payload of the node's OffloadState.

### OffloadState

A node that a coordinator may run elsewhere carries a Graph::OffloadState behind one pointer;
`modules/engine/task/README.md` describes it. The type and sizeof(Node) do not depend on a build flag, and a node that never offloads never allocates one.

### Node

Node inherits from Task so the scheduler can dispatch it without knowing the DAG layer. Dependency
management is lock-free: in_degree is decremented with acq_rel by EventPolling as predecessors
complete; when it reaches zero the node is pushed to the ready queue. Successors are stored inline
(up to MAX_INLINE_SUCCESSORS) and overflow into a lock-free linked list.

### Node::region

The dynamic region graph splits a node's dependencies into a stable intra-region interior and a
recomputable cross-region boundary layer; `region` holds the boundary layer, as the RegionLink
section describes. Each step resets the live in_degree to region.intra_in_degree +
region.boundary_in_degree, so the boundary layer can be re-stitched without disturbing the frozen
interior. The static graph leaves it at its defaults and never reads it.

### is_sentinel

The sentinel delivers run completion (and, on a failed run, the error) through its host_work, so it
must run even after a task exception flips the scheduler into drain-without-executing mode. Skipping
it would leave the completion promise unset and hang execute(). ExecutionEngine sets the flag when
it wires the sentinel.

### io_reads_ and io_writes_

io_reads_ and io_writes_ are retained on every build, not just the distributed one, so the scheduler
can route a kernel submit to the queue whose context actually owns the data; it anchors on a
region's base. USM is pinned to the device and context it was allocated on, and running the kernel
on a sibling device's queue (CPU USM on a CUDA queue, for example) is an illegal cross-context
access. TaskScheduler::execute_task does the routing.

### locality_checked_

The scheduler verifies on a node's first dispatch that all its USM lives on one device context,
because a single kernel cannot touch two non-peer contexts at once. It caches the result in
locality_checked_ so later steps pay nothing. A node is dispatched serially across steps, so a plain
flag needs no synchronisation. Cross-device execution is a WP-5 concern; until then co-location is
the contract this guard enforces.

### placement_ and place_on

A node that names no region gives the scheduler nothing to route by, so place_on() lets its builder
state the device, and a node that states none runs on device 0. A node that names a region a device
owns runs on that owner whatever it states. `modules/engine/scheduler/README.md`, under "Queue
selection", holds the rule, and `Graph::placement_of()` applies it.

placement_ is a 16-bit field with NO_PLACEMENT for "none stated". It sits in the padding between
locality_checked_ and cached_queue_index_, so sizeof(Node) does not grow. place_on() throws
`Errc::invalid_graph` for an index the field cannot hold; whether the index names a device of the
runtime is checked at the node's first dispatch, where the device count is known.

### cached_queue_index_

cached_queue_index_ is the node's placement, resolved once on its first dispatch, alongside
locality_checked_, by `Graph::placement_of()`. Later steps route straight to
`DeviceQueueSet::queue(index)`, an indexed read. It is -1 only until that first dispatch. The index
is stable across replay: the device set never changes and the node's anchor buffer and stated
placement are fixed, so it cannot go stale. A node is dispatched serially, so a plain int needs no
synchronisation.

### bindings_

bindings_ is null for an ordinary node, so the scheduler's hot path pays one null-pointer load
before falling through to normal execution. When a dynamic add() overload sets it, the fluent layer
re-points its fields each step: the scheduler reads enabled to decide whether to run the node, and
the kernel reads count for its iteration range. Bindings may change size and enable freely but never
which resources the node touches. That would invalidate the compiled dependency edges and requires a
recompile.

### registration_index

TaskGraph stamps registration_index once at insertion time from a per-graph counter, in call order.
Unlike plan_index, which reflects the compiled plan's position, it reflects registration order.
Every internal node one public add() call creates (a reduction's tile levels, for example) shares
this value. It is zero until the node is registered.

### add_successor

add_successor() fills the inline buffer first. Once the buffer is full it pushes `target`, the node
to enqueue when this one completes, onto the lock-free successor_list through compare_exchange_weak
with release/relaxed ordering. The success ordering is release so the stored pointer is visible to
any thread that reads successor_list with acquire ordering.

### add_successor: slot before count

Each node has a single writer: successors are wired at graph build or step-boundary time, never
concurrently with another add_successor on the same node. That allows publishing the slot before the
count: write inline_successors[idx], then release-store the incremented count. A concurrent reader
in trigger_successors acquire-loads the count and, on seeing idx+1, is guaranteed to also see the
slot write. This closes the window where it read an incremented count but a still-null slot and
silently skipped the successor, a missed in_degree decrement that ends in deadlock. The previous
relaxed fetch_add published the count first, exposing that window.

### arm_in_degree

arm_in_degree() sets in_degree to region.intra_in_degree + region.boundary_in_degree, the count the
scheduler decrements as predecessors complete. The dynamic graph calls it for every node before each
replayed step. It is the equivalent of CompiledGraph::rearm for a plan whose boundary layer is
maintained incrementally and not snapshotted.

### offload

offload() is the mutable entry point for the paths that populate or update offload state. The first
call allocates this node's OffloadState; later calls reuse it.

### offload_state

offload_state() returns a pointer to the offload state, or nullptr if the node has none. A node that
never offloads never allocates one, so the scheduler's offloadable check reads a null return as "not
offloadable" without forcing an allocation.

### on_complete

on_complete() walks the three places a successor can live (the inline array, the lock-free spillover
list and the cross-region boundary vector) and decrements each target's in-degree. A target whose
count reaches zero has no dependencies left and goes to the sink. The decrement is acq_rel so the
freed successor sees every write the predecessors made.

## task_graph.hpp

### The node an add call returns

Every add_\* call returns the node it added, so a builder configures that node directly: its name,
its width, its placement. The reference stays valid while the graph holds the node and, after
compile() or take_nodes(), while the plan or the vector that took it over holds it. A parallel
builder needs the reference, because "the last node added" names one node only while a single thread
adds.

### add_node(work, dependencies, registration)

This overload is for a caller's own SYCL kernels or memory transfers. It hands `work` a
sycl::handler to define the work. `dependencies` are manual events to wait for before starting.

### add_node(work, read_access, write_access, dependencies, registration)

The runtime orders this task by the USM regions it touches: two tasks are serialized only when their
regions overlap, so disjoint sub-ranges of one allocation run in parallel. A bare base pointer
converts to the whole-buffer region, the conservative default.

### add_node(work, read_access, write_access, bindings, dependencies, registration)

This overload is the dependency-tracked one plus a per-step control block: the same region ordering
applies, but the node also holds `bindings` so the scheduler can skip it when disabled this step and
the kernel can read its live iteration size. The compiled topology stays fixed; only size and the
enable flag vary per step, so the graph is built once and replayed without recompiling.

### name_last_node

The name reaches the profiler as RunReport::NodeTiming::name. It is set after the fact and not
passed to each add_\* overload because two of the fluent launch shapes end in a parameter pack whose
last element is the callable, so a trailing name is not expressible there.

name_last_node() is called during graph description, on the thread that just added the node, so it
needs no synchronisation of its own.

### reserve_registration

reserve_registration() lets an add() shape that registers several internal nodes for one public call
(a reduction's tile levels) reserve one index up front and pass it to each node's add_\* call as
`registration`, so they report the same Node::registration_index. It is safe to call any number of
times; each call returns a distinct value.

### add_task(meta, read_access, write_access, device_work, fallback_work, ...)

This overload is for kernel-capable tasks that need a real device lambda. The host fallback is still
tracked for CPU-only or library execution paths: `fallback_work` is a lambda executed on the host
queue when host execution is selected. `write_access` lists the memory pointers the operation
overwrites or updates.

### add_task(meta, read_access, write_access, fallback_work, ...)

This is the compatibility overload for host-executed semantic tasks. Kernel-capable tasks should
prefer the overload that also receives device_work. `write_access` lists the memory pointers the
operation overwrites or updates.

### add_offload_dynamic

add_offload_dynamic() is the double-buffered counterpart to the offload add_task overloads. For a
task that reads one allocation and writes another and swaps the two each step (a distributed State),
the touched buffers cannot be frozen at compile time. The node carries the shared `live_reads` and
`live_writes` pointer lists, which the caller re-points before each step; compile() marks the node
offloadable without baking handles, and the coordinator resolves them from the live lists at
dispatch. Dependency ordering and queue routing use the lists' current contents at insertion.
`fallback_work` is the host fallback run when the task is not offloaded.

### add_native

add_native() is for host work that touches no device API: gameplay logic, AI, navigation, the CPU
half of a broadphase. The node is ordered by the regions it reads and writes exactly like a kernel
task, but the scheduler runs `work` directly on a worker thread and reports an empty event, so
completion uses the same successor-release path as any other node. By the time the node is
dispatched its predecessor events have completed, so the shared USM it reads is visible.

### compile

compile() links leaf nodes to a sentinel completion node, identifies root nodes (zero in-degree),
and transfers node ownership to the returned CompiledGraph. It does not submit any work to hardware.

compile() is not thread-safe with respect to add_node() and add_task(). All nodes must be inserted
before it is called.

A graph with no root, or with a cycle behind a root, throws `Core::SushiException` with
`Errc::invalid_graph`. The message gives the node counts and names one node that cannot run, by its
label and registration index.

### take_nodes

take_nodes() drains the insertion stacks and returns the node objects exactly as inserted, without
resolving roots and leaves or wiring a sentinel. The dynamic region graph builds a region's nodes
once with it to obtain a sub-graph whose intra-region edges are already baked (the dependency pass
ran during add_\*), then caches it and stitches only the cross-region boundary on top, so a region
that did not change is never rebuilt. The returned nodes keep their intra edges and carry their
in_degree as the region's own pass left it.

## Implementation

The task graph and the dependency tracker that orders its nodes. The headers are in
`include/SushiRuntime/graph/`. This page records why the sources behave as they do; the
source comments state the invariant and point here.

### dependency_tracker.cpp

#### resolve

`resolve()` pushes the access onto the shard's lock-free history and then walks backwards,
ordering the node after every earlier access whose region overlaps. That resolves
read-after-write, write-after-read and write-after-write hazards. A write is ordered after
every overlapping prior access, read or write. A read is ordered after every overlapping
prior write, and readers stay parallel to each other.

The walk stops early only at a prior writer that fully covers the accessed region. Every
earlier access to that range is then already ordered behind that writer transitively, so it
needs no direct edge. A writer that overlaps only part of the region does not end the walk.
An earlier access could touch the part it leaves uncovered, and stopping there would drop a
real edge and race silently on sub-regions.

#### reset

`reset()` retires the access history of every shard. It is safe because it is called only
when graph compilation is completely done.

#### add_dependency

`add_dependency()` checks the inline successor list and then the spillover list for the
edge before adding it, so an edge is recorded once. It registers the successor through the
atomic successor list, without a lock, and raises the target's in-degree atomically.

### task_graph.cpp

#### Lock-free construction

`push_node_lock_free()` pushes a node onto three stacks, the lifetime stack and the root and
leaf candidate stacks, in O(1), so graph construction needs no `std::mutex`. The
registration index is stamped before the node is published, so every reader sees a formed
index.

`registration_counter_` is atomic for the same reason. `add_node()`, `add_task()`,
`add_native()` and `add_offload_dynamic()` run on the same lock-free contract as the rest of
graph construction, so a plain counter would race under concurrent callers. `fetch_add`
makes each draw atomic and distinct whoever is calling.

#### Access regions stay on the node

`add_node()` and `add_task()` keep the read and write regions on the node, in every build.
The scheduler uses them to route a KERNEL submit to the queue that owns the USM. Without
them a kernel can run on a device whose context does not own the data, which is a
cross-context illegal access on heterogeneous setups, for example a GPU kernel touching
CPU-context USM.

`add_native()` keeps them as well, for dependency identity. Queue routing is unused for a
native task, which acquires no queue, but the lists stay consistent with every other node
and keep the distributed handle resolution working.

#### Execution mode of a semantic task

`add_task()` decides the mode from capability and explicit intent. KERNEL: a `device_work`
callable is present and the task is not marked host-only with `CUSTOM_HOST`. HOST:
everything else. A `host_work` callable is valid, intentional work and may itself submit
device kernels. The runtime cannot look inside the `std::function`, so choosing host mode is
not an error and must not warn. The only real defect is a task with no executable work at
all; everything else is a deliberate, supported configuration.

#### Distributed offload

The graph decides nothing about offload and names no context. Its constructor takes a
`Graph::OffloadCoordinator*`, null by default, and `compile()` hands `all_nodes` to that
coordinator's `prepare()` after the tracker is reset and before the graph is returned;
`modules/extension/distributed/README.md` says which nodes the distributed coordinator marks. With a null
coordinator no node gets an offload state from `compile()`, so the scheduler's offload
branch does nothing for it. The constructor that takes a `RuntimeContext` ignores it and
builds a graph with no coordinator.

The add functions keep nothing for offload beyond `io_reads_` and `io_writes_`, which
`prepare()` reads. `add_offload_dynamic()` is the exception: it stores the two live lists
in the node's `OffloadState`, in every build.

`add_offload_dynamic()` snapshots the current pointers for dependency ordering and queue
routing. Both reason about a single compiled graph, where the snapshot holds. The node
touches whole allocations that rotate each step, so each live pointer maps to a whole-buffer
region. The live lists drive handle resolution at dispatch, so the node follows the
double-buffer swap without recompiling.

#### The last node added

`all_nodes` is a LIFO stack, so its head is the last node pushed. `name_last_node()` is
called during graph description on the thread that just added the node, which is why a
relaxed load is enough: there is no other writer to order against. `width_last_node()`
mirrors it. A caller that kept the reference its add call returned needs neither.

#### compile

`compile()` does not traverse the whole graph to find roots and leaves. It processes only
the nodes tagged as candidates during insertion: a candidate with in-degree zero is a root,
and a candidate with no successor, inline or spilled, is a leaf and gets an edge to the
sentinel.

When no root is found, `compile()` drains the tracker before it throws. The `CompiledGraph`
and every node it owns are destroyed as the exception unwinds, but the tracker's access
entries still hold raw task pointers into those nodes. The `TaskGraph` survives a failed
`compile()`, so a later `add_node()` would call `resolve()`, traverse the history and call
`add_dependency()` on freed pointers: a use-after-free write. `reset()` retires the entries
and clears the shard tails, so the next `resolve()` starts from an empty, valid history. The
cycle path resets for the same reason.

A non-empty root set is necessary and not sufficient. A graph can have a valid root and a
disjoint dependency cycle whose nodes never reach in-degree zero. They are never dispatched,
completion never fires, and the run hangs forever on the sentinel. `compile()` therefore
simulates the runtime drain with Kahn's algorithm over a shadow copy of the in-degrees; any
node left unreached sits in such a cycle. The check is O(N+E), paid once at compile and
amortised over every replay of the plan.

#### take_nodes

The root and leaf candidate stacks and the tracker history matter only to `compile()`, so
`take_nodes()` drops them and leaves the graph a clean, reusable builder. The lifetime stack
is LIFO, so draining it yields reverse insertion order. The composer re-resolves
dependencies in adoption order and must see the nodes as the caller added them, so the
vector is reversed before it is returned.
