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
