# examples tests

The tests of the example headers under `applications/examples/`. With the examples built,
`tests/applications/examples/CMakeLists.txt` builds them, with the adapter's source, into
`sr_examples_tests`; the test of `distributed_kernels.hpp` needs the distributed layer as well.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_distributed_kernels.cpp

The file tests the distributed example's kernel: registered under its operation, it adds two
vectors.

| Test | What it proves |
| --- | --- |
| `TheKernelAddsTwoVectors` | The kernel is registered under its operation and adds element by element. |
