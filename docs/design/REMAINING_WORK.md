# Remaining work

Next id: RW-62

## Decisions waiting for the owner

### RW-1 Reconcile `AGENTS.md` with the delegation rule
- State: decision
- Done when: `AGENTS.md` and the owner's delegation rule agree on whether a subagent runs `sr`

### RW-2 Decide whether the CLI shares the product version
- State: decision
- Done when: `cli/pyproject.toml`, `cli/sushiruntime/__init__.py` and `.config/doxygen/Doxyfile`
  carry the library's version, or the owner has ruled that the CLI versions on its own

### RW-3 Cut the v0.4.0 release
- State: decision
- Done when: the owner has approved v0.4.0 and the release commit sets `version.hpp`

More than 49 commits have landed since v0.3.0, several of them breaking; pre-1.0, a breaking
change makes the next release v0.4.0.

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
- Source: `docs/agent/2026_10_11_API_QUALITY_CONTROL/REPORT.md`

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

### RW-7 Detect a `Buffer` moved after it was recorded
- State: open
- Done when: the liveness guard reports a recorded `Buffer` that moved (finding H4 of
  `docs/archive/slop/CODE_AND_ARCHITECTURE_REVIEW.md`)

### RW-8 Run the topology code on a machine with several NUMA nodes and processor groups
- State: unmeasured
- Done when: NUMA placement, processor groups and the POSIX affinity branch have run on such a
  machine (QC-02, QC-29, QC-33)

## Design phases

### RW-9 Build graph replay P2
- State: open
- Done when: plans without device-sized nodes record, finalize and replay, with fallback on
  failure
- Source: `docs/design/GRAPH_REPLAY.md`

### RW-10 Measure the gain of graph replay P0, P1 and P3
- State: unmeasured
- Done when: the gain through `Graph::run` is measured on each device
- Source: `docs/design/GRAPH_REPLAY.md`

### RW-11 Build backbone BB-1b, the sync export from SYCL to Vulkan
- State: open
- Done when: a SYCL completion can be exported as a Vulkan semaphore
- Source: `docs/design/ENGINE_BACKBONE_REFACTOR.md` §5

### RW-12 Re-check backbone BB-5 to BB-8 against the tree
- State: open
- Done when: §9 to §12 of the backbone plan and its status line match the code, BB-5d's
  redistributable licence question included
- Source: `docs/design/ENGINE_BACKBONE_REFACTOR.md`

### RW-13 Build the multi-colour dispatch node, R9
- State: open
- Done when: a graph-colouring consumer pays one scheduler round trip per kind, not per colour
- Source: `docs/design/PHYSICS_SUBSTRATE_REQUIREMENTS.md`

### RW-14 Build simulation substrate WP-5 and WP-7, and re-check the rest
- State: open
- Done when: peer-to-peer domain decomposition and the native CPU path exist, and the other work
  packages are checked against the tree
- Source: `docs/design/SIMULATION_ENGINE_SUBSTRATE_PLAN.md` §7

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

### RW-17 Dispatch a worker's kernels through its scheduler
- State: open
- Done when: `WorkerAgentOptions` takes a device and a worker dispatches through its scheduler
  on every device

### RW-18 Protect frames after the handshake
- State: open
- Done when: every frame is encrypted or carries a MAC

### RW-19 Cap the peers that have not registered
- State: open
- Done when: unregistered connections have a cap of their own below `max_peers`

### RW-20 Remember every nonce of a connection
- State: open
- Done when: `MasterLink` checks every `CHALLENGE` on a connection, the sixth `HELLO` included

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
- Source: `docs/agent/2026_10_11_API_QUALITY_CONTROL/REPORT.md`

### RW-27 Record a failed event before its successors run
- State: open
- Done when: a node whose status query throws releases no successor before the failure is
  recorded, with a test (QC-18)

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
  `docs/archive/slop/CODE_AND_ARCHITECTURE_REVIEW.md` are verified

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
