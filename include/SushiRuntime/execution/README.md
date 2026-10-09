# execution

The runtime context, the execution engine and the in-order chain that run a compiled graph.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## chain_plan.hpp

### classify_chain

Any doubt leaves the plan on the scheduler. classify_chain() refuses the chain for native host work,
an offload candidate, a device value read at dispatch, a node with no USM region, a region this
context did not allocate, or two devices.

## chain_run.hpp

### ChainRun

One ChainRun lives for one run. Each submit_step() walks the plan's compiled order and submits every
enabled node with no scheduler hand-off and no polling; the queue's order stands in for the DAG's
edges. finish() is the run's single wait. With profiling on, every submission's event becomes a
NodeEventRecord in the plan's ledger. The destructor drains the queue, so a throwing run never
leaves work in flight against a plan about to be destroyed.

## execution_engine.hpp

### Including graph/node.hpp

CompiledGraph holds sushi_ptr\<Graph::Node> members and is exported from the DLL (SUSHIRUNTIME_API).
On Windows, exporting a class forces its implicit special members to be instantiated at the class
definition, which requires Node to be complete in execution_engine.hpp; a forward declaration
triggers "member access into incomplete type" under clang. Including the definition keeps the build
valid on Windows and changes nothing on platforms where the class is not exported, such as Linux.

### Including execution/runtime_settings.hpp

worker_pool() returns WorkerPoolInfo. runtime_settings.hpp is a leaf header (no SYCL, no runtime
types), so including it in execution_engine.hpp does not create a cycle with runtime_context.hpp,
which includes execution_engine.hpp.

### capture_base_state

execute() consumes a node's in_degree as it releases successors, so a second run needs the original
counts restored first. The caller invokes capture_base_state() once after compile(); rearm() then
restores from the snapshot before every subsequent run. Keeping the snapshot in CompiledGraph, next
to Node, and not in the fluent API is what lets the public API stop reaching into Node's
scheduler-internal fields.

### capture_profiling_state

capture_profiling_state() is shared by capture_base_state() (the static Graph) and DynamicGraph's
commit(). commit() has no compiled in-degree baseline of its own to capture but needs this sizing
whenever it populates or repopulates all_nodes. It is safe to call only once all_nodes carries its
final registration indices for this compose.

### IExecutionEngine::execute(CompiledGraph&)

execute(CompiledGraph&) does not consume the graph: the engine reads the graph's roots and sentinel
and resets and re-arms its scheduler for the round, but does not take ownership of the nodes. This
is what makes replay possible. The caller keeps one CompiledGraph and runs it many times, resetting
node state between runs. The one-shot rvalue overload delegates to this one.

It returns nothing. It used to return a default-constructed sycl::event that carried no information:
the run is already finished when execute() returns, so there was never a hardware event to chain on,
and the documentation said so. Constructing one is not free. On oneAPI's libsycl sycl::event{}
heap-allocates, which put one allocation per tick on the steady-state loop that run(RunReport&)
promises will not allocate.

### IExecutionEngine::execute(CompiledGraph&&)

execute(CompiledGraph&&) binds rvalues, as in execute(graph.compile()), and delegates to the
in-place overload. The moved-in graph is destroyed when the call returns, so this is the right entry
point for a single run.

### supports_asynchronous_run

The capability is reported and not assumed, so a strategy that has no meaningful split between
starting and finishing a run (a dry-run engine, a replay engine, a test double) is not forced to
fake one. It is false by default; callers must check before using submit() and complete().

### IExecutionEngine::submit

submit() is the half of execute() that hands work to the scheduler. It does everything execute()
does up to blocking, so the work proceeds on the worker pool while the caller does something else,
such as extracting the previous step's state for a renderer. Every submit() must be paired with
exactly one complete(); until it is, the engine rejects another run and the compiled plan must stay
alive. The graph runs in place and is not consumed.

### IExecutionEngine::complete

complete() is the other half of execute(): it waits for the completion sentinel, drains the pool,
releases the engine for the next run, and rethrows a task exception exactly as execute() would.
Calling it without a matching submit() is a programming error.

### run_finished

run_finished() lets a caller decide whether waiting would block, which is the difference between
overlapping more work and stalling. When no run is in flight it is true and carries no meaning.

### worker_pool

worker_pool() reports the applied count and core placement, which differ from the requested
RuntimeSettings when the range did not fit the machine. The default is an empty pool, so engines
without worker threads and test doubles need not implement it.

### IExecutionEngine::reset_profiling

The default does nothing, so engines that gather no timing and test doubles need not implement it.

### ExecutionEngine::execute

ExecutionEngine::execute() injects all root nodes into the persistent scheduler, re-arms the running
flag for this execution round, then blocks on the sentinel's completion promise. It does not
recreate any threads. The graph runs in place and is not consumed.

### ExecutionEngine::submit

