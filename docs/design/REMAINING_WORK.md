# Remaining work

Next id: RW-74

## Decisions waiting for the owner

### RW-1 Reconcile `AGENTS.md` with the delegation rule
- State: decision
- Done when: `AGENTS.md` and the owner's delegation rule agree on whether a subagent runs `sr`

### RW-4 Choose how a host read of Shared memory during a run is made safe
- State: decision
- Done when: the owner has chosen a read that waits on the owning queue, a debug-build guard
  that reports the access with its call site, or the documented rule that a value read mid-run
  lives in `Residency::Host`

### RW-5 Choose the install path for `sr` under WSL
- State: decision
- Done when: a WSL shell has a documented way to install and run `sr`

### RW-6 Rule on the public-surface reshapes of the API review
- State: decision
- Done when: the owner has ruled on each reshape, each a breaking change of its own

QC-21, 22, 23 (`IExecutionEngine`, `Node`, `TaskScheduler` responsibilities); QC-56 (types in
`buffer.hpp`); QC-57, 67, 68 (`Recorder` listener, `untracked_kernel` order, `scratch()` span);
QC-59 (no `Dynamic` overload); QC-63 (`dynamic_graph.hpp` includes `runtime.hpp`); QC-85
(`Node::profiling` and `Core::Backend` unread); QC-109 (`EpochManager` singleton); QC-113, 114
(topology distances); QC-122 (`WorkerStats`); QC-126 (interop handle switch); QC-93, 103, 116,
136 (naming, `enqueue_host_task`).

### RW-57 Decide what goes of the leftovers in git and on disk
- State: decision
- Done when: the two stash entries of 2026-06-20, `.superpowers/` and the unmerged branch
  `pre-clean-main` are each kept or deleted by the owner

## Correctness

### RW-67 Make `StateStopsOnAWindowPastTheEnd` pass under parallel test jobs
- State: open
- Done when: the cause of KI-17 is found, the test passes ten runs of the medium label at four
  jobs, and it leaves `SR_QUARANTINED_TESTS`

### RW-7 Detect a `Buffer` moved after it was recorded
- State: open
- Done when: the liveness guard reports a recorded `Buffer` that moved (finding H4 of
  the code and architecture review document in the version history)

### RW-8 Run the topology code on a machine with several NUMA nodes and processor groups
- State: unmeasured
- Done when: NUMA placement, processor groups and the POSIX affinity branch have run on such a
  machine (QC-02, QC-29, QC-33)

## Design phases

### RW-9 Build graph replay P2
- State: open
- Done when: plans without device-sized nodes record, finalize and replay, with fallback on
  failure
- Source: `docs/design/GRAPH_REPLAY/README.md`

### RW-10 Measure the gain of graph replay P0, P1 and P3
- State: unmeasured
- Done when: the gain through `Graph::run` is measured on each device
- Source: `docs/design/GRAPH_REPLAY/README.md`

### RW-11 Build backbone BB-1b, the sync export from SYCL to Vulkan
- State: open
- Done when: a SYCL completion can be exported as a Vulkan semaphore
- Source: `docs/design/ENGINE_BACKBONE_REFACTOR/BB_1_VULKAN_INTEROP.md`

### RW-12 Re-check backbone BB-5 to BB-8 against the tree
- State: open
- Done when: the pages BB-5 to BB-8 of the backbone plan and its status line match the code, BB-5d's
  redistributable licence question included
- Source: `docs/design/ENGINE_BACKBONE_REFACTOR/README.md`

### RW-13 Build the multi-colour dispatch node, R9
- State: open
- Done when: a graph-colouring consumer pays one scheduler round trip per kind, not per colour
- Source: `docs/design/PHYSICS_SUBSTRATE_REQUIREMENTS/README.md`

### RW-14 Build simulation substrate WP-5 and WP-7, and re-check the rest
- State: open
- Done when: peer-to-peer domain decomposition and the native CPU path exist, and the other work
  packages are checked against the tree
- Source: `docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN/README.md`

