# Profiler keys

Every name the runtime writes into a SushiProfiler record: the source, the tracks, the span attributes, the
zones, the values and counters, and the facts. A name is declared once, in the source file the table cites, and
the profiler's design asks for the list in the client's own manual.

## The source and its tracks

| Name | Holds | Declared in |
| --- | --- | --- |
| `sushiruntime.device` | The source that reports device work as spans | `source_names.hpp` (`K_DEVICE_SOURCE_NAME`) |
| `<node>/<device index>/<device name>` | The track of one device of one runtime, built by `device_track_name()` | `device_track_name.hpp` |
| `<node>/host` | The track of the nodes that ran on a worker thread with no queue, built by `host_track_name()` | `device_track_name.hpp` |

`<node>` is the machine's name in `NodeFacts`, so two runtimes in one process have distinct tracks. A library
that states a fact keyed by a track calls `device_track_name()` and never builds the string itself.

## Span attributes

A span's name is the node's name in the plan's node table, or `node <index>` when the table has no row for it.

| Key | Kind | Holds | When it is written |
| --- | --- | --- | --- |
| `node` | integer | The node's registration index | Always |
| `invocation` | integer | The step index within the run | Always |
| `operation` | integer | The hash of the node's operation | When the node names one |
| `launched_width` | integer | The elements the dispatch launched over | When known |
| `live_width` | integer | The elements that did work | When known |
| `layer` | name | `scheduler` or `chain`, the path that ran the dispatch | Always |
| `graph` | name | The label of the graph that owns the plan, set with `Graph::set_label()` | When the graph has a label |
| `origin` | name | The caller's own operation the node belongs to, from `NodeOptions::origin` | When the node states one |
| `flops` | integer | Floating-point operations of one dispatch, as the caller counted them | When the node states a cost |
| `bytes` | integer | Bytes one dispatch reads and writes, as the caller counted them | When the node states a cost |
| `dtype` | name | `f32`, `f16`, `bf16` or `i8` | When the node's cost names a type |

The keys `node`, `invocation`, `operation`, `launched_width`, `live_width`, `graph`, `origin`, `flops`, `bytes` and `dtype` are
`SushiProfiler::DeviceSpanKeys`; `layer` is the runtime's own (`profile_keys.hpp`).

## Zones

All zones are in the group `Runtime`. The compiled level is `SR_PROFILE_LEVEL`, 2 by default; a zone above it
is compiled out. Level 1 is a stage, level 2 a fine zone and level 3 an element-level zone.

| Zone | Level | Where |
| --- | --- | --- |
| `plan.build` | 1 | Building a plan from a recorder (`plan_cache.cpp`) |
| `plan.compile` | 1 | Compiling a task graph (`task_graph.cpp`) |
| `plan.prepare` | 2 | Preparing the plan's nodes for a coordinator and capturing its state |
| `plan.classify` | 2 | Deciding whether a plan can run as a chain |
| `region.commit` | 1 | Composing the plan of a dynamic graph |
| `run.steps` | 1 | A blocking run's steps (`step_run.cpp`) |
| `run.submit_step` | 1 | Starting an asynchronous step |
| `run.finish_step` | 1 | Closing an asynchronous step |
| `run.submit` | 1 | Handing a graph to the scheduler (`scheduler_execution_engine.cpp`) |
| `run.wait` | 1 | Waiting for a scheduled run |
| `run.wait.latch` | 2 | The part of `run.wait` spent on the completion latch |
| `run.wait.quiesce` | 2 | The part spent draining the pool |
| `run.wait.errors` | 2 | The part spent reading the queues' asynchronous errors |
| `run.collect` | 2 | Reading the ledger into an observation |
| `chain.submit_step` | 1 | Submitting one step to the in-order queue (`chain_run.cpp`) |
| `chain.wait` | 1 | The chain's wait |
| `chain.record_events` | 2 | Reading the events of the chain's dispatches |
| `dispatch.task` | 2 | One task's dispatch (`task_scheduler.cpp`) |
| `dispatch.submit` | 2 | One submission to a queue |
| `dispatch.host` | 2 | A host node's body, inline |
| `worker.steal` | 3 | A worker's search for a task to steal |
| `wait.runtime` | 1 | `Runtime::wait()` |
| `wait.queues` | 2 | Waiting for every built device queue |
| `memory.allocate` | 2 | One USM allocation |
| `memory.release` | 2 | One USM release |
| `lock.pending` | 1 | A contended acquisition of the scheduler's pending-submission mutex; an uncontended one writes nothing |

## Values and counters

| Name | Kind | Unit | Written |
| --- | --- | --- | --- |
| `graph.nodes` | value | nodes | At the end of every compile; the recorded nodes, without the completion sentinel |
| `graph.roots` | value | nodes | At the end of every compile |
| `memory.allocate_bytes` | value | bytes | With every allocation |
| `worker.busy` | value | ms | At the end of every run: the workers' time inside task execution |
| `worker.stealing` | value | ms | The workers' time searching victims |
| `worker.polling` | value | ms | The workers' time polling in-flight events |
| `worker.idle` | value | ms | The workers' time backing off or waiting to be armed |
| `run.tasks` | counter, add | tasks | At the end of every run |
| `run.steals` | counter, add | tasks | At the end of every run: tasks stolen, local and remote |
| `transfer.h2d_bytes` | counter, add | bytes | At the end of every run: explicit host-to-device bytes |
| `transfer.d2h_bytes` | counter, add | bytes | At the end of every run: explicit device-to-host bytes |

The four `worker.*` values are zero for a run that used no worker, and for a run that recorded no device timing,
since the buckets are filled only then.

## Facts

The environment contributor sets these when a session starts; the `build` facts are the library's own.

| Fact | Section | Holds |
| --- | --- | --- |
| `sushiruntime.version` | environment | The library's version |
| `sushiruntime.<node>.devices` | environment | The device count of a runtime |
| `sushiruntime.<node>.device.<i>.name`, `kind`, `backend`, `driver`, `memory_bytes`, `compute_units`, `numa_node` | environment | One device; `driver` is `unknown` when the backend does not answer |
| `sushiruntime.<node>.workers`, `rebalancer` | environment | The worker pool and the rebalancer state |
| `sushiruntime.build_type` | build | `release` when assertions are off, `debug` otherwise |
| `sushiruntime.profile_level` | build | The compiled `SR_PROFILE_LEVEL` |
| `sushiruntime.sanitizer` | build | `address`, `thread` or `none` |
| `sushiruntime.sycl_toolchain` | build | The SYCL compiler version, or `unknown` |
