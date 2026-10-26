# execution

The runtime context, the execution engine and the in-order chain that run a compiled graph.
The module is internal: its headers are for the other modules of the library and are not installed.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## chain_plan.hpp

### classify_chain

Any doubt leaves the plan on the scheduler. classify_chain() refuses the chain for native host work,
a node the installed coordinator acts on, a device value read at dispatch, a node with no USM
region, a region this context did not allocate, two devices, or a stated device that
`placement_of()` refuses, which the scheduler then reports. A chained node runs on the device
`placement_of()` gives it, so a stated device in its owner's context wins on the chain as it does
at dispatch. A plan that holds a HOST node and leaves two of its nodes unordered is not chained
either: the chain runs host bodies one after another on the calling thread, which would serialise
work the scheduler runs at the same time, such as the distributed worker's batch of dispatches over
disjoint buffers.

classify_chain() keeps the first reason it refused the chain in `ChainPlan::not_chainable`, a string
literal, and logs it once, at `DEBUG` under `sushiruntime.execution`, where the plan is classified, so a
plan on the scheduler says why. A plan that can chain has none.

With a coordinator installed, it acts on a node in two cases. The node is an offload candidate:
`OffloadState::offloadable` is set, which only the coordinator's prepare() does, when the engine
prepares the plan, for a node it has a worker and shared buffers for. Or the node runs locally and
the coordinator records what it wrote: its `OffloadState` holds a `payload` or a `live_writes`
list. Only the scheduler's poller calls `on_local_completion()`, and the chain has no poller, so a
chained node would leave the version of a shared buffer it wrote where it was.

The operation a node names does not decide. With no coordinator installed a named node is chained
like an unnamed one, and so is a node the installed coordinator keeps nothing for.

## chain_run.hpp

### ChainRun

One ChainRun lives for one run. Each submit_step() walks the plan's compiled order and submits every
enabled node with no scheduler hand-off and no polling; the queue's order stands in for the DAG's
edges. finish() is the run's single wait. With device timing on, every submission's event becomes a
NodeEventRecord in the plan's ledger, through the ledger's `record()`, with the device the chain's
queue belongs to and the profiler thread index of the submitting thread. The stage zones are
`chain.submit_step` and `chain.wait`, and `chain.record_events` is the fine zone around the read
of the events. The destructor drains the queue, so a failed run never
leaves work in flight against a plan about to be destroyed. submit_step(), wait() and finish()
return `Result<void>`: each runs its SYCL calls through `Internal::SYCL::translate_exceptions()`
of `modules/foundation/sycl` and returns what was thrown as `KernelFailed`, or as
`AllocationFailed` for refused memory. submit_step() translates one node at a time and names the
node in the error through `Internal::Scheduler::name_failure()`, as the scheduler does for a
node it dispatches.

The constructor clears the plan's `pending_events`, and the destructor of a run that never
reached finish() drains the queue with `wait_and_throw()` and logs what it held. A run that
failed therefore leaves the next run on the same plan neither its event records nor its
asynchronous errors.

A ChainRun submits straight to a device queue and never calls the engine's submit(), so it marks
itself on the engine: ExecutionEngine::run_in_progress() is true from the constructor until
finish() returns, or until the destructor has drained the queue. The mark is written
once when the run starts and once when it ends; submit_step() does not touch it.

## execution_engine.hpp

### Including the Node definition

CompiledGraph holds sushi_ptr\<Internal::Graph::Node> members and is exported from the DLL (SUSHIRUNTIME_API).
On Windows, exporting a class forces its implicit special members to be instantiated at the class
definition, which requires Node to be complete in execution_engine.hpp; a forward declaration
triggers "member access into incomplete type" under clang. Including the definition keeps the build
valid on Windows and changes nothing on platforms where the class is not exported, such as Linux.

### Including execution/runtime_settings.hpp

