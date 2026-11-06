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
| RW-90 | P3 | open | Let a memory_exchange import test state the device is a GPU without hardware | The Medium_MemoryExchangeImport fixture skips on no machine |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-06 |
| RW-95 | P3 | open | Give check_naming, check_device_sources and the tier table a text-taking seam | The three tools tests the review resized to small read no file from disk |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-06 |
| RW-96 | P3 | decision | Show the ContextAccess promises through a public path or rule them untested | The five dropped context_access promises have a public-surface test each or an owner ruling |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-06 |
| RW-97 | P3 | decision | Decide where the no-hub-install rule of the service messages lives | test_no_service_message_sends_the_user_to_hub_install reads no source text, or a tools/ checker holds the rule |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-06 |
| RW-98 | P2 | open | Give register_profiler_parts the registration interface of decision 3 | RefusesASecondRegistration and TheRegistryMakesTheDeviceSource run small with no Runtime |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-06 |
| RW-99 | P3 | decision | Give the profiling tests the seams their small resizes need | The nine M13 records left medium for want of a seam run small, or each has an owner ruling |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-06 |
| RW-100 | P3 | decision | Give WorkloadCoordinator a clock and the scheduler a single-thread executor | test_workload_coordinator and PriorityBypass hold no sleep_for and no loop |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-06 |
| RW-101 | P3 | decision | Give the foundation units the seams their small resizes need | The M5 records left medium for a handler, clock, threshold or sink seam run small, or each has an owner ruling |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-102 | P3 | decision | Give the engine units the seams their small resizes need | The M3 records left medium for an executor, logger port, dependency or ownership seam run small, or each has an owner ruling |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-103 | P3 | decision | Give the platform units the seams their small resizes need | The eleven M6 records left medium for a route, release, queue, clock or device port run small, or each has an owner ruling |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-104 | P2 | open | Finish the distributed test records on InProcessTransport | test_worker_agent and the ScriptedWorker and StubTransport tests of test_distributed run on InProcessTransport with no stand-in |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-105 | P3 | decision | Ship the extension adapters the remaining offload tests need | test_offload_dispatcher, test_task_resubmission and test_frame_sending run on an in-memory OffloadPolicy, a refusing sink and a recording TransportEvents |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-106 | P3 | decision | Give the logged TCP_NODELAY failure a public path or rule it untested | The socket option report promise has a test through the public surface or an owner ruling |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-107 | P2 | decision | Give OffloadCoordinator a public install path for the dropped plan-cache tests | The four plan-cache and offloaded-count promises M7 dropped have a public-surface test each, or an owner ruling |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-108 | P3 | decision | Give the api data and detail tests the seams their small resizes need | The M7 data and detail records left medium for want of a predicate or sink seam run small, or each has an owner ruling |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-109 | P3 | decision | Show the NodeStamp parameter copy through a public path or rule it untested | The copy of a node's parameters into its stamp has a public-surface test, or an owner ruling leaves it untested |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-110 | P3 | decision | Give the api units the seams their small resizes need | The M7 api records left medium for a placement, pool, error-text, empty-check or slot seam run small, or each has an owner ruling |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-111 | P3 | open | Give run_for a clock so its multi-step test needs no wall time | A test of run_for completing several steps advances an injected clock and reads no wall time |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-112 | P3 | open | Move four import refusals of test_interop.cpp to small rows of Small_DescriptorRefusal | A null Win32 handle, an fd above INT_MAX, an offset past the allocation and a UUID mismatch have rows in test_descriptor_refusal.cpp |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-113 | P3 | decision | Show the chain-versus-bare-SYCL allocation claims through a public queue | The two dropped growth claims of test_sushiruntime.cpp have a public-surface test each or an owner ruling |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-114 | P3 | decision | Show force_scheduler and the advanced() context link through a public path | The two dropped test_runtime.cpp and test_fluent.cpp promises have a public-surface test each or an owner ruling |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |

## Known issues

| ID | Origin | Symptom | Workaround | Fix | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- |
