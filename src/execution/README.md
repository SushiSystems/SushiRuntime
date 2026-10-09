# execution

The runtime context, the execution engine and the in-order chain path. The headers are in
`include/SushiRuntime/execution/`. This page records why the sources behave as they do; the
source comments state the invariant and point here.

## chain_run.cpp

### The event buffer belongs to the plan

`ChainRun` reserves `pending_events` on the plan, not on itself. A fresh `ChainRun` binds to
the same plan every tick, so growing `graph_.chain.pending_events` once and reusing it keeps
later runs free of allocation. Reserving in the run would pay for a new buffer each time
this short-lived object is built.

### submit_node_discarding

The in-order queue carries the ordering a returned event would, and the run waits once in
`wait()`, so no caller reads a per-node event on this path.

### submit_profiled

The run's first kernel submission claims the clock pairing, as the scheduler's first device
submission does. Unlike the scheduler, this path never keeps an inline host node:
`node_kind_is_chainable` in `chain_plan.cpp` admits only a KERNEL node or a HOST node that
submits device work. An empty event here would be a rare, unexpected case, not the routine
one the scheduler guards against. The event is therefore kept unconditionally.
`record_events()` calls `read_event_timestamps()`, which already drops an empty or
unreadable event, so no check per submission is needed.

## execution_engine.cpp

### CompletionLatch

`execute()` once allocated a fresh `std::promise`/`std::future` pair, and a capturing
sentinel closure, on every step of a replayed run. Over a million-step run that was millions
of heap allocations and a thread hand-off per step, which is jitter on a real-time frame.
The latch lives once on the engine and is reset, armed and waited on each step without
allocating: the sentinel publishes the outcome under the mutex and notifies, and the caller
waits on the predicate.

A `condition_variable` is the portable primitive that allocates nothing here. C++20's
`atomic::wait` and `binary_semaphore` are missing from the C++17 baseline. The mutex is
uncontended in steady state, so the cost is a futex wait and not a `malloc`. The failure
that travels with the outcome is published before `done`, under the same lock, so a waiter
that observes `done` sees a fully written outcome.

`poll()` is how the asynchronous path asks whether waiting would stall it, so a caller can
choose between overlapping more work and settling up.

### One persistent scheduler

`ExecutionEngine::Impl` owns a single `TaskScheduler`, started once in the constructor and
stopped exactly once in shutdown. It is not rebuilt per graph execution.
`RuntimeContext::Impl`'s destructor orders shutdown explicitly, so the scheduler is always
stopped before the queues it dispatches onto are torn down.

### The rebalancer is built but off

The `WorkloadCoordinator` is always constructed, so `set_rebalancer_enabled()` can start it
later without a second construction path. Whether its heartbeat thread runs is the settings'
decision, and the default is off: the 5 ms migration heartbeat costs jitter and
reproducibility, and only a throughput workload gets something for it. `start()` and
`stop()` are idempotent on the coordinator, so toggling is safe; `rebalancer_on` records the
intent for `rebalancer_enabled()` without reaching into the coordinator's state.

### execute is submit plus complete

`TaskGraph::compile()` builds the DAG and the root list. The engine owns thread
orchestration, the sentinel node, completion signalling and graph cleanup. Those are divided
between `submit()`, which starts, and `complete()`, which waits, and `execute()` composes
the two. Keeping it a composition, not a third copy of the sequence, means the synchronous
and the asynchronous path cannot drift apart: reset before submit and quiesce before
returning hold for both.

The scheduler is re-armed for each round through `arm_for_execution()`; it is not destroyed
and recreated. Completion flows through the one `CompletionLatch`.

### The non-reentrancy flag

`execute()` shares one latch, one sentinel and one scheduler-arm state, so two concurrent
runs would overwrite each other. `submit()` therefore rejects a concurrent run before it
touches any shared state. If the exchange on `run_in_progress` returns true, another run
owns the engine, and `submit()` throws without releasing the flag, which that run still
holds.