worker_pool() returns WorkerPool. runtime_settings.hpp is a leaf header (no SYCL, no runtime
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
commit(). commit() has no compiled in-degree baseline of its own to capture but needs this sizing,
and the plan's node table, whenever it populates or repopulates all_nodes. It is safe to call only once all_nodes carries its
final registration indices for this compose.

### ExecutionEngine::prepare

prepare() is the one call between `TaskGraph::compile()` and the first run of a plan. It takes the
plan and the rules of the placement snapshot the plan compiled against. The default does nothing.
SchedulerExecutionEngine hands `all_nodes` and those rules to the scheduler's offload gate, which
passes them to the installed coordinator's `prepare()`; with none installed no node gets an
offload state. The graph module therefore names no coordinator. A caller that compiles a
`TaskGraph` by hand and wants its nodes offered to a worker calls prepare() itself, before
`classify_chain()`, which reads the mark.

### ExecutionEngine::execute(CompiledGraph&)

execute(CompiledGraph&) does not consume the graph: the engine reads the graph's roots and sentinel
and resets and re-arms its scheduler for the round, but does not take ownership of the nodes. This
is what makes replay possible. The caller keeps one CompiledGraph and runs it many times, resetting
node state between runs. The one-shot rvalue overload delegates to this one.

It returns nothing. The run is already finished when execute() returns, so there is no event
to chain on.

### ExecutionEngine::execute(CompiledGraph&&)

execute(CompiledGraph&&) binds rvalues, as in execute(graph.compile()), and delegates to the
in-place overload. The moved-in graph is destroyed when the call returns, so this is the right entry
point for a single run.

### supports_asynchronous_run

The capability is reported and not assumed, so a strategy that has no meaningful split between
starting and finishing a run (a dry-run engine, a replay engine, a test double) is not forced to
fake one. It is false by default; callers must check before using submit() and complete().

### ExecutionEngine::submit

submit() is the half of execute() that hands work to the scheduler. It does everything execute()
does up to blocking, so the work proceeds on the worker pool while the caller does something else,
such as extracting the previous step's state for a renderer. Every submit() must be paired with
exactly one complete(); until it is, the engine rejects another run and the compiled plan must stay
alive. The graph runs in place and is not consumed. submit() returns `Result<void>`: `InvalidState`
after shutdown() and while another run holds the engine, `InvalidGraph` for a plan with no root.
A refused submit() started nothing, so no complete() follows it.

### ExecutionEngine::complete

complete() is the other half of execute(): it waits for the completion sentinel, drains the pool,
releases the engine for the next run, and returns the first failure of the run as an `Error`,
exactly as execute() would. Once the pool has drained it calls
`DeviceQueueSet::raise_asynchronous_errors()`, so an error SYCL holds for a queue's asynchronous
handler, such as an exception out of a `host_task`, comes back from this run as `KernelFailed`
and does not wait in the queue for a later one. A task's own failure wins over it.
Calling complete() without a matching submit() is a programming error.

### run_finished

run_finished() lets a caller decide whether waiting would block, which is the difference between
overlapping more work and stalling. When no run is in flight it is true and carries no meaning.

### run_in_progress

run_in_progress() answers "does this engine hold a run nobody has collected yet". A driver that
shares the engine with other graphs asks it before it starts a run or touches the engine's
cancellation state, because a pending marker kept per graph cannot see a run another graph started.
It is true inside execute(), from submit() until complete() returns, and while a ChainRun
is alive and unfinished. Between two steps of a multi-step scheduled run, each of which is one
execute(), it is false. The default is false, so engines that keep no such record and test doubles
need not implement it.

### worker_pool

worker_pool() reports the applied count and core placement, which differ from the requested
RuntimeSettings when the range did not fit the machine. The default is an empty pool, so engines
without worker threads and test doubles need not implement it.

### ExecutionEngine::reset_profiling

The default does nothing, so engines that gather no timing and test doubles need not implement it.

### ExecutionEngine::submit_observed and write_run_values

submit_observed() starts a run as submit() does and has the run's observation handed to the observers
of the context when the run's device work completes, on the thread that learns it. The default
calls submit(), so an engine with no observation hands nothing over. write_run_values() writes the
values only the engine sees into the run's period, with or without an observer; the default does
nothing.

### SchedulerExecutionEngine::execute

SchedulerExecutionEngine::execute() injects all root nodes into the persistent scheduler, re-arms the running
flag for this execution round, then blocks on the sentinel's completion promise. It does not
recreate any threads. The graph runs in place and is not consumed.

### SchedulerExecutionEngine::submit

execute() is exactly submit() followed by complete(); the two exist separately so a caller can put
its own work between them. The ExecutionEngine::submit section gives the pairing contract.

### set_rebalancer_enabled

The rebalancer migrates tasks between workers for throughput, but it is a background thread doing
work mid-run and a source of run-to-run nondeterminism. A low-jitter real-time frame or a
deterministic or replay run disables it; batch throughput leaves it on (the default).
set_rebalancer_enabled() is safe to call between runs.

### SchedulerExecutionEngine::reset_profiling

SchedulerExecutionEngine::reset_profiling() zeroes the scheduler's per-worker time buckets and resets
the plan's event ledger, sizing it to `expected_invocations` per node or the previous run's
high-water mark, whichever is larger, so capture never allocates once a shape has been seen. It
sizes the ledger's pairing slots to the number of devices. The driver calls it only when the plan
records device timing.

### collect_profiling

collect_profiling() fills the scratch `RunObservation` the plan keeps, cleared and refilled every run so an
observed run allocates nothing once its vectors have grown, from the plan: the shared node table, the id of the graph that owns the plan, the ledger's
records in node order, one `DeviceClock` per device whose pairing was read, and the drop counts of the
run. The records are not converted: a device record stays on its device's event clock and a host
record on the steady clock, and the pairings let an observer place the first on the second. When the
ledger lost any dispatch it logs one `WARN` under `sushiruntime.execution` naming each cause and its
count, and logs nothing when none was lost.

### SchedulerExecutionEngine::submit_observed and write_run_values

submit_observed() sets a flag the sentinel reads. When the sentinel runs, every record of the run is
already written, since each node completes through the poller before its successors start and the
sentinel follows every leaf. It fills an observation and hands it to the context's observers before it
signals the latch, so a caller that waits late loses nothing, and a failed run hands over nothing. A
plan that does not record timing hands over an observation with the table and no records.

write_run_values() sums the workers' busy, stealing, polling and idle time and writes the sums as the
values `worker.busy`, `worker.stealing`, `worker.polling` and `worker.idle`, then starts the buckets
again so an unobserved run does not report an earlier run's time.

## runtime_context.hpp

### RuntimeContext(config, provider, distance_model, settings)

Topology discovery and NUMA distances are supplied through injected interfaces. A null `provider`
becomes a HwlocTopologyProvider and a null `distance_model` becomes a HwlocDistanceModel, the
production defaults. Tests pass RemappingTopologyProvider and MatrixDistanceModel to exercise
multi-node logic on single-node hardware.

### settings

The scheduler reads worker count and core placement from settings() at construction, and the
execution engine reads the rebalancer choice. Routing them through the context and not through each
constructor's parameter list keeps one source of truth and stops the signatures growing a parameter
per setting. The settings are fixed at construction.

### install_offload and offload

install_offload() gives the scheduler the Internal::Graph::OffloadCoordinator that may run graph nodes
elsewhere; null removes it. The context shares ownership of the coordinator: it calls stop() on the
one still installed in shutdown() and again when the context is destroyed, and releases it in the
destructor, after the engine has shut down.
A TaskGraph built with a coordinator keeps its raw address and calls it in compile(). The
caller therefore compiles, empties with take_nodes() or destroys every such graph before it
replaces or removes that coordinator; the context cannot see those graphs and checks nothing. The
fluent layer keeps no such graph: PlanCache::build() and RegionComposer::build_region() each build
a TaskGraph, finish it and drop it inside one call.

install_offload() does not refuse a call made while a run is in flight, although
run_in_progress() could tell it. Cluster::attach() and Cluster::detach() stop the installed
coordinator before they call it, so a refusal there would leave a stopped coordinator installed;
the check belongs before that stop, in the distributed module.

offload() returns the installed pointer, or nullptr. A TaskGraph offloads only when it is
constructed with that pointer. With no coordinator installed the context is a single machine;
Distributed::Cluster::attach() is what installs one.
offload_sink() returns the sink a coordinator hands its nodes back to. A coordinator may be bound
to it and started before install_offload() runs, so one that fails to start is never installed and
the coordinator installed before it keeps working.

### set_placement_rules, install_offload and placement

The context publishes one immutable `PlacementSnapshot`: the rules in force (null for none),
whether an offload coordinator is installed, and a generation that rises by one on every
`set_placement_rules()` and every `install_offload()`, including a removal. `placement()` returns
it with one `std::atomic_load`. A compile takes it once, at its start: it resolves each node against
those rules, applies a rule's absent policy itself only when the record says no coordinator is
installed, and keeps the record, so a later change cannot free rules a compiled plan points into.
A plan whose record's generation differs from the generation `placement()` returns now is stale,
so a change that lands while a plan compiles makes the next run compile again, and a coordinator
installed after a compile does too.

A writer builds the next record from the one in force and swaps it in with
`std::atomic_compare_exchange_strong`, so a rule change and a coordinator install on two threads
never publish one generation twice and neither loses the other's field. A reader loads once per
use: `PlacementScope::device()`, a compile and a region commit each see one whole record. A mutex was the alternative; it would cost a lock and an unlock on every scope read
and on the start of every run, where the swap costs the reader one load and the run nothing.

`set_placement_rules()` returns false while a run is in flight, and the refusal is exact against a
run that starts at the same moment. It allocates the new record first, then takes the idle-change
bit of the engine's run marker with one compare-exchange that succeeds only from zero (see "The run
marker"), publishes the record and clears the bit. A run that starts while the bit is held waits for
the publish, so no run sees the swap land inside it. `install_offload()` takes no bit: its
precondition already excludes a run in flight.

`PlanCache::build` hands the coordinator the rules of the record the compile loaded, through
`ExecutionEngine::prepare()`, so the coordinator never reads a set the compile did not stamp. Each
node the coordinator marks keeps that set in its `OffloadState::rules`, so plans compiled under two
sets and run in turn each place their nodes by their own set.

### add_run_observer, remove_run_observer and observe_runs

The context keeps its observers as one immutable list behind one atomic shared pointer. Adding or
removing copies the list under a mutex and publishes the copy, so a run reads one consistent list
without a lock, and a run that is notifying while an observer is removed still holds the list it
began with. `add_run_observer()` returns an id and `remove_run_observer()` takes it; the free function
`observe_runs()` adds an observer and returns the `RunObserverHandle` that removes it, holding the
context weakly so a handle that outlives the context removes nothing. `Runtime::add_run_observer()` and
the parts object of the `profiling` module both go through it. `wants_device_timing()` is true while the
list is not empty, which is what a plan reads when it is built and when it is checked for staleness; a
plan built under the other answer is rebuilt once at its next run. `notify_run()` hands an observation
to every observer, in the order they were added. The destructor empties the list after the engine is
destroyed.

### set_machine_facts and machine_facts

`Runtime::create()` hands the context the `NodeFacts` of its machine once, after it lists the
devices; `set_machine_facts()` stops the process on a second call. `machine_facts()` returns them,
or null for a context built without a `Runtime`, and a compile and `PlacementScope::device()` read
the targets of the rules against them. A graph holds no facts of its own.

## runtime_settings.hpp

### RuntimeSettings

RuntimeSettings is separate from the hardware filter: it shapes the runtime itself (threads,
placement, background work), not which devices are selected. Every field defaults to the behaviour a
deterministic simulation wants, because that is the primary workload. A batch consumer opts out of
the parts it does not need.

The type lives in the Execution layer and not in API because RuntimeContext and TaskScheduler
consume it, and a lower layer may not depend on a higher one. RuntimeConfig is the public
spelling of this same type.

### worker_count

A runtime inside a game is not the machine's only tenant: it shares it with a main thread, a render
thread, an audio callback, asset I/O and the compositor. Taking every core means the host's own
threads are preempted by workers. Naming a smaller count, together with first_core, is how the host
reserves cores for itself.

The count is clamped to K_MAX_WORKER_THREADS.

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

## Implementation

The runtime context, the execution engine and the in-order chain path. The headers are in
`include/SushiRuntime/execution/`. This page records why the sources behave as they do; the
source comments state the invariant and point here.

### chain_run.cpp

#### The event buffer belongs to the plan

`ChainRun` reserves `pending_events` on the plan, not on itself. A fresh `ChainRun` binds to
the same plan every tick, so growing `graph_.chain.pending_events` once and reusing it keeps
later runs free of allocation. Reserving in the run would pay for a new buffer each time
this short-lived object is built.

#### submit_node_discarding

The in-order queue carries the ordering a returned event would, and the run waits once in
`wait()`, so no caller reads a per-node event on this path.

#### submit_profiled

The run's first kernel submission claims the clock pairing, as the scheduler's first device
submission does. Unlike the scheduler, this path never keeps an inline host node:
`node_kind_is_chainable` in `chain_plan.cpp` admits only a KERNEL node or a HOST node that
submits device work. An empty event here would be a rare, unexpected case, not the routine
one the scheduler guards against. The event is therefore kept unconditionally.
`record_events()` reads the timestamps through `read_dispatch_timestamps()`, across the events a body
handed over when it handed over any; an unreadable event is counted in the ledger as a dropped
dispatch.

### execution_engine.cpp

#### CompletionLatch

The latch lives once on the engine and is reset, armed and waited on each step without
allocating: the sentinel publishes the outcome under the mutex and notifies, and the caller
waits on the predicate.

A `condition_variable` is the portable primitive that allocates nothing here. C++20's
`atomic::wait` and `std::binary_semaphore` are missing from the C++17 baseline. The mutex is
uncontended in steady state, so the cost is a futex wait and not a `malloc`. The failure
that travels with the outcome is published before `done`, under the same lock, so a waiter
that observes `done` sees a fully written outcome.

`poll()` is how the asynchronous path asks whether waiting would stall it, so a caller can
choose between overlapping more work and settling up.

#### One persistent scheduler

`SchedulerExecutionEngine::Impl` owns a single `TaskScheduler`, started once in the constructor and
stopped exactly once in shutdown. It is not rebuilt per graph execution.
`RuntimeContext::Impl`'s destructor orders shutdown explicitly, so the scheduler is always
stopped before the queues it dispatches onto are torn down.

#### The rebalancer is built but off

The `WorkloadCoordinator` is always constructed, so `set_rebalancer_enabled()` can start it
later without a second construction path. Whether its heartbeat thread runs is the settings'
decision, and the default is off: the 5 ms migration heartbeat costs jitter and
reproducibility, and only a throughput workload gets something for it. `start()` and
`stop()` are idempotent on the coordinator, so toggling is safe; `rebalancer_on` records the
intent for `rebalancer_enabled()` without reaching into the coordinator's state.

#### execute is submit plus complete

`TaskGraph::compile()` builds the DAG and the root list. The engine owns thread
orchestration, the sentinel node, completion signalling and graph cleanup. Those are divided
between `submit()`, which starts, and `complete()`, which waits, and `execute()` composes
the two. Keeping it a composition, not a third copy of the sequence, means the synchronous
and the asynchronous path cannot drift apart: reset before submit and quiesce before
returning hold for both.

The scheduler is re-armed for each round through `arm_for_execution()`; it is not destroyed
and recreated. Completion flows through the one `CompletionLatch`.

#### The non-reentrancy flag

`execute()` shares one latch, one sentinel and one scheduler-arm state, so two concurrent
runs would overwrite each other. `submit()` therefore rejects a concurrent run before it
touches any shared state. It sets the `K_SCHEDULED_RUN` bit of `run_marker` with a
`fetch_or`; if the bit was already set, another run owns the engine, and `submit()` returns
`InvalidState` without clearing the bit, which that run still holds.

The bit is set in `submit()` and cleared by `complete()`, not by a guard scoped to
`submit()`, because the run outlives the function. Both functions hold a
`ScheduledRunRelease`. The one in `submit()` clears the bit only on an early exit, an
exception while wiring or submitting, so a submit that does not finish does not leave the engine
closed for good; on success it is disarmed and ownership passes to `complete()`, whose guard
clears the bit on every exit.

#### The run marker

`run_marker` is one `std::atomic<std::uint32_t>` and `run_in_progress()` is one load of it,
compared with zero once bit 1 is masked off. Bit 0 is the scheduled run. Bit 1, `K_IDLE_CHANGE`,
is held by `begin_idle_change()` for a change no run may see half done, and is no run: it is taken
only from an idle marker, and `submit()` and `begin_chained_run()` that find it in the value their
read-modify-write returned yield until `end_idle_change()` clears it. The check reads that returned
value, so a run pays one branch and no extra atomic. Every live `ChainRun` adds `K_CHAINED_RUN`,
which is 4, through `begin_chained_run()` and takes it off through `end_chained_run()`. A
count is needed because runs nest: a chained run's host node executes on the calling thread
and can start another graph's run, and a second graph can run chained while the first has an
asynchronous step pending. With a single flag the inner run's end would clear the outer
run's mark. The chained count does not block `submit()`, which tests bit 0 alone, so what a
scheduled run may overlap with is unchanged.

The marker is written when a run starts and when it ends. A scheduled run already paid one
atomic exchange and one store for the non-reentrancy flag, and the `fetch_or` and
`fetch_and` replace them one for one. A chained run adds one `fetch_add` and one `fetch_sub`
per `ChainRun`, whatever its step count. Node dispatch reads and writes nothing here. The
writes are `acq_rel` and the load is `acquire`, so a thread that sees a run begin or end
also sees what the writer did before it.

`begin_chained_run()` and `end_chained_run()` are virtual members of `ExecutionEngine` that do
nothing by default, so `ChainRun` marks whatever engine the context holds and names no engine
class. `ChainRun::RunMark` holds the mark. It is the last member, so the destructor's drain runs
before the mark is released, and a constructor that throws after it was taken releases it.

#### The log level of a run

The "Executing graph" line is TRACE, not INFO. A run happens once per replayed step, so in a
million-step `run(n)` an INFO line would emit millions of log records, each with an
`ostringstream`, a timestamp, a lock and a condition-variable notify, and that would undo the
allocation-free latch. TRACE is compiled out in release, where `K_LOG_LEVEL_FLOOR` is `Debug`.

#### The sentinel

The sentinel's `host_work` is wired once. It captures only stable pointers, the engine's
latch and scheduler, so the closure is built the first time a plan runs and reused for every
later step. A recompile produces a fresh sentinel with an empty `host_work`, which wires
again. The `profiling` flag it captures is fixed for the sentinel's lifetime. It also captures the engine
and the plan, for the observation of a run started with `submit_observed()`.

The sentinel is the final node: the DAG dispatches it only once every predecessor's event
has completed. It signals completion directly in C++, not through a SYCL host operation.
AdaptiveCpp does not run a custom-operation callback until its event is waited on, so a
sentinel built on a host task would never fire and `execute()` would block forever.

It waits on the predecessor events first, so device work is flushed before it reports done;
this also drives AdaptiveCpp's queue to completion. Each event is copied, because
AdaptiveCpp's `event::wait()` is non-const and `dependencies` is const.

The empty event it compares against is built once, outside the loop.

The sentinel reads the run outcome before it re-arms, because `arm_for_execution()` clears
the recorded failure (`RunOutcome::clear_failure()`) for the next round. A failed run still drains every node to the sentinel, so
reaching it means the run has quiesced either way.

The sentinel's own `arm_for_execution()` keeps the pool armed through the drain tail. Peers
are still finishing poll bookkeeping for the run's last nodes and must not park in the
middle of it. `complete()` disarms once that has quiesced and the next `submit()` arms
again, so the sentinel's call is not the next round's arming.

#### Order inside submit

The latch is reset, the scheduler armed, and only then are the roots submitted.

The profiling ledger is wired before arming and is null when the plan does not record device
timing.

The dispatch step is advanced in `submit()`, not inside `arm_for_execution()`. The sentinel
also re-arms, to keep the pool spinning through the drain tail, and that call must not
advance the step counter.

The roots go in through one batched injector pass: one lock acquisition and one wake burst per step.

#### complete: drain, then disarm

The sentinel signals the latch from its `host_work`, which runs on a worker thread before
that worker and its peers finish the poll bookkeeping for the run's last nodes.
`complete()` waits for the pool to drain before it returns. The caller may destroy the
`CompiledGraph` the instant `complete()` returns, and a worker still inside
`trigger_successors` or `release` would then touch freed nodes.

After that the run is over and every task has been reaped, so the pool can go back to sleep.
This is the only point where that is safe: the idle wait predicate does not look at the
per-worker deques, and quiescence is what guarantees they are empty. The sentinel re-arms for a next
round that may never come.

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

The flag is released before `complete()` returns a failure, so a failed run leaves the engine
usable. The pool survives a task that fails, and refusing every later run would turn one bad
kernel into a dead runtime.

`run_finished()` answers "has the DAG finished", not "is it safe to destroy the plan". The
pool still has bookkeeping to drain, which `complete()` waits for.

#### Profiling collection

`reset_dispatch_step()` runs once per run, before any step's `arm_for_execution()` draws
from it. The ledger's `reserve()` folds in the last run's true demand per row, so growth
happens outside the poller.

`collect_profiling()` has one capture path: the ledger's records. The names and operations of the
nodes are not read per run: the plan's node table holds them, built once.

The run's stage zones are `run.submit`, `run.wait` with the fine children `run.wait.latch`,
`run.wait.quiesce` and `run.wait.errors`, and the fine zone `run.collect` around the read of the
ledger. complete() returns the failure through `merge_run_failures()`, which keeps a task failure and
logs the queue's asynchronous error at `WARN` when both exist.

#### Definitions that need the complete Node type

`ExecutionEngine::execute(CompiledGraph&&)` and `CompiledGraph::capture_base_state()` are
defined in this file and not in the header. Constructing the local `CompiledGraph`
instantiates its destructor, `sushi_ptr<Node>::~sushi_ptr`, and touching `Node::in_degree`
does the same kind of thing: both need the complete `Node` type. The header only
forward-declares `Node` to stay light, and this translation unit is the one that sees it.

`capture_base_state()` also stamps each node's `plan_index`.

### runtime_context.cpp

#### Settings are immutable

The settings are set once at construction and never changed. The scheduler sizes and places
its worker pool from them, the execution engine reads the rebalancer choice, and the device
set is built with the profiling choice. A SYCL queue carries the profiling property from
creation and is not rebuilt to toggle it. Immutability is what makes the settings safe to
read from worker threads without a lock.

`settings` is the first member of `Impl`, so it is stored before `probe_devices()` builds the
queues and before the `SchedulerExecutionEngine` reads the worker-pool and rebalancer choices.

#### Construction order

`Impl` builds its members in declaration order: the settings, the `Internal::Topology::TopologyView`,
the `Internal::Device::DeviceQueueSet`, the `Internal::Memory::USMAllocator` (from the set's plain queues) and the
`Internal::Observability::TransferCounters`. The topology provider is asked for the core map and the
hardware nodes during that construction and is not kept.

The `SchedulerExecutionEngine` is constructed after all of them, so it receives a context
with every queue available. The scheduler it starts lives as long as the `RuntimeContext`.

#### Destruction order

The order in `~Impl` is load-bearing. `Impl::stop_work()` runs first, and
`RuntimeContext::shutdown()` calls the same function, so an explicit shutdown and the
destructor stop things in one order. It stops the installed offload coordinator while the
pool still runs: the coordinator hands the nodes it holds back to the scheduler, and after
that no late remote completion can touch a scheduler that is being torn down. Then it shuts
the `SchedulerExecutionEngine` down, which joins every worker thread. A context that was shut down
explicitly calls `stop_work()` a second time from its destructor. The seam allows a second
`OffloadCoordinator::stop()` and the engine guards its own shutdown, so `Impl` keeps no flag
for it.

The destructor then clears the allocator's release observer: a `Buffer` holds the allocator
through `shared_memory()` and can release its memory after the context is gone, and the
observer it would call is the coordinator `Impl` is about to release. The engine is destroyed
next, which removes any dangling reference to a queue or to another `Impl` member. Only then
is it safe to drain outstanding GPU work, which the `Internal::Device::DeviceQueueSet` does in its own
destructor when `Impl`'s members are destroyed, so every hardware operation has completed
before the SYCL objects it used go out of scope.

`Impl` releases its share of the stopped coordinator after the engine, not when it stops. The
scheduler keeps its address for the offload branch, the poller for the completion call, and a
worker can read it until the engine's shutdown has joined the pool.

The set's destructor logs a late asynchronous device error and continues;
`modules/platform/device/README.md` says why.

#### Queues

FPGAs and custom accelerators often execute better in order or do not support out-of-order
queues natively, so a device that is neither a CPU nor a GPU gets an in-order queue.
A profiling queue is built lazily, on the first run of a plan that records device timing, so a
`RunObserver` can read each kernel's device start and end timestamps. The two properties compose, and
the FPGA case keeps its in-order guarantee.

The queues themselves, their lazy build and the NUMA rotation live in
`Internal::Device::DeviceQueueSet`; `modules/platform/device/README.md` describes them. The context
keeps no queue of its own. `devices().queue(i, {false, true})` asks the set for the flavour
`{in_order = false, profiling = true}` and `devices().queue(i, {true, profiling})` for
`{in_order = true, profiling}`. The context builds the set with plain queues only.

`describe_device()` turns each `Internal::Topology::HardwareNode` into a `Internal::Device::DeviceDescriptor`, so
the device module includes no topology header. A node whose `is_gpu` flag is set becomes
`DeviceKind::GPU` whatever SYCL reports, which keeps `devices().is_gpu()` answering from the flag
discovery recorded. Descriptors are built by walking the node list in order, one per node
with no skips, so a node and its queue share an index.

#### No device at all

An empty device list is reported in `probe_devices()`, where the filter that caused it is
still in hand. Anything later asks for queue 0 and fails with a bad-index error.
That message is true, but it names neither the machine's state nor the filter, and it is the
first thing a player on a machine without a GPU would see. `RuntimeContext::create()` returns
it as `Unsupported`.

`RuntimeContext` has no public constructor. `create()` asks the provider for the devices, builds
the parts and then the engine, all inside one call of `Internal::SYCL::translate_exceptions()`:
a `sycl::exception` from a queue build is `DeviceLost`, any other exception is
`AllocationFailed`, and an empty or failed enumeration is `Unsupported`. The private constructor
takes the finished parts and cannot fail.

#### Transfer counters

`transfers`, an `Internal::Observability::TransferCounters`, counts only the explicit transfers the
runtime starts itself: the selective readback in `Buffer::read_range` and `write_range`, and
the distributed path. `RunReport` uses them to say how much the host pulled from and pushed
to the device during a run. Implicit shared-USM page migration by the driver is invisible to
SYCL and is not counted. The atomics are relaxed because the values are
monotonic counters read at a run boundary, with no ordering dependency on other state.

#### USM ownership

`Internal::Memory::USMAllocator` maps a USM pointer to the index of its owning queue. Each allocation
made by `RuntimeContext::memory().allocate()` is recorded, so the scheduler can run a task on
the exact queue and context its data was allocated on. The record is the source of truth;
pointer introspection stays only as a best-effort fallback.

Introspection is unreliable for picking an owner. On a single platform, CUDA for example, a
shared or device USM pointer is valid in sibling contexts too, but only the
owning context reports it as exactly the kind it created: device USM reports `device` in its
own context and shared USM reports `shared`. A context that does not own the pointer
typically reports a different kind. CUDA shared USM was observed showing up as `device` in
the CPU context. `memory().owning_device()` therefore never trusts the type alone. For USM that
was not allocated through this context, such as a test calling bare `sycl::malloc_shared`,
it prefers a queue that both reports device-or-shared and whose own device equals
`get_pointer_device()`; that pair identifies the allocating queue, and a `shared` report
settles it at once. Host USM, a null pointer, or anything that cannot be pinned takes the
NUMA-local round-robin queue.

`memory().release()` resolves and forgets the owner
in one `take()`. Two separate locks would let a second free of the same pointer read an
owner the first is already acting on. The free goes against the owning queue's context,
which correctness requires; without a record it falls back to queue 0 as a best effort and
logs a WARN. An address recorded through `memory().adopt()` is forgotten and not freed.

The allocator tells a `Internal::Memory::USMReleaseObserver` about a release before the record is
dropped and the memory freed. The context installs none; `Distributed::Cluster::attach()`
installs its coordinator there, and `~Impl` clears whatever is installed.

`memory().containing_base()` resolves an interior pointer to the base `owner_of()` knows, for
callers validating a pointer a user handed them. It is linear in live allocations, which
makes it a registration-time query and not a dispatch-time one.

#### Offload coordinator

`install_offload()` hands a `Internal::Graph::OffloadCoordinator` to the engine, which forwards it to
the scheduler's `set_offload_coordinator()`, and keeps the `shared_ptr` whose address
`offload()` returns. The context shares ownership so that the coordinator outlives the
engine's shutdown whatever its installer does with its own reference. Installing another
coordinator, or null, releases the share after the engine holds the new address. The context
never names a concrete coordinator: `modules/extension/distributed/README.md` describes the one the
distributed layer installs. A plan offloads only when the engine prepared it while that
coordinator was installed: `ExecutionEngine::prepare()`, which the fluent `Graph` calls on each
plan it compiles.

## Designs

<!-- designs:begin -->
- [GRAPH_REPLAY](../../../docs/design/GRAPH_REPLAY/README.md): SushiEngine's matter device solver is launch-bound on the device.
<!-- designs:end -->
