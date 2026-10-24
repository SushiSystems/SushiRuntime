# Known issues

Next id: KI-17

## Runtime

### KI-1 A host read of a Shared buffer during a run faults on Windows with CUDA
- Origin: own
- Where: `modules/surface/api/include/SushiRuntime/api/data/buffer.hpp`
- Cause: the host touches a `Residency::Shared` buffer while a kernel runs on the same GPU
- Recognise: the process stops with `0xC0000006` during a print, a save or a mid-run read
- Workaround: keep any value read mid-run in `Residency::Host`
- Fix: RW-4

### KI-2 A `Buffer` moved after it was recorded leaves the graph on the old address
- Origin: own
- Where: `modules/surface/api/include/SushiRuntime/api/data/buffer.hpp`
- Cause: the graph records the handle by address and the liveness token moves with the value
- Recognise: a run reads or writes freed memory and the liveness guard says nothing
- Workaround: do not move a `Buffer` a graph recorded; see the `@warning` on its move constructor
- Fix: RW-7

### KI-3 A `cancel()` that races the end of its run can cancel the next run
- Origin: own
- Where: `modules/surface/api/include/SushiRuntime/api/graph/plumbing/step_driver.hpp`
- Cause: the cancel request is not tied to the run it was made for
- Recognise: a run started right after a late cancel ends cancelled
- Workaround: wait for the run before starting the next one after a cancel
- Fix: RW-28

### KI-4 A `DynamicGraph` keeps the regions it dropped until it is destroyed
- Origin: own
- Where: `modules/surface/api/include/SushiRuntime/api/graph/dynamic_graph.hpp`
- Cause: dropped regions are freed at the next run only
- Recognise: memory held by regions a graph dropped and never ran again
- Workaround: run the graph once more, or destroy it
- Fix: RW-30

### KI-6 A distributed worker runs one kernel at a time
- Origin: own
- Where: `modules/extension/distributed/source/worker_agent.cpp`,
  `modules/extension/distributed/source/dispatch_executor.cpp`
- Cause: one exec thread calls each kernel on the queue of the device its dispatch names, outside
  the worker's scheduler, and the cache allocates its shared USM on device 0
- Recognise: two dispatches for two devices of one worker do not overlap
- Workaround: run one worker per device
- Fix: RW-17

### KI-8 `Cluster::detach()` during a run leaves the stopped coordinator installed
- Origin: own
- Where: `modules/extension/distributed/source/cluster.cpp`
- Cause: the poller holds the coordinator as a plain pointer
- Recognise: the coordinator stays until the next `detach()`, `attach()` or the end of the
  runtime
- Workaround: call `detach()` between runs
- Fix: RW-25

### KI-9 Any host that reaches the master's port can hold every peer slot
- Origin: own
- Where: `modules/extension/distributed/source/tcp_transport.cpp`
- Cause: every open connection counts towards `max_peers`, registered or not
- Recognise: registered workers are refused while unregistered connections stay open
- Workaround: keep the port behind a firewall
- Fix: RW-19

### KI-10 An op recorded on a dynamic-graph region always runs on the master
- Origin: own
- Where: `modules/engine/graph/source/task_graph.cpp`
- Cause: the dynamic composer bypasses `TaskGraph::compile`, where offloadable nodes are stamped
- Recognise: the op runs its local fallback with a cluster attached
- Workaround: record the op on a `Graph`
- Fix: RW-24

### KI-11 Frames between master and worker are not protected after the handshake
- Origin: own
- Where: `modules/extension/distributed/source/master_link.cpp`
- Cause: the handshake authenticates both ends; frames carry no encryption or signature
- Recognise: the layer is safe on a trusted LAN only, as `docs/guides/DISTRIBUTED_GUIDE.md` §2
  says
- Workaround: run the cluster on a trusted network
- Fix: RW-18

## Build and command line

### KI-13 Configuring leaves empty `CXX-DetectStdlib-*.h` files in the root
- Origin: own
- Where: `cli/sushiruntime/services/project.py`
- Cause: the CLI runs configure and compile with the source root as working directory
- Recognise: untracked `CXX-DetectStdlib-*.h` files in the root after a configure
- Workaround: delete them
- Fix: RW-56

## Documentation

### KI-14 Three CMake comments cite documents by old paths
- Origin: own
- Where: `cmake/SushiRuntimeConfig.cmake.in`, `tests/package/CMakeLists.txt`,
  `cmake/SushiRuntimeRedistributables.cmake`
- Cause: the documents moved and the comments did not
- Recognise: the cited files do not exist; the integration guide is `docs/guides/INTEGRATION.md`
- Workaround: look the document up in `docs/README.md`
- Fix: RW-51

### KI-15 The changelog lacks entries for three past changes
- Origin: own
- Where: `docs/reference/CHANGELOG.md`
- Cause: the entries were never written
- Recognise: no line for the dispatch-width work of 2026-09-21, the `sushicore` lower bound of
  0.7.0 or seven earlier deprecations
- Workaround: read `git log`
- Fix: RW-34

