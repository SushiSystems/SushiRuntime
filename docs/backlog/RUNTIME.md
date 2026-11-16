# Runtime

## Remaining work

| ID | Pri | State | Title | Done when | Blocked by | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RW-79 | P2 | open | Build the global handle design, phases H0 to H10 | every phase of docs/design/GLOBAL_HANDLE/README.md is built and the topic is closed |  | `docs/design/GLOBAL_HANDLE/README.md` | 2026-11-04 |
| RW-140 | P2 | open | Measure what a timed chained submit costs over an untimed one | A benchmark row gives host us per chained submit with and without device timing on CUDA, and the gap is named by cause |  | `docs/design/GRAPH_REPLAY/README.md` | 2026-11-14 |
| RW-142 | P2 | open | Let a replay with a late window run its steps back to back | A replayed plan whose window reads host words submits every step without waiting for the previous replay, bytes equal to layer 1 |  | `docs/design/GRAPH_REPLAY/README.md` | 2026-11-14 |
| RW-143 | P3 | open | Test replay_refusal on an in-memory placement memory | A Small test pins each replay refusal reason without a device |  | `docs/design/GRAPH_REPLAY/README.md` | 2026-11-14 |
| RW-144 | P2 | open | Measure the spike table through Graph::run with layer 2 on CUDA and OpenCL | GRAPH_REPLAY carries per-node replay cost for both backends, measured through Graph::run |  | `docs/design/GRAPH_REPLAY/README.md` | 2026-11-14 |
| RW-145 | P2 | open | Stop a task body on the funnel thread deadlocking on an unordered node | A body that waits on an unordered node under the funnel either runs or is refused with a named error, never deadlocks |  | `docs/design/GRAPH_REPLAY/SUBMIT_FUNNEL.md` | 2026-11-14 |
| RW-146 | P2 | open | Give the four GRAPH_REPLAY units of RW-141 their mirrored tests | chain_replay, chained_run_mark, recorded_steps and submit_form each have a mirrored test file and no known finding |  | `docs/design/GRAPH_REPLAY/README.md` | 2026-11-14 |
| RW-148 | P2 | open | Label host and replay runs by their own layer in the profiler | device_source.cpp names Host and Replay runs by their ExecutionLayer, with a test per layer |  | `modules/surface/profiling/source/device_source.cpp` | 2026-11-14 |
| RW-149 | P2 | open | Refuse a nested run from a host body during a host-placed run | A nested run() from a host body of a Host-placed run on a host runtime is refused with a named error, tested |  | `docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md` | 2026-11-14 |
| RW-150 | P2 | open | Count a host-placed run's tasks as host tasks in the run report | fill_run_report counts a Host run's tasks as host tasks on a GPU runtime, tested |  | `modules/foundation/report/include/SushiRuntime/report/run_report.hpp` | 2026-11-14 |
| RW-151 | P3 | open | Record ExecutionLayer::Host in the PROFILING design topic | The PROFILING topic names ExecutionLayer::Host and links RUN_PLACEMENT |  | `docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md` | 2026-11-14 |
| RW-152 | P3 | open | Measure host_step_refusal's per-run region lock cost | A benchmark row gives host_step_refusal's cost per run for a 4-node and a 64-node plan |  | `docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md` | 2026-11-14 |
| RW-156 | P1 | open | Cut the per-node submit cost of planned graphs on the scheduler path | H9 independent and layered tests pass: planned step at most 1.05x CUDA alone; per-node submit near Device residency's 0.14 ms |  | `tests/surface/api/graph/plumbing/test_region_composer/test_partition_hardware.cpp` | 2026-11-16 |
| RW-157 | P1 | open | Report the CPU's reachable host memory as its profile capacity | The CPU profile on the 7600X box gives about 16 GB, and the H9 chain test plans without a pool 0 refusal |  | `modules/engine/device_profile/source/profile_measurement.cpp` | 2026-11-16 |
| RW-158 | P2 | open | Drop device seconds of devices that hold no nodes from the partition ledger | A test shows a device with no nodes in the plan in force reports zero measured seconds |  | `modules/engine/partition_session/source/partition_ledger.cpp` | 2026-11-16 |
| RW-159 | P2 | open | Let a device with one node per step fold its throughput | A fold test with one sample per step per device moves the profile after enough steps |  | `modules/engine/device_profile/source/profile_fold.cpp` | 2026-11-16 |

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
| KI-28 | own | The warm-up period of a GPU session sometimes holds 2 of its 3 device spans | none | none, cause not yet found | `tests/surface/profiling/test_register_profiler_parts/test_runtime_session_devices.cpp` | 2026-11-10 |
| KI-33 | toolchain | A process that used the HIP adapter on gfx1036 exits with 0xC0000409 after its work ends | none; results are correct, only teardown fails |  | `docs/design/GLOBAL_HANDLE/README.md` | 2026-11-15 |
| KI-35 | own | A file space outlives its buffer, and a runtime holds at most 64 memory spaces | Create few file-backed buffers per runtime | none, GLOBAL_HANDLE T46 frees the space on retire | `modules/engine/execution/source/runtime_memory_spaces.cpp` | 2026-11-15 |
| KI-36 | own | Copies of two graphs running at once reach both partition reports | Read a graph's copy counts only from runs that did not overlap another graph's | none, transfer paths do not yet know the graph they copy for | `modules/engine/partition_session/README.md` | 2026-11-15 |
