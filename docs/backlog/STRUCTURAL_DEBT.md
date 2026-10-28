# Structural debt

## Remaining work

| ID | Pri | State | Title | Done when | Blocked by | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RW-46 | P2 | open | Take the injector queues and time buckets out of `TaskScheduler` | each is a brick of its own, as task E0 of `docs/agent/2026_10_09_USABILITY_AUDIT/PLAN.md` did; `task_scheduler.cpp` holds 1 022 lines |  |  | 2026-10-19 |
| RW-47 | P2 | open | Inject the sentinel in `ExecutionEngine::execute()` | `TaskGraph::compile()` returns a pure DAG |  |  | 2026-10-19 |
| RW-48 | P2 | open | Turn on interprocedural optimisation | `cmake/` sets `INTERPROCEDURAL_OPTIMIZATION`, and PERF-7 and PERF-8 of the code and architecture review document in the version history are verified |  |  | 2026-10-19 |
| RW-49 | P2 | open | See every new test fail | the 36 headers the break check skipped have tests seen failing |  | `docs/agent/2026_10_14_TEST_COVERAGE/REPORT.md` | 2026-10-19 |
| RW-58 | P2 | open | Finish the layout steps of the usability audit | the library exports only its public symbols and every public header compiles alone |  | `docs/agent/2026_10_09_USABILITY_AUDIT/PLAN.md` | 2026-10-19 |

## Known issues

| ID | Origin | Symptom | Workaround | Fix | Source | Opened |
| --- | --- | --- | --- | --- | --- | --- |
