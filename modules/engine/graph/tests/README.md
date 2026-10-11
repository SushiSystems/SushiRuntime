# graph tests

The tests of the `graph` module, built into one program, `sr_graph_tests`, which
`modules/engine/graph/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_graph_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_dependency_tracker.cpp

The file covers hazard resolution in the lock-free `DependencyTracker`. It pins the edge semantics
on raw nodes: read-after-write, write-after-read and write-after-write each add an edge, two readers
stay parallel, a shared buffer produces one edge and not several, and a node never depends on
itself. The observable is `in_degree`, the count of incoming edges that the scheduler decrements.

## test_task_graph.cpp

The file covers the builder surface of `TaskGraph`: what an add call returns, which registration
index it refuses, and what `compile()` throws for a graph that cannot run. No test submits work, so the device and host callables do
nothing.

### `add_edge`

`add_edge` wires one dependency by hand, a successor entry and one `in_degree` count, which is what
the dependency pass adds for a hazard. The tracker never produces a cycle from region accesses, so
the cycle tests build theirs with it.

### `EveryAddReturnsTheNodeItAdded`

Each of the seven add overloads is called once. The returned reference must be the node
`last_node_added()` reports right after the call, must carry what that overload sets (a region
list, the bindings, the execution mode, the live lists), and no two calls may share a node.

### `AReturnedNodeTakesItsRegistrationAndConfiguration`

A node added with a registration carries it, and one added without draws the index
`last_registration_index()` reports. A name, a width and a placement written through the reference
are what the "last node" accessors then read back.

### `ARegistrationTheGraphNeverIssuedIsRefused`

`add_node()` on an empty graph is given `NO_REGISTRATION`, then, after one `reserve_registration()`,
the index one past it. Both throw `Errc::invalid_graph`. The first message must name the call, the
index, the count issued and `reserve_registration()`. No node was added by either call.

### `EveryAddOverloadRefusesAnUnissuedRegistration`

Six of the seven add overloads are each given index 7 on a graph that issued none, and each throws
`Errc::invalid_graph`; the test above covers the seventh. A node added afterwards over the same regions has an `in_degree` of
zero and compiles into a plan of one root, so no refused call left an access in the tracker.

### `AReservedRegistrationIsAcceptedTwice`

Two nodes take one reserved index. A third draws its own, and a fourth is given the index the third
drew, which the graph also issued.

### `TheCompiledPlanOwnsTheReturnedNodes`

The references stay valid across `compile()`: the plan's `all_nodes` holds the same two objects.

### `CompileWithNoRootThrowsInvalidGraphNamingANode`

Two nodes that wait on each other leave no root. The test expects `Errc::invalid_graph` and a
message with the node count and one of the two, named by its label and registration index.

### `CompileWithACycleBehindARootThrowsInvalidGraphNamingANode`

A third node with no dependency gives the graph a root, so only the drain check finds the cycle. The
message must count 2 of 3 nodes and name a cycle member. Which member follows the order of
`all_nodes`, so the test accepts either and rejects the root.

### `AGraphCompilesAfterAFailedCompile`

A failed `compile()` leaves the builder empty and usable: one node added afterwards compiles into a
plan of one node and one root.
