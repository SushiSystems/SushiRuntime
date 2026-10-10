# topology tests

The tests of the `topology` module, built into one program, `sr_topology_tests`, which
`modules/platform/topology/CMakeLists.txt` declares. `sr test` runs it with the rest;
`sr run sr_topology_tests -- --gtest_filter='<pattern>'` runs it alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_affinity.cpp

Thread-to-core pinning is best-effort: on failure it logs a warning and does not throw. The tests
assert that no call throws. They pin from a worker thread, so the affinity of the test runner is
left untouched.