The flag is taken in `submit()` and released by `complete()`, not by an RAII guard scoped to
`submit()`, because the run outlives the function. `SubmitGuard` releases the flag only on
an early exit, an exception while wiring or submitting, so a throwing submit does not leave
the engine closed for good. On success the guard is disarmed and ownership passes to
`complete()`.

### The log level of a run

The "Executing graph" line is DEBUG, not INFO. A run happens once per replayed step, so in a
million-step `run(n)` an INFO line would emit millions of log records, each with an
`ostringstream`, a timestamp, a global mutex and a condition-variable notify, and that would
undo the allocation-free latch. DEBUG is compiled out in release, where `ACTIVE_LOG_LEVEL`
is INFO.

### The sentinel

The sentinel's `host_work` is wired once. It captures only stable pointers, the engine's
latch and scheduler, so the closure is built the first time a plan runs and reused for every
later step. A recompile produces a fresh sentinel with an empty `host_work`, which wires
again. The `profiling` flag it captures is fixed for the sentinel's lifetime.

The sentinel is the final node: the DAG dispatches it only once every predecessor's event
has completed. It signals completion directly in C++, not through a SYCL host operation.
AdaptiveCpp does not run a custom-operation callback until its event is waited on, so a
sentinel built on a host task would never fire and `execute()` would block forever.

It waits on the predecessor events first, so device work is flushed before it reports done;
this also drives AdaptiveCpp's queue to completion. Each event is copied, because
AdaptiveCpp's `event::wait()` is non-const and `deps` is const.

The empty event it compares against is built once, outside the loop. `sycl::event{}`
allocates on oneAPI's libsycl, so constructing it inside the loop cost one allocation per
predecessor per run. That was the same defect `execute()` had, on a worker thread where the
steady-state counter could not see it.

The sentinel reads the run outcome before it re-arms, because `arm_for_execution()` clears
`run_failed_` for the next round. A failed run still drains every node to the sentinel, so
reaching it means the run has quiesced either way.

The sentinel's own `arm_for_execution()` keeps the pool armed through the drain tail. Peers
are still finishing poll bookkeeping for the run's last nodes and must not park in the
middle of it. `complete()` disarms once that has quiesced and the next `submit()` arms
again, so the sentinel's call is not the next round's arming.

### Order inside submit

The latch is reset, the scheduler armed, and only then are the roots submitted. The reset
must come before submission so that a waiter cannot observe the previous step's completion.
The caller's wait returned only after the previous sentinel signalled, that is after that
step had drained, so the reset races nothing.

The profiling ledger is wired before arming and is null when the plan does not request
profiling.

The dispatch step is advanced in `submit()`, not inside `arm_for_execution()`. The sentinel
also re-arms, to keep the pool spinning through the drain tail, and that call must not
advance the step counter.

The roots go in through one batched injector pass, where there used to be one `submit()`,
with its lock and notify, per root. The per-step cost of injecting roots no longer grows
with fan-out.

### complete: drain, then disarm

The sentinel signals the latch from its `host_work`, which runs on a worker thread before
that worker and its peers finish the poll bookkeeping for the run's last nodes.
`complete()` waits for the pool to drain before it returns. The caller may destroy the
`CompiledGraph` the instant `complete()` returns, and a worker still inside
`trigger_successors` or `release` would then touch freed nodes.

After that the run is over and every task has been reaped, so the pool can go back to sleep.
This is the only point where that is safe: the idle wait predicate does not look at the
per-worker deques, and quiescence is what guarantees they are empty. Disarming here is what
makes an idle runtime free. The sentinel re-arms for a next round that may never come, and
before this call existed every worker kept spinning through the steal backoff for the rest
of the context's life.

The submission guard is still held at that point, so no other thread can start a run between
the disarm and the guard's release, and the next `submit()` arms again before it injects
anything. That exclusion is why the disarm lives in `complete()` and not once per
user-visible run. Parking from anywhere the engine does not hold the guard could disarm a
pool another thread had just armed, and a worker holding queued tasks with no active events
would then park on them.

