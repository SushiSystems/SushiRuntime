# Design phases

## Remaining work

| ID | Pri | State | Title | Done when | Blocked by | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RW-9 | P2 | open | Build graph replay P2 | plans without device-sized nodes record, finalize and replay, with fallback on failure |  | `docs/design/GRAPH_REPLAY/README.md` | 2026-10-19 |
| RW-10 | P2 | unmeasured | Measure the gain of graph replay P0, P1 and P3 | the gain through `Graph::run` is measured on each device |  | `docs/design/GRAPH_REPLAY/README.md` | 2026-10-19 |
| RW-11 | P2 | open | Build backbone BB-1b, the sync export from SYCL to Vulkan | a SYCL completion can be exported as a Vulkan semaphore |  | `docs/design/ENGINE_BACKBONE_REFACTOR/BB_1_VULKAN_INTEROP.md` | 2026-10-19 |
| RW-12 | P2 | open | Re-check backbone BB-5 to BB-8 against the tree | the pages BB-5 to BB-8 of the backbone plan and its status line match the code, BB-5d's redistributable licence question included |  | `docs/design/ENGINE_BACKBONE_REFACTOR/README.md` | 2026-10-19 |
| RW-13 | P2 | open | Build the multi-colour dispatch node, R9 | a graph-colouring consumer pays one scheduler round trip per kind, not per colour |  | `docs/design/LAYERED_EXECUTION/PHYSICS_SUBSTRATE_REQUIREMENTS.md` | 2026-10-19 |
| RW-14 | P2 | open | Build simulation substrate WP-5 and WP-7, and re-check the rest | peer-to-peer domain decomposition and the native CPU path exist, and the other work packages are checked against the tree |  | `docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN/README.md` | 2026-10-19 |
| RW-69 | P2 | unmeasured | Measure the closed-gate cost of the runtime's zones | `tests/engine/execution/benchmark_scheduler_execution_engine.cpp` has printed the time of a run of N empty nodes at levels 0, 1 and 2 with the gate closed and… |  | `docs/design/PROFILING/README.md` | 2026-10-26 |
| RW-70 | P2 | open | Time the distributed layer | dispatch, transport and settle write zones and flows, so a record can place a remote task on the timeline |  | `docs/design/PROFILING/README.md` | 2026-10-26 |
| RW-71 | P2 | unmeasured | Build and run the profiling work | `sr build` and `sr test` pass with `SR_ENABLE_PROFILER` on and off, the end-to-end session tests pass on the CPU SYCL device, and the `Large_` tests ran on… |  | `docs/design/PROFILING/README.md` | 2026-10-26 |
| RW-72 | P2 | blocked | Replace the runtime's local log throttles with SushiLog's | `RulePlacement::is_first_warning` and the TCP attempt throttle are gone, the drop counts of a run use SushiLog's counter, and the tests that read their lines… | external: SushiLog ships log-once, a rate limit and a self-reporting counter | `docs/design/PROFILING/README.md` | 2026-10-26 |
| RW-73 | P2 | blocked | Return a `Result` from every recording call | a refused recording reports its error at the call and not at the next run | RW-6 | `docs/design/PROFILING/README.md` | 2026-10-26 |

## Known issues

| ID | Origin | Symptom | Workaround | Fix | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- |
