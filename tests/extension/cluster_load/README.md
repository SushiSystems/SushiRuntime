# cluster_load tests

The tests of the `cluster_load` module, mirroring `modules/extension/cluster_load/`.
`tests/extension/cluster_load/CMakeLists.txt` builds them into `sr_cluster_load_test_objects`, linked into `sr_extension_tests`. `sr test`
runs them with the rest; `sr run sr_extension_tests -- --gtest_filter='<pattern>'` runs one
program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_offload_policy.cpp

The tests need no hardware and no cluster, so they are small.

| Test | What it proves |
| --- | --- |
| Offload policy tests | The policy tests call `pick_worker()` of `LeastLoadedOffloadPolicy` with synthetic `WorkerLoad` and no cluster. They cover the byte gate, the round-trip and backlog terms of the estimate, the free-memory gate, and the worker that is not alive. |
| `live_worker`, `pick` | `live_worker` builds the `WorkerLoad` of one live worker. `pick` asks a default policy to place a task of a given size on a list of workers. |

## compile_cluster_statistics.cpp and compile_placement_decision.cpp

Each file includes one header alone, `cluster_statistics.hpp` and `placement_decision.hpp`, so the header
carries every include it needs. Both headers hold plain data and a `std::variant` with no behaviour of
their own to test.