### RW-69 Measure the closed-gate cost of the runtime's zones
- State: unmeasured
- Done when: `tests/engine/execution/benchmark_scheduler_execution_engine.cpp` has printed the time of
  a run of N empty nodes at levels 0, 1 and 2 with the gate closed and open, on a named machine, and the
  default `SR_PROFILE_LEVEL` is 1 if level 2 missed the profiler's budget
- Source: `docs/design/PROFILING/README.md`

### RW-70 Time the distributed layer
- State: open
- Done when: dispatch, transport and settle write zones and flows, so a record can place a remote task
  on the timeline
- Source: `docs/design/PROFILING/README.md`

### RW-71 Build and run the profiling work
- State: unmeasured
- Done when: `sr build` and `sr test` pass with `SR_ENABLE_PROFILER` on and off, the end-to-end session
  tests pass on the CPU SYCL device, and the `Large_` tests ran on the GPU and on a second device
- Source: `docs/design/PROFILING/README.md`

All of P1 to P4 is written and has not been compiled.

### RW-72 Replace the runtime's local log throttles with SushiLog's
- State: blocked
- Blocked by: external: SushiLog ships log-once, a rate limit and a self-reporting counter
- Done when: `RulePlacement::is_first_warning` and the TCP attempt throttle are gone, the drop counts
  of a run use SushiLog's counter, and the tests that read their lines pass unchanged
- Source: `docs/design/PROFILING/README.md`

### RW-73 Return a `Result` from every recording call
- State: blocked
- Blocked by: RW-6
- Done when: a refused recording reports its error at the call and not at the next run
- Source: `docs/design/PROFILING/README.md`

The runtime logs the first refusal and names the later ones as dropped; the public change belongs to the
review RW-6 holds.

## Release

### RW-15 Bump to 1.0.0 and tag it
- State: blocked
- Blocked by: RW-12
- Done when: the freeze ends with the 1.0.0 tag

### RW-16 Move to C++20
- State: blocked
- Blocked by: external: the SYCL toolchain baseline
- Done when: concepts, `std::span`, `std::barrier`, `std::format` and `std::source_location`
  replace the shims `core/bit.hpp` and `core/span.hpp`

`Core::Result<T, E>` stays by decision BB-4.5.

## Distributed layer

### RW-65 Carry out the runtime's share of layered execution
- State: open
- Done when: tasks R1 to R13 of `docs/design/LAYERED_EXECUTION/README.md` are closed
- Source: `docs/design/LAYERED_EXECUTION/README.md`

R9 closes RW-17 and KI-6; R13 closes RW-21 and RW-22. The programme's entry is `sushiai RW-130`.

### RW-17 Run a worker's kernels through its scheduler and overlap them
- State: open
- Done when: a worker dispatches through its runtime's scheduler, and two dispatches on two
  devices overlap in a test

### RW-18 Protect frames after the handshake
- State: open
- Done when: every frame is encrypted or carries a MAC

### RW-19 Cap the peers that have not registered
- State: open
- Done when: unregistered connections have a cap of their own below `max_peers`

### RW-20 Remember every nonce of a connection
- State: open
- Done when: `MasterLink` checks every `CHALLENGE` on a connection, the sixth `HELLO` included
- Where: `modules/extension/distributed/source/master_link.cpp`

A worker with a wrong secret is not rejected on its connection after five `HELLO`. The connection
stays open until the master's handshake timeout. Cause: `MasterLink` remembers four nonces and
ignores a `CHALLENGE` it cannot check.

### RW-21 Measure the per-dispatch allocation and the release observer
- State: unmeasured
- Done when: the TCP benchmark shows whether `PendingTasks::insert()` needs a pooled entry and
  the release observer a lock-free test

### RW-22 Run the distributed verification
- State: unmeasured
- Done when: 100 repeats of the TCP fault suite, the TCP rows of the distributed benchmark and
  an ASan run of the layer have passed

The ASan run needs a bundle with compiler-rt or the Linux machine.

