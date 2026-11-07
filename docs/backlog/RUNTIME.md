# Runtime

## Remaining work

| ID | Pri | State | Title | Done when | Blocked by | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RW-79 | P2 | open | Build the global handle design, phases H0 to H9 | every phase of docs/design/GLOBAL_HANDLE/README.md is built and the topic is closed |  | `docs/design/GLOBAL_HANDLE/README.md` | 2026-11-04 |
| RW-93 | P2 | open | Use the non-deprecated parallel_for overload in distributed_kernels.hpp | test_distributed_kernels.cpp compiles under -Werror with SR_ENABLE_DISTRIBUTED on |  | `applications/examples/distributed_kernels.hpp` | 2026-11-06 |
| RW-125 | P1 | open | Move every logic test that only a device or a thread could reach onto its seam as small | Every Medium_ case the 2026-11-07 audit rules LOGIC runs small on its seam, one medium case is kept per boundary |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-126 | P2 | open | Give NdRange, raw, queue, device-sized and late-count launches a host form | Each of those launch kinds runs on Runtime::create_host, and the tests the medium audit kept medium for want of it run small |  | `modules/surface/api/include/SushiRuntime/api/detail/emit_launch.hpp` | 2026-11-07 |
| RW-127 | P1 | open | Distributed threads run as jobs on a SeededExecutor | The cluster monitor, MasterLink, DispatchExecutor and WorkerAgent loops run as SeededExecutor jobs, and distributed LOGIC cases run small |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-128 | P2 | open | Profiling tests reach the process-wide SushiProfiler Collector | Session::start takes an injected Collector, and the 41 profiling LOGIC cases that record periods or plan zones run small on one |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |
| RW-129 | P2 | open | TaskScheduler, ChainRun and queue-taking task bodies have no host seam | TaskScheduler runs on a SeededExecutor, ChainRun and execute_host take a queue port, and the engine LOGIC cases left medium run small |  | `docs/agent/2026_11_05_TEST_REVIEW/REPORT.md` | 2026-11-07 |

## Known issues

| ID | Origin | Symptom | Workaround | Fix | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- |
| KI-1 | own | A host read of a Shared buffer during a run faults on Windows with CUDA | keep any value read mid-run in `Residency::Host` | RW-4 |  | 2026-10-19 |
| KI-2 | own | A `Buffer` moved after it was recorded leaves the graph on the old address | do not move a `Buffer` a graph recorded; see the `@warning` on its move constructor | RW-7 |  | 2026-10-19 |
| KI-3 | own | A `cancel()` that races the end of its run can cancel the next run | wait for the run before starting the next one after a cancel | RW-28 |  | 2026-10-19 |
| KI-4 | own | A `DynamicGraph` keeps the regions it dropped until it is destroyed | run the graph once more, or destroy it | RW-30 |  | 2026-10-19 |
| KI-6 | own | A distributed worker runs one kernel at a time | run one worker per device | RW-17 |  | 2026-10-24 |
| KI-8 | own | `Cluster::detach()` during a run leaves the stopped coordinator installed | call `detach()` between runs | RW-25 |  | 2026-10-19 |
| KI-9 | own | Any host that reaches the master's port can hold every peer slot | keep the port behind a firewall | RW-19 |  | 2026-10-19 |
| KI-10 | own | An op recorded on a dynamic-graph region always runs on the master | record the op on a `Graph` | RW-24 |  | 2026-10-19 |
| KI-11 | own | Frames between master and worker are not protected after the handshake | run the cluster on a trusted network | RW-18 |  | 2026-10-19 |
| KI-17 | own | `StateStopsOnAWindowPastTheEnd` fails under four parallel test jobs | the test is quarantined; run it alone | RW-67 |  | 2026-10-26 |
| KI-23 | own | Boundary successor order follows buffer addresses, so submission order varies by run | none; results stay byte-equal, only order and traces differ | none, a stable buffer id is designed in the global handle topic | `modules/surface/api/source/region_composer.cpp` | 2026-11-01 |
| KI-25 | own | DeviceIndex follows enumeration order and one card may appear under two backends | set the SYCL device selector to one backend per card | none, a stable device key is designed in the global handle topic | `modules/platform/topology/source/hw_discovery.cpp` | 2026-11-01 |
