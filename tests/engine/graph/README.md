# graph tests

The tests of the `graph` module, mirroring `modules/engine/graph/`.
`tests/engine/graph/CMakeLists.txt` builds them into `sr_graph_tests`. `sr test` runs them with the
rest; `sr run sr_graph_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_compiled_graph.cpp

The width of a node is stamped after the node is added, as its name is: the emitters build the
node before they know which of several internal widths it took.

`Node` is a non-literal type, so `Node{}` cannot be an operand of a `static_assert`; the compiler
reports "non-literal type 'Node' cannot be used in a constant expression".

### `BakedNodeReportsOneWidthTwice`

A node with no late bindings reports its baked width as both launched and live, because nothing
narrows it at run time.

### `DynamicNodeSeparatesCapacityFromLiveCount`

A dynamic node launches over its capacity and does work over its live count. The instrument has to
show both, or the narrow kernels look wide.

### `UseBakedFallsBackToTheBakedWidth`

`K_USE_BAKED` means "the rest of the capacity" and is not a count, so it must not be reported as a
measured live width.

### `OversizedWidthReportsUnknownRatherThanTruncating`

A width that does not fit in `uint32_t` is reported as unknown, not truncated. `launched_width` is
32 bits wide, and a wrapped value would read as a real, wrong measurement.

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
`last_node_added()->registration_index` reports. A name, a width and a placement written
through the reference are what the "last node" accessors then read back.

### `ARegistrationTheGraphNeverIssuedStops`

`add_node()` on an empty graph is given `K_NO_REGISTRATION`, then, after one `reserve_registration()`,
the index one past it. Both stop the process. The first line must name the call, the index, the
count issued and `reserve_registration()`.

### `EveryAddOverloadStopsOnAnUnissuedRegistration`

Six of the seven add overloads are each given index 7 on a graph that issued none, and each stops
the process; the test above covers the seventh.

### `AReservedRegistrationIsAcceptedTwice`

Two nodes take one reserved index. A third draws its own, and a fourth is given the index the third
drew, which the graph also issued.

### `TheCompiledPlanOwnsTheReturnedNodes`

The references stay valid across `compile()`: the plan's `all_nodes` holds the same two objects.

### `CompileWithNoRootReturnsInvalidGraphNamingANode`

Two nodes that wait on each other leave no root. `compile()` returns `ErrorCode::InvalidGraph` and a
message with the node count and one of the two, named by its label and registration index.

### `CompileWithACycleBehindARootReturnsInvalidGraphNamingANode`

A third node with no dependency gives the graph a root, so only the drain check finds the cycle. The
message must count 2 of 3 nodes and name a cycle member. Which member follows the order of
`all_nodes`, so the test accepts either and rejects the root.

### `AGraphCompilesAfterAFailedCompile`

A failed `compile()` leaves the builder empty and usable: one node added afterwards compiles into a
plan of one node and one root.

## test_chain_plan.cpp

The file tests ChainPlan and PendingEvent: an empty plan that does not chain until classified.

| Test | What it proves |
| --- | --- |
| `ADefaultPlanDoesNotChain` | A default plan does not chain and holds nothing. |
| `APendingEventStartsWithUnknownWidths` | A pending event's widths start unknown. |

## test_compiled_graph.cpp: the node table of a plan

| Test | What it proves |
| --- | --- |
| `Small_CompiledGraph.CapturesOneTableRowPerRegistrationIndex` | The plan builds one row per registration index, with the node's name and operation. |
| `Small_CompiledGraph.ASharedIndexKeepsItsMultiplicity` | Several nodes sharing one index make one row that counts them. |
| `Small_CompiledGraph.CarriesTheCostOfTheFirstNode` | The first node of an index names the row and states its cost. |
| `Small_CompiledGraph.AnIndexNoNodeRegisteredUnderHasNoRow` | An index no node registered under has no row, and the sentinel has none. |
| `Small_CompiledGraph.ANodeWithNoDeviceWorkIsHostNative` | A node with no device work is marked host-native and a kernel node is not. |
| `Small_CompiledGraph.AnEmptyPlanBuildsAnEmptyTable` | An empty plan builds an empty table rather than none. |
| `Small_CompiledGraph.ACaptureAfterTheFirstLeavesTheEarlierTableIntact` | A second capture builds a fresh table and leaves the one a run still holds intact. |
