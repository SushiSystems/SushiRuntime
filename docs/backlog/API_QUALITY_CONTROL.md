# Api quality control

## Remaining work

| ID | Pri | State | Title | Done when | Blocked by | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RW-26 | P2 | unmeasured | Measure the phase E findings | QC-19, 20, 86, 58, 08, 117 and 121 each have a measurement and a ruling |  |  | 2026-10-19 |
| RW-27 | P2 | open | Record a failed event before its successors run | a node whose status query throws releases no successor before the failure is recorded, with a test (QC-18) |  |  | 2026-10-19 |
| RW-28 | P2 | open | Keep a late `cancel()` from reaching the next run | a `cancel()` racing the end of its run cannot cancel the next one (QC-12) |  |  | 2026-10-19 |
| RW-29 | P2 | open | Test the full rebuild after a commit that threw | a test covers the rebuild after a commit that threw midway (QC-06) |  |  | 2026-10-19 |
| RW-30 | P2 | open | Release the regions a dynamic graph drops | a `DynamicGraph` that drops regions and never runs again frees them (QC-05) |  |  | 2026-10-19 |
| RW-31 | P2 | open | Resolve `sushi_ptr<T> == sushi_ptr<U>` under C++20 | the comparison is unambiguous under C++20 (QC-28) |  |  | 2026-10-19 |
| RW-32 | P2 | unmeasured | Find out whether a SYCL backend takes over an imported file descriptor | each backend's behaviour is known and documented (QC-40) |  |  | 2026-10-19 |
| RW-33 | P2 | open | Settle three edge cases of launch and wait | a zero-size `parallel_for`, `NdRange` tiling and `DeviceQueueSet::wait_all()` after a throwing queue are each tested (QC-73, QC-74, QC-115) |  |  | 2026-10-19 |
| RW-34 | P2 | open | Write the changelog entries that were never written | `docs/reference/CHANGELOG.md` records the seven earlier deprecations (QC-137), the dispatch-width work of 2026-09-21 and the `sushicore` lower bound of 0.7.0 |  |  | 2026-10-19 |
| RW-35 | P2 | open | Trace the two TCP timeouts at zero | `TCPWorkerOptions::attempt_timeout` and `write_idle_timeout` at zero are traced and documented |  |  | 2026-10-19 |
| RW-36 | P2 | open | Remove the finding numbers from two comments | `scheduler_execution_engine.cpp` carries no "(F4)" or "(F5)" |  |  | 2026-10-19 |
| RW-37 | P2 | open | Make `NodeDescription` a variant of per-kind structs | each node kind has its own fields and an unhandled kind is a compile error |  |  | 2026-10-19 |
| RW-38 | P2 | open | Give a recorder about to be cleared a consuming build | a recorder that is cleared after its run moves its work into the plan, not copies |  |  | 2026-10-19 |
| RW-39 | P2 | open | Cache the region walks of a dynamic graph at commit | `DynamicGraph::run()` walks no region three times |  |  | 2026-10-19 |
| RW-40 | P2 | open | Translate errors through one helper outside the engine and the surface | the distributed extension, `usm_allocator.cpp`, `event_timestamps.hpp` and `hw_discovery.cpp` translate exceptions through one helper, and `Cluster::attach`… |  |  | 2026-10-19 |
| RW-41 | P2 | open | Give `NodeOptions` named setters | naming only an operation does not require spelling the width first |  |  | 2026-10-19 |
| RW-43 | P2 | open | Prove the library's allocations on Windows | a static-link build of the allocation test, or an allocation observer the library exports, sees the library's allocations |  |  | 2026-10-19 |
| RW-44 | P2 | open | Keep a cluster plan on a shared buffer in the chain layer | `ChainRun` reports completions itself under a cluster |  |  | 2026-10-19 |
| RW-45 | P2 | open | Bound the dispatch decoder's names by the encoder's limit | the decoder refuses every name the encoder refuses |  |  | 2026-10-19 |
| RW-59 | P2 | open | Offer an operand-address late binding | a compiled plan can take an operand's address at run time, so a consumer feeds a batch without an extra dispatch round |  |  | 2026-10-19 |
| RW-60 | P2 | open | Give consumers a public "run after" edge and wider task overloads | a public "run after" edge exists, `Recorder::task` takes a span of regions and a small-buffer callable, and the guide states what concurrent `Recorder::task`… |  |  | 2026-10-19 |
| RW-61 | P2 | open | Group the includes of every source file | every file groups its includes as the contributing guide states, in step with the BLAS tree |  |  | 2026-10-19 |
| RW-62 | P2 | open | State and test host-written memory read by a replayed plan | the runtime states that a replayed plan reads memory the host wrote before the replay, and a test holds it |  |  | 2026-10-19 |

## Known issues

| ID | Origin | Symptom | Workaround | Fix | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- |
