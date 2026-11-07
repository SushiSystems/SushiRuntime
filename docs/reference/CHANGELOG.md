# Changelog

## Unreleased

- 2026-11-07 — tests: Moved the arithmetic of the raw, range and queue test kernels into host functions with small rows; those launch kinds keep no host form (RW-126, `84484f43`).
- 2026-11-07 — distributed: Made `WorkerAgent::stop()` of an agent with `own_threads` false close the master's connection at once, in place of waiting up to 2 s for a loss (RW-130, `df27871c`).
- 2026-11-07 — worker_types: Added `KernelRegistry::register_host_kernel()` and `find_host()`, a kernel that needs no queue, which a worker on a host runtime runs; `register_kernel()` behaves as before (RW-127, `4310f909`).
- 2026-11-07 — api: Added `Recorder::task(access, body, host_form, placement)`, a queue task that also has a host form the host runtime runs (RW-127, `4310f909`).
- 2026-11-07 — device: Added `DeviceQueueSet::shares_context()` and `free_memory()`, which answer a host set without a queue (RW-127, `4310f909`).
- 2026-11-07 — distributed: Added `WorkerAgentOptions::clock`, the clock the link reads; null keeps the steady clock (RW-127, `4310f909`).
- 2026-11-07 — core: Added `Executor`, the port a part that runs jobs takes them through, with a thread-pool adapter beside the seeded one; a host runtime takes any executor (`executor.hpp`).
- 2026-11-07 — execution: Moved the offload members of `ExecutionEngine` into `OffloadHost`, which only the scheduler engine implements; a host context hands out a refusing sink (`offload_host.hpp`).
- 2026-11-07 — memory: Gave `USMSource` a `copy()` and one owner promise for both adapters, which factories make; the allocator takes no queue list (`usm_source.hpp`).
- 2026-11-07 — device: Removed `DeviceQueueSet::holds_queues()`; a device's driver is `DeviceDescription::driver_version` (`device_queue_set.hpp`).
- 2026-11-07 — in_process_transport: Made every endpoint take a `TransportDrive`, a thread by default or an executor with its clock, in place of the options' executor and clock (`transport_drive.hpp`).
- 2026-11-07 — api: Made `Runtime::create_host()` install no process-wide fatal-log handler (`runtime.cpp`).
- 2026-11-07 — topology: Added `make_host_topology()`, the machine a host context stands for (`host_topology.hpp`).
- 2026-11-07 — api: Added `Runtime::create_host()`, a runtime with host devices, host memory and no thread, whose nodes run as jobs on a seeded executor (RW-124, `e6ec9548`).
- 2026-11-07 — api: Gave count, `Extent`, dynamic, device-sized and late-count launches a host form the host runtime runs (RW-126, `7d4c949d`).
- 2026-11-07 — in_process_transport: Made an endpoint given an executor run with no pump thread, its rounds posted as jobs (RW-87, `c156df96`).
- 2026-11-07 — core: Added `Clock`, the port a timed part reads, with a steady and a manual adapter a test advances (RW-122, `ce70e55b`).
- 2026-11-07 — memory: Made the allocator take its memory from a `USMSource`, a SYCL one or the host-heap one a small test uses (RW-123, `da4525a5`).
- 2026-11-07 — device: Gave `DeviceQueueSet` a host form that holds devices and no queue (RW-123, `da4525a5`).
- 2026-11-07 — tests: Applied the test review's verdict to every test, which kept, rewrote, resized or deleted each, and wrote down what a test may reach (`tests/README.md`).
- 2026-11-07 — in_process_transport: Made an endpoint whose `control_queue_frames` is 0 refuse every control frame with `QueueFull`; any other bound behaves as before (RW-119, `in_process_transport.hpp`).
- 2026-11-07 — device: Moved the NUMA routing of `DeviceQueueSet` into `NUMARouteTable`, which is built from NUMA ids alone; the set routes every request as before (RW-103, `numa_route_table.hpp`).
- 2026-11-07 — machine: Added `admits_class()` and `admits()`, which decide whether a `DeviceFilter` accepts one device; discovery calls them and keeps the same devices as before (RW-103, `device_filter.hpp`).
- 2026-11-07 — api: Added `AdvancedControls::install_offload()`, which installs or removes the coordinator the runs of a runtime offer their nodes to; a runtime starts with none, as before (RW-107, `advanced_controls.hpp`).
- 2026-11-07 — network_socket: Gave `Socket::connect()` and `Listener::accept()` a `SocketOptionSetter` parameter whose default, `set_socket_option()`, calls the system; a call that passes nothing behaves as before (RW-106, `socket.hpp`).
- 2026-11-07 — transport: Added `RecordingTransportEvents`, the in-memory adapter of the `TransportEvents` port, beside the port (RW-105, `recording_transport_events.hpp`).
- 2026-11-07 — task: Added `RefusingOffloadSink`, an `OffloadSink` whose `resubmit()` throws and counts (RW-105, `refusing_offload_sink.hpp`).
- 2026-11-07 — cluster_load: Added `InMemoryOffloadPolicy`, the in-memory adapter of the `OffloadPolicy` port (RW-105, `in_memory_offload_policy.hpp`).
- 2026-11-07 — core: Added `AssertionHandlerSlot`, which holds a handler and reports a failure without stopping, and a `version_matches()` form over two versions; the existing calls behave as before (RW-101, `assert.hpp`, `version.hpp`).
- 2026-11-07 — measurement: Made `CompletionSignal::wait_for()` with a zero budget read the flag and no clock; a positive budget behaves as before (RW-101, `completion_signal.hpp`).
- 2026-11-07 — execution: Moved the run-observer list out of `RuntimeContext` into `RunObserverRegistry`, so the gate rules run in tests with no device; the context behaves as before (RW-102).
- 2026-11-07 — task: Added the `PlacementMemory` port with a USM adapter and an in-memory one; `placement_of()` and `classify_chain()` gain forms that take it, and the old forms behave as before (RW-102).
- 2026-11-07 — scheduler: Gave `WorkloadCoordinator` a constructor that takes a `HeartbeatClock`, and a public `beat()`; the three-argument constructor still sleeps on the system clock (RW-100).
- 2026-11-07 — scheduler: Removed the 1 ms sleep from `PriorityTrackingTask` and ran the priority-bypass test on a one-worker pool that waits on `wait_quiescent()` (RW-100).
- 2026-11-07 — profiling: Gave `register_profiler_parts()` a form that takes a `ProfiledRuntime`, the port the registration reads a runtime through; the `Runtime` form forwards to it (RW-98, `profiled_runtime.hpp`).
- 2026-11-07 — profiling: Gave `device_track_name()` a form that takes a device list, and ran the environment facts small on `InMemoryProfiledRuntime` (RW-99, `device_track_name.hpp`).
- 2026-11-07 — tools: Gave the naming, device-source and tier-table checkers a text-taking function beside each file reader, so their tests run small on text; the two tier-table tests of the real CMake file stay medium (RW-95).
- 2026-11-07 — memory_exchange: Moved the import refusal into `descriptor_refusal()` and `device_refusal()`, pure functions its rows test small with no device; one medium import test keeps the wiring (RW-89, `descriptor_refusal.hpp`).
- 2026-11-07 — tests: Dropped the test of the empty `ExternalHandleKindSet` default, trivial code the testing skill leaves untested (RW-91).
- 2026-11-07 — examples: Gave the diffusion adapter a second `create_backend()` form that takes the runtime it runs on, and exposed its step as `diffused_cell()` and `diffuse_on_host()`; the first form behaves as before (RW-92).
- 2026-11-07 — tests: Gave each header whose tests the review deleted a `compile_<name>.cpp` that includes it alone, which clears the four mirror findings (RW-94).
- 2026-11-06 — distributed: Moved `InProcessTransport` out of `distributed` into its own module, `in_process_transport`, beside the `Transport` port; its behaviour is unchanged (RW-81).
- 2026-11-06 — tests: Added in-memory adapters for five more ports, among them `Task` and `AllocationOwner`, and moved `RecordingObserver` beside its port into `report`.
- 2026-11-06 — cli: Gave the project, package and container commands a `Surroundings` seam so the CLI tests run on sushicore's recording runner and console; the CLI needs sushicore 0.10.12 (`surroundings.py`).
- 2026-11-06 — cli: Fixed `sr config` failing with an AttributeError under sushicore 0.10.12, which reads `_ENV_OVERRIDES` from the configuration module (`config.py`).
- 2026-11-06 — tests: Set the size time limits to 1 s, 10 s and 120 s, the testing skill's numbers, and removed `K_ALL_KEYS`, which nothing read (`cmake/SushiRuntimeTests.cmake`, `profile_keys.hpp`).
- 2026-11-06 — tests: Named every benchmark suite `Benchmark_` for `sr bench` and moved the promises the benchmarks checked into sized tests; the epoch stress tests became `Large_` (`test_epoch_reclamation.cpp`).
- 2026-11-06 — tests: Added in-memory adapters for the rebalancer, offload and topology ports, beside each port (`in_memory_telemetry.hpp`, `in_memory_offload_coordinator.hpp`, `in_memory_topology_provider.hpp`).
- 2026-11-06 — tools: Fixed the mirror checker refusing `tests/support`, the shared test helper folder, by taking SushiSkills' copy (KI-26, `check_test_mirror.py`).
- 2026-11-06 — licence: Relicensed SushiRuntime under the Apache License 2.0 and removed `COMMERCIAL.md`; every source file's licence block follows (`LICENSE`, `tools/settings.py`).
- 2026-11-05 — tools: Took the shared checkers of 2026-11-05 with a baseline of the findings they meet, which RW-80 clears.
- 2026-10-30 — build: Fixed a `tsan` build failing under `-Werror` on every unit compiled without `-fsycl`, which has no device pass for the TSan opt-out to apply to (`cmake/Sanitizers.cmake`).
- 2026-10-30 — cli: Made `sr build --type tsan` link SushiLog and SushiProfiler packages built with `--sanitize thread`, as RelWithDebInfo packages; it linked Release ones (`library_packages.py`).
- 2026-11-07 — distributed_layer: Ran the TransportContract suite small against InProcessTransport (RW-87, `c40854ec`).
- 2026-11-07 — memory: Made `EpochManager` an ordinary object that its guard and the dependency tracker take, with no process-wide instance (`754279e7`).