execute() is exactly submit() followed by complete(); the two exist separately so a caller can put
its own work between them. The IExecutionEngine::submit section gives the pairing contract.

### set_rebalancer_enabled

The rebalancer migrates tasks between workers for throughput, but it is a background thread doing
work mid-run and a source of run-to-run nondeterminism. A low-jitter real-time frame or a
deterministic or replay run disables it; batch throughput leaves it on (the default).
set_rebalancer_enabled() is safe to call between runs.

### ExecutionEngine::reset_profiling

ExecutionEngine::reset_profiling() zeroes every node's per-run timing in `graph` and the scheduler's
per-worker time buckets, so the resulting report covers only the run about to start. The driver
calls it only when profiling is on.

It also resets the plan's event ledger, sizing it to `expected_invocations` per node or the previous
run's high-water mark, whichever is larger, so capture never allocates once a shape has been seen.

### collect_profiling

collect_profiling() reads each node's accumulated device and host time and invocation count into
`report.node_timings`, skipping nodes that never ran, and the per-worker time split into
`report.worker_timings`. It derives scheduling_overhead_ms from the workers' completion-polling
time.

## runtime_context.hpp

### RuntimeContext(config, provider, distance_model, settings)

Topology discovery and NUMA distances are supplied through injected interfaces. A null `provider`
becomes a HwlocTopologyProvider and a null `distance_model` becomes a HwlocDistanceModel, the
production defaults. Tests pass RemappingTopologyProvider and MatrixDistanceModel to exercise
multi-node logic on single-node hardware.

### profiling_enabled

profiling_enabled() is true if device kernel timing is available. It lets the run driver skip all
profiling collection when the context was created without it, keeping the no-profiling path free of
timing reads. The value is fixed at construction.

### settings

The scheduler reads worker count and core placement from settings() at construction, and the
execution engine reads the rebalancer choice. Routing them through the context and not through each
constructor's parameter list keeps one source of truth and stops the signatures growing a parameter
per setting. The settings are fixed at construction.

### add_device_to_host_bytes

The selective-readback path (Buffer::read_range) calls add_device_to_host_bytes() so a run report
can attribute host-pull traffic. It counts only runtime-issued copies, never the driver's implicit
USM migration.

### profiling_queue

The profiling queue shares the plain queue's sycl::context (and its in-order property, when the
device needs one) but carries enable_profiling, so events it submits expose command_submit,
command_start and command_end. It is built on the first call for that device, under a per-device
once_flag, unless RuntimeSettings::profiling built it at construction. The plain queue returned by
get_queue() is unchanged by this call. When RuntimeSettings::profiling is true the plain queue
already carries enable_profiling, so profiling_queue() returns that same queue instead of a second
one. The queues live in Device::DeviceQueueSet; `include/SushiRuntime/device/README.md` has the
slot each call maps to.

### has_profiling_queue

has_profiling_queue() is true only after profiling_queue() or chain_queue(index, true) has been
called for `xpu_index`, or after RuntimeSettings::profiling built the queue eagerly. It exists so a
caller can observe the lazy-build contract (no profiling queue before the first request) without
triggering the build itself by calling profiling_queue().

### chain_queue

The chain queue shares the plain queue's sycl::context, so USM allocated through this context is
valid on it. It is built on the first call for that device and flavour, under a per-device
once_flag, and owned for the context's lifetime beside the plain and profiling queues. On a device
whose plain queue is already in-order, a device that is neither a CPU nor a GPU, chain_queue()
returns the plain queue or the profiling queue itself and builds no second one.

### device_count

device_count() reflects SushiRuntime's own device set after deduplication (one CPU per NUMA node,
GPUs once each), which can differ from sycl::device::get_devices(). Valid indices are [0, count).

### device_is_gpu

device_is_gpu() answers from the inventory built at construction, not by asking SYCL again.
The device class of a queue cannot change while the context is alive, so re-deriving it per call is
redundant work. It is not free work either: `queue::get_device()` returns a device by value, and the
SYCL runtime, not SushiRuntime, defines what that costs. A caller on a steady-state tick path cannot
hold a zero-allocation contract across that difference, which is exactly what run_report_builder was
doing.

### get_queue_for_ptr

USM is pinned to the device and context it was allocated on, so a kernel touching it must run on a
queue sharing that context; running it elsewhere (CPU USM on a CUDA queue, for example) is an
illegal cross-context access. get_queue_for_ptr() returns the owning queue of a USM pointer the task
reads or writes. If `ptr` is null or not found in any queue's context, it falls back to
get_numa_local_queue(`fallback_numa`), which preserves round-robin for pointer-less tasks.

### context_index_for_ptr

context_index_for_ptr() is the integer counterpart of get_queue_for_ptr(). It lets the scheduler
tell whether two of a node's pointers live on the same context (equal indices) or straddle a device
boundary, without comparing queue objects. The index is the one get_queue() and malloc_shared_on()
use, so the scheduler can re-allocate a staging copy on the owning device.