### RW-23 Read cluster endpoints from a nodes file
- State: open
- Done when: a cluster's addresses can come from a file, not only from flags and the environment

### RW-24 Offer an op recorded on a dynamic-graph region to a worker
- State: open
- Done when: the dynamic composer stamps offloadable nodes as `TaskGraph::compile` does

### RW-25 Release the stopped coordinator at `detach()`
- State: open
- Done when: a `detach()` during a run leaves no coordinator installed (QC-84)

## API quality control

### RW-26 Measure the phase E findings
- State: unmeasured
- Done when: QC-19, 20, 86, 58, 08, 117 and 121 each have a measurement and a ruling

### RW-27 Record a failed event before its successors run
- State: open
- Done when: a node whose status query throws releases no successor before the failure is
  recorded, with a test (QC-18)
- Where: `modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp`

A successor can run before a failed event is recorded. A successor of a failed node runs once.
Cause: when an event's status query throws, the node is released before the failure is stored.

### RW-28 Keep a late `cancel()` from reaching the next run
- State: open
- Done when: a `cancel()` racing the end of its run cannot cancel the next one (QC-12)

### RW-29 Test the full rebuild after a commit that threw
- State: open
- Done when: a test covers the rebuild after a commit that threw midway (QC-06)

### RW-30 Release the regions a dynamic graph drops
- State: open
- Done when: a `DynamicGraph` that drops regions and never runs again frees them (QC-05)

### RW-31 Resolve `sushi_ptr<T> == sushi_ptr<U>` under C++20
- State: open
- Done when: the comparison is unambiguous under C++20 (QC-28)

### RW-32 Find out whether a SYCL backend takes over an imported file descriptor
- State: unmeasured
- Done when: each backend's behaviour is known and documented (QC-40)

### RW-33 Settle three edge cases of launch and wait
- State: open
- Done when: a zero-size `parallel_for`, `NdRange` tiling and `DeviceQueueSet::wait_all()` after a
  throwing queue are each tested (QC-73, QC-74, QC-115)

### RW-34 Write the changelog entries that were never written
- State: open
- Done when: `docs/reference/CHANGELOG.md` records the seven earlier deprecations (QC-137), the
  dispatch-width work of 2026-09-21 and the `sushicore` lower bound of 0.7.0

### RW-35 Trace the two TCP timeouts at zero
- State: open
- Done when: `TCPWorkerOptions::attempt_timeout` and `write_idle_timeout` at zero are traced and
  documented

### RW-36 Remove the finding numbers from two comments
- State: open
- Done when: `scheduler_execution_engine.cpp` carries no "(F4)" or "(F5)"

### RW-37 Make `NodeDescription` a variant of per-kind structs
- State: open
- Done when: each node kind has its own fields and an unhandled kind is a compile error

### RW-38 Give a recorder about to be cleared a consuming build
- State: open
- Done when: a recorder that is cleared after its run moves its work into the plan, not copies

### RW-39 Cache the region walks of a dynamic graph at commit
- State: open
- Done when: `DynamicGraph::run()` walks no region three times

### RW-40 Translate errors through one helper outside the engine and the surface
- State: open
- Done when: the distributed extension, `usm_allocator.cpp`, `event_timestamps.hpp` and
  `hw_discovery.cpp` translate exceptions through one helper, and `Cluster::attach` holds no
  reference to a context a `Runtime` owns

### RW-41 Give `NodeOptions` named setters
- State: open
- Done when: naming only an operation does not require spelling the width first

### RW-43 Prove the library's allocations on Windows
- State: open
- Done when: a static-link build of the allocation test, or an allocation observer the library
  exports, sees the library's allocations

### RW-44 Keep a cluster plan on a shared buffer in the chain layer
- State: open
- Done when: `ChainRun` reports completions itself under a cluster

### RW-45 Bound the dispatch decoder's names by the encoder's limit
- State: open
- Done when: the decoder refuses every name the encoder refuses

### RW-59 Offer an operand-address late binding
- State: open
- Done when: a compiled plan can take an operand's address at run time, so a consumer feeds a
  batch without an extra dispatch round

