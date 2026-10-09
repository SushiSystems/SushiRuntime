# graph

The task graph and the dependency tracker that orders its nodes. The headers are in
`include/SushiRuntime/graph/`. This page records why the sources behave as they do; the
source comments state the invariant and point here.

## dependency_tracker.cpp

### resolve

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

### reset

`reset()` retires the access history of every shard. It is safe because it is called only
when graph compilation is completely done.

### add_dependency

`add_dependency()` checks the inline successor list and then the spillover list for the
edge before adding it, so an edge is recorded once. It registers the successor through the
atomic successor list, without a lock, and raises the target's in-degree atomically.

## task_graph.cpp

### Lock-free construction

`push_node_lock_free()` pushes a node onto three stacks, the lifetime stack and the root and
leaf candidate stacks, in O(1), so graph construction needs no `std::mutex`. The
registration index is stamped before the node is published, so every reader sees a formed
index.

`registration_counter_` is atomic for the same reason. `add_node()`, `add_task()`,
`add_native()` and `add_offload_dynamic()` run on the same lock-free contract as the rest of
graph construction, so a plain counter would race under concurrent callers. `fetch_add`
makes each draw atomic and distinct whoever is calling.

### Access regions stay on the node

`add_node()` and `add_task()` keep the read and write regions on the node, in every build.
The scheduler uses them to route a KERNEL submit to the queue that owns the USM. Without
them a kernel can run on a device whose context does not own the data, which is a
cross-context illegal access on heterogeneous setups, for example a GPU kernel touching
CPU-context USM.

`add_native()` keeps them as well, for dependency identity. Queue routing is unused for a
native task, which acquires no queue, but the lists stay consistent with every other node
and keep the distributed handle resolution working.

### Execution mode of a semantic task

`add_task()` decides the mode from capability and explicit intent. KERNEL: a `device_work`
callable is present and the task is not marked host-only with `CUSTOM_HOST`. HOST:
everything else. A `host_work` callable is valid, intentional work and may itself submit
device kernels. The runtime cannot look inside the `std::function`, so choosing host mode is
not an error and must not warn. The only real defect is a task with no executable work at
all; everything else is a deliberate, supported configuration.

### Distributed offload

The graph decides nothing about offload. `compile()` hands `all_nodes` to
`DistributedCoordinator::prepare()` when the context has a coordinator, after the tracker is
reset and before the graph is returned; `src/distributed/README.md` says which nodes it
marks. With no coordinator no node gets an offload state from `compile()`, so the
scheduler's offload branch does nothing for it.

The add functions keep nothing for offload beyond `io_reads_` and `io_writes_`, which
`prepare()` reads. `add_offload_dynamic()` is the exception: it stores the two live lists
in the node's `OffloadState`, in every build.

`add_offload_dynamic()` snapshots the current pointers for dependency ordering and queue
routing. Both reason about a single compiled graph, where the snapshot holds. The node
touches whole allocations that rotate each step, so each live pointer maps to a whole-buffer
region. The live lists drive handle resolution at dispatch, so the node follows the
double-buffer swap without recompiling.

### The last node added

`all_nodes` is a LIFO stack, so its head is the last node pushed. `name_last_node()` is
called during graph description on the thread that just added the node, which is why a
relaxed load is enough: there is no other writer to order against. `width_last_node()`
mirrors it.

### compile

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

### take_nodes

The root and leaf candidate stacks and the tracker history matter only to `compile()`, so
`take_nodes()` drops them and leaves the graph a clean, reusable builder. The lifetime stack
is LIFO, so draining it yields reverse insertion order. The composer re-resolves
dependencies in adoption order and must see the nodes as the caller added them, so the
vector is reversed before it is returned.