A multi-step run disarms and re-arms per step. That costs almost nothing in practice: the
next arm follows within microseconds, and a worker parks only if it reaches the top of its
loop inside that window. Workers that do park are woken by the next arm's notify. A tight
batch keeps running and an idle runtime goes to sleep, with no timer and no threshold to
choose.

The flag is released before a task exception is rethrown, so a failed run leaves the engine
usable. The pool survives a task throwing, and refusing every later run would turn one bad
kernel into a dead runtime.

`run_finished()` answers "has the DAG finished", not "is it safe to destroy the plan". The
pool still has bookkeeping to drain, which `complete()` waits for.

### Profiling collection

`reset_dispatch_step()` runs once per run, before any step's `arm_for_execution()` draws
from it. The ledger's `reserve()` folds in the last run's true demand per row, so growth
happens outside the poller.

`collect_profiling()` has one capture path, labelled F4 in the comments it replaces:
`node_timings` is aggregated from the same event records the ledger collected, one row per
`registration_index` that was dispatched, indexed by that index directly and not through a
hashed lookup. Completion polling is the scheduler's own overhead between kernels, and it is
reported in `scheduling_overhead_ms` so that a frame can be tuned against it.

### Definitions that need the complete Node type

`IExecutionEngine::execute(CompiledGraph&&)` and `CompiledGraph::capture_base_state()` are
defined in this file and not in the header. Constructing the local `CompiledGraph`
instantiates its destructor, `sushi_ptr<Node>::~sushi_ptr`, and touching `Node::in_degree`
does the same kind of thing: both need the complete `Node` type. The header only
forward-declares `Node` to stay light, and this translation unit is the one that sees it.

`capture_base_state()` also stamps each node's `plan_index`, because it is the one pass that
already walks every node once after compile.

## runtime_context.cpp

### Settings are immutable

The settings are set once at construction and never changed. The scheduler sizes and places
its worker pool from them, the execution engine reads the rebalancer choice, and the device
set is built with the profiling choice. A SYCL queue carries the profiling property from
creation and is not rebuilt to toggle it. Immutability is what makes the settings safe to
read from worker threads without a lock.

`settings` is the first member of `Impl`, so it is stored before `probe_devices()` builds the
queues and before the `ExecutionEngine` reads the worker-pool and rebalancer choices.

### Construction order