### malloc_shared

malloc_shared() is the better choice over a bare sycl::malloc_shared when graph tasks will touch the
data: the runtime remembers which queue (device and context) the allocation belongs to, so the
scheduler can dispatch the task to that exact queue and never trigger a cross-context illegal
access. The allocation goes to the lowest-indexed device on `numa_id`, or to device 0 when that
node has no device, so the same `numa_id` always names the same device.

### malloc_shared_on

Unlike malloc_shared(bytes, numa_id), which resolves a NUMA node to a device,
malloc_shared_on() allocates on exactly `device_index` and records that owner. It is for buffers
that must share one context, because a kernel touching USM from two different device contexts is an
illegal cross-context access. The fluent Buffer and State handles allocate through it so a graph's
data stays co-located.

### malloc_device_on

malloc_device_on() is the device-resident counterpart of malloc_shared_on(): the allocation stays on
`device_index` and is never host-addressable, so a field kept resident across frames does not
migrate to the host on access. The owner is recorded exactly as for shared USM, so the scheduler
routes kernels to the owning queue and selective host readback copies through it. It is for
device-resident buffers that must share one context, such as a State's two fields.

### malloc_host_on

Kernels on `device_index` read and write the allocation, and the host reads it at any time. The
owner is recorded as for malloc_shared_on.

### aligned_malloc_shared_on

aligned_malloc_shared_on() is malloc_shared_on() with a stated alignment: the allocation starts on a
multiple of `alignment` bytes, the owner is recorded the same way, and free_usm() releases it. It is
for buffers a consumer reads in vector-width or cache-line steps, such as tensor storage.

### aligned_malloc_device_on

aligned_malloc_device_on() is the device-resident counterpart, as malloc_device_on() is of
malloc_shared_on(). Whether a backend honours an alignment above its own default for device USM is
the backend's to answer; the call returns nullptr when it cannot.

### containing_usm_base

containing_usm_base() lets a caller holding an interior pointer name the allocation it belongs to,
which get_queue_for_ptr() and sycl::get_pointer_type() cannot: both answer on exact base addresses.
The search is linear in live allocations, so it belongs in graph building, never in dispatch.

### enable_distributed_master

enable_distributed_master() creates the buffer registry and the coordinator, installs the
coordinator with install_offload(), and starts the transport. Until it is called the context
behaves exactly as Standalone.

### install_offload and offload

install_offload() gives the scheduler the Graph::OffloadCoordinator that may run graph nodes
elsewhere; null removes it. The context does not own the coordinator and calls its stop() when the
context is destroyed. offload() returns the installed pointer, or nullptr. A TaskGraph offloads only
when it is constructed with that pointer.

## runtime_settings.hpp

### PinPolicy

Pinning is what lets a host application reserve cores: bound workers cannot drift onto the core the
render or audio thread is using. Float gives that up in exchange for letting the OS scheduler move a
worker off a core that a higher-priority thread has taken over. That is the better trade on an
oversubscribed machine or inside a container whose CPU allowance is enforced by quota and not by
affinity.

### RuntimeSettings

RuntimeSettings is separate from the hardware filter: it shapes the runtime itself (threads,
placement, background work), not which devices are selected. Every field defaults to the behaviour a
deterministic simulation wants, because that is the primary workload. A batch consumer opts out of
the parts it does not need.

The type lives in the Execution layer and not in API because RuntimeContext and TaskScheduler
consume it, and a lower layer may not depend on a higher one. API::RuntimeConfig is the public
spelling of this same type.

### worker_count

A runtime inside a game is not the machine's only tenant: it shares it with a main thread, a render
thread, an audio callback, asset I/O and the compositor. Taking every core means the host's own
threads are preempted by workers. Naming a smaller count, together with first_core, is how the host
reserves cores for itself.

The count is clamped to Core::MAX_WORKER_THREADS.

### first_core

Worker i binds to core (first_core + i), so a host that keeps its own threads on cores 0..3 sets
first_core = 4. The field is ignored under PinPolicy::Float. If the range would run past the
machine's last core the runtime clamps it and logs what it did, instead of pinning to core indices
the OS will silently ignore.

### rebalancer

The rebalancer is off by default. It migrates tasks between workers on a 5 ms heartbeat, which
trades run-to-run reproducibility and a low jitter floor for throughput on long batch work. A
simulation frame wants the opposite, and the runtime's primary consumer is a simulation. Throughput
workloads turn it on explicitly.

### WorkerPoolInfo

RuntimeSettings is what the caller asked for; WorkerPoolInfo is what the runtime could give. They
diverge when the requested core range does not fit the machine, or when the count exceeds
Core::MAX_WORKER_THREADS. A host that reserved cores should verify against WorkerPoolInfo and not
against its own request, because a reservation is only worth something if it actually happened.
