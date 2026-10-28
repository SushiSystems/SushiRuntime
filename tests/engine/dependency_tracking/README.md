# dependency_tracking tests

The tests of the `dependency_tracking` module, mirroring `modules/engine/dependency_tracking/`.
`tests/engine/dependency_tracking/CMakeLists.txt` builds them into `sr_dependency_tracking_test_objects`, linked into `sr_engine_tests`.
`sr test` runs them with the rest; `sr run sr_engine_tests --
--gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_dependency_tracker.cpp

The file covers hazard resolution in the lock-free `DependencyTracker`. It pins the edge semantics
on raw nodes: read-after-write, write-after-read and write-after-write each add an edge, two readers
stay parallel, a shared buffer produces one edge and not several, and a node never depends on
itself. The observable is `in_degree`, the count of incoming edges that the scheduler decrements.