`Impl` builds its members in declaration order: the settings, the `Topology::TopologyView`,
the `Device::DeviceQueueSet`, the `Memory::UsmAllocator` (from the set's plain queues) and the
`Observability::TransferCounters`. The topology provider is asked for the core map and the
hardware nodes during that construction and is not kept.

The `ExecutionEngine` is constructed after all of them, so it receives a context
with every queue available. The scheduler it starts lives as long as the `RuntimeContext`.

### Destruction order

The order in `~Impl` is load-bearing. The installed offload coordinator stops first, so no
late remote completion can touch the scheduler while or after it is torn down. The
allocator's release observer is cleared next: a `Buffer` holds the allocator through
`shared_memory()` and can release its memory after the context is gone, and the observer it
would call is the coordinator `Impl` is about to release. The `ExecutionEngine` goes after
that; its
shutdown joins every worker thread and removes any dangling reference to a queue or to
another `Impl` member. Only then is it safe to drain outstanding GPU work, which the
`Device::DeviceQueueSet` does in its own destructor when `Impl`'s members are destroyed, so
every hardware operation has completed before the SYCL objects it used go out of scope.

`Impl` releases its share of the stopped coordinator after the engine, not when it stops. The
scheduler keeps its address for the offload branch, the poller for the completion call, and a
worker can read it until the engine's shutdown has joined the pool.

The set's destructor logs a late asynchronous device error and continues;
`include/SushiRuntime/device/README.md` says why.

### Queues

FPGAs and custom accelerators often execute better in order or do not support out-of-order
queues natively, so a device that is neither a CPU nor a GPU gets an in-order queue.
Profiling is added on top when the context was built for it, so `RunReport` can read each
kernel's device start and end timestamps. The two properties compose, and the FPGA case
keeps its in-order guarantee.

The queues themselves, their lazy build and the NUMA rotation live in
`Device::DeviceQueueSet`; `include/SushiRuntime/device/README.md` describes them. The context
keeps no queue of its own. `profiling_queue(i)` asks the set for the flavour
`{in_order = false, profiling = true}` and `chain_queue(i, profiling)` for
`{in_order = true, profiling}`.

`describe_device()` turns each `Topology::HardwareNode` into a `Device::DeviceDescriptor`, so
the device module includes no topology header. A node whose `is_gpu` flag is set becomes
`DeviceKind::Gpu` whatever SYCL reports, which keeps `device_is_gpu()` answering from the flag
discovery recorded. Descriptors are built by walking the node list in order, one per node
with no skips, so a node and its queue share an index.

### No device at all

An empty device list is reported in `probe_devices()`, where the filter that caused it is
still in hand. Anything later asks for queue 0 and fails with a bad-index error.
That message is true, but it names neither the machine's state nor the filter, and it is the
first thing a player on a machine without a GPU would see. The comment this page replaces
adds that "BB-5b's try_create() turns this into a returned Error rather than a throw".

### Transfer counters

`transfers`, an `Observability::TransferCounters`, counts only the explicit transfers the
runtime starts itself: the selective readback in `Buffer::read_range` and `write_range`, and
the distributed path. `RunReport` uses them to say how much the host pulled from and pushed
to the device during a run. Implicit shared-USM page migration by the driver is invisible to
SYCL and is deliberately not counted. The atomics are relaxed because the values are
monotonic counters read at a run boundary, with no ordering dependency on other state.

### USM ownership

`Memory::UsmAllocator` maps a USM pointer to the index of its owning queue. Every
`RuntimeContext::malloc_*` forwards to its `allocate()`, so the scheduler can run a task on
the exact queue and context its data was allocated on. The record is the source of truth;
pointer introspection stays only as a best-effort fallback.

Introspection is unreliable for picking an owner. On a single platform, CUDA for example, a
`malloc_shared` or `malloc_device` pointer is valid in sibling contexts too, but only the
owning context reports it as exactly the kind it created: device USM reports `device` in its
own context and shared USM reports `shared`. A context that does not own the pointer
typically reports a different kind. CUDA shared USM was observed showing up as `device` in
the CPU context. `get_queue_for_ptr()` therefore never trusts the type alone. For USM that
was not allocated through this context, such as a test calling bare `sycl::malloc_shared`,
it prefers a queue that both reports device-or-shared and whose own device equals
`get_pointer_device()`; that pair identifies the allocating queue, and a `shared` report
settles it at once. Host USM, a null pointer, or anything that cannot be pinned takes the
NUMA-local round-robin queue.

`free_usm()` forwards to the allocator's `release()`, which resolves and forgets the owner
in one `take()`. Two separate locks would let a second free of the same pointer read an
owner the first is already acting on. The free goes against the owning queue's context,
which correctness requires; without a record it falls back to queue 0 as a best effort and
logs a WARN. An address recorded through `adopt_usm()` is forgotten and not freed.

The allocator tells a `Memory::UsmReleaseObserver` about a release before the record is
dropped and the memory freed. The context installs none; `Distributed::Cluster::attach()`
installs its coordinator there, and `~Impl` clears whatever is installed.

`containing_usm_base()` resolves an interior pointer to the base `owner_of()` knows, for
callers validating a pointer a user handed them. It is linear in live allocations, which
makes it a registration-time query and not a dispatch-time one.

### Offload coordinator

`install_offload()` hands a `Graph::OffloadCoordinator` to the engine, which forwards it to
the scheduler's `set_offload_coordinator()`, and keeps the `shared_ptr` whose address
`offload()` returns. The context shares ownership so that the coordinator outlives the
engine's shutdown whatever its installer does with its own reference. Installing another
coordinator, or null, releases the share after the engine holds the new address. The context
never names a concrete coordinator: `src/distributed/README.md` describes the one the
distributed layer installs. A graph offloads only when it was built with the installed
pointer: `TaskGraph(ctx.offload())`, which the fluent `Graph` and `DynamicGraph` pass
themselves.