### RW-60 Give consumers a public "run after" edge and wider task overloads
- State: open
- Done when: a public "run after" edge exists, `Recorder::task` takes a span of regions and a
  small-buffer callable, and the guide states what concurrent `Recorder::task` guarantees

### RW-61 Group the includes of every source file
- State: open
- Done when: every file groups its includes as the contributing guide states, in step with the
  BLAS tree

### RW-62 State and test host-written memory read by a replayed plan
- State: open
- Done when: the runtime states that a replayed plan reads memory the host wrote before the
  replay, and a test holds it

Item R1 of SushiAI's graph capabilities plan, `docs/design/GRAPH_CAPABILITIES/README.md` in that
repository; its dropout fill (B1, G1) waits on it.

## Structural debt

### RW-46 Take the injector queues and time buckets out of `TaskScheduler`
- State: open
- Done when: each is a brick of its own, as task E0 of
  `docs/agent/2026_10_09_USABILITY_AUDIT/PLAN.md` did; `task_scheduler.cpp` holds 1 022 lines

### RW-47 Inject the sentinel in `ExecutionEngine::execute()`
- State: open
- Done when: `TaskGraph::compile()` returns a pure DAG

### RW-48 Turn on interprocedural optimisation
- State: open
- Done when: `cmake/` sets `INTERPROCEDURAL_OPTIMIZATION`, and PERF-7 and PERF-8 of
  the code and architecture review document in the version history are verified

### RW-49 See every new test fail
- State: open
- Done when: the 36 headers the break check skipped have tests seen failing
- Source: `docs/agent/2026_10_14_TEST_COVERAGE/REPORT.md`

### RW-58 Finish the layout steps of the usability audit
- State: open
- Done when: the library exports only its public symbols and every public header compiles alone
- Source: `docs/agent/2026_10_09_USABILITY_AUDIT/PLAN.md`

## Documentation

### RW-50 Correct the `Doxyfile` comments
- State: open
- Done when: `.config/doxygen/Doxyfile` cites the architecture page at its path

### RW-51 Correct the CMake comments that cite moved documents
- State: open
- Done when: `cmake/SushiRuntimeConfig.cmake.in`, `tests/package/CMakeLists.txt` and
  `cmake/SushiRuntimeRedistributables.cmake` cite documents at their paths

### RW-52 Write `cmake/README.md`
- State: open
- Done when: the build functions and the tier table are described in `cmake/README.md`

### RW-53 Verify the manual's C++ samples and CLI flags
- State: unmeasured
- Done when: every C++ sample compiles against the current headers and each flag in
  `docs/guides/CLI_GUIDE.md` matches the CLI

## Repository hygiene

### RW-54 Add `.clang-format` and `.editorconfig`
- State: open
- Done when: the root holds both files

### RW-55 Bring the ignore rules up to date
- State: open
- Done when: `.gitignore` drops `dependencies/` and `docs/html` and names `.superpowers/`,
  `.pytest_cache/` and `.venv/`

### RW-56 Configure from the build folder
- State: open
- Done when: `cli/sushiruntime/services/project.py` runs the configure and compile steps with
  the build folder as working directory, so no probe file lands in the root

### RW-63 Run the GPU detection's acceptance on a CUDA machine
- State: unmeasured
- Done when: `sr build` reports `SR_GPU_BACKEND_RESOLVED=cuda` and the NVIDIA topology test runs
  and passes

Item R1 of SushiHub's GPU backend provisioning plan; the locate functions, `DetectGpu.cmake` and
`--cuda-path` are in the tree.

### RW-64 Take the build environment from sushicore
- State: open
- Done when: `cli/sushiruntime/` sets its build environment through `sushicore.build_env`, not a
  copy of it in `env.py`

## Documentation architecture

### RW-66 Write the About line of each design topic by hand
- State: open
- Done when: each topic has an `**About:**` line of one sentence written for a cold reader

The topic model generated the lines from the first sentence of each document or from its title.