## v0.5.1 — 2026-10-30

- 2026-10-29 — ci: Gave the sanitizer job 90 minutes and the SushiBLAS job 150; both ran out of time building their packages (`.github/workflows/ci.yml`).
- 2026-10-29 — tests: Fixed every Windows Debug test program crashing on load; the tree stages the Release gtest.dll it links instead of the Debug one vcpkg copied (`cmake/SushiRuntimeTests.cmake`).
- 2026-10-29 — tests: Quarantined five medium tests under AdaptiveCpp, whose CPU backend fails them, as KI-18 to KI-22 with RW-75 to fix them (`cmake/SushiRuntimeTests.cmake`).
- 2026-10-29 — scheduler: Fixed two scheduler tests failing to compile under AdaptiveCpp, whose handler has no `host_task`; they enqueue through `Detail::enqueue_host_task` (`test_dispatch_record.cpp`, `test_event_polling.cpp`).
- 2026-10-29 — ci: Built SushiBLAS against this commit with icpx, the compiler the oneAPI jobs use, so its SushiRuntime package finds IntelSYCL (`.github/workflows/ci.yml`).
- 2026-10-29 — device: Fixed the device line failing to compile under AdaptiveCpp, whose backends carry other names than DPC++'s (`device_queue_set.cpp`, `runtime_environment.cpp`).
- 2026-10-29 — build: Gave the Debug line tables to the host pass alone under intel/llvm, whose spir64 device pass rejects the flag (`cmake/Optimization.cmake`).
- 2026-10-29 — build: Let a Debug tree link an imported package built as `None`, such as Ubuntu's GTest (`cmake/SushiRuntimeLanguage.cmake`).
- 2026-10-29 — deps: Moved the SushiProfiler pin to v0.4.1, whose CLI installs against sushicore 0.10 (`cli/sushistack.deps.toml`).
- 2026-10-29 — ci: Installed the CLI of every pinned Sushi library beside its clone, since a library package is built by that library's own CLI (`.github/actions/setup-sr/action.yml`).
- 2026-10-29 — deps: Moved the SushiLog pin to v0.3.0, the first tag a consumer can build as a package (`cli/sushistack.deps.toml`).
- 2026-10-29 — cli: Handed the library packages the compiler as the build environment's PATH finds it, so a oneAPI `icpx` that only setvars puts on the PATH no longer stops `sr build` (`services/project.py`).
- 2026-10-29 — ci: Cloned the Sushi libraries from the `[sushisystems.*]` pins, which the clone steps still read as `[sushi.*]`, and installed the CLI before the checkers so they find sushicore (`.github/actions/setup-sr/action.yml`).
- 2026-10-29 — cli: Imported `configure_arguments` from sushicore where the build calls it (`services/project.py`).
- 2026-10-29 — cli: Fixed `sr package` printing every line of `cmake --install`; it prints one line counting the files through sushicore 0.10.3, and cmake's output only on a failure (`packaging.py`).
- 2026-10-29 — cli: Fixed the command-list tests that the `backlog` and `design` groups broke; sushicore's groups are checked by name only (`test_cli.py`, `test_help_screen.py`).
