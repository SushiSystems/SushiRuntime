# api/runtime

The Runtime entry point, its configuration and its read-only view.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## Lifetime

One rule covers every handle. `Runtime`, `Graph`, `DynamicGraph`, `Buffer`, `State`, `Interop` and
`AdvancedControls` each hold a `shared_ptr` to the same `Internal::Execution::RuntimeContext`, and the context
is destroyed when the last of them is. A `Runtime` may therefore be destroyed before the graphs,
buffers and views it handed out; they keep working, and the worker threads and device queues stop
when the last handle goes.

`Runtime` itself has no destructor logic and owns nothing but its share. This page states no rule
between two handles, such as a graph and the buffers recorded in it; `api/graph/README.md` and
`api/data/README.md` hold those.

## runtime_config.hpp

### RuntimeConfig

`RuntimeConfig` is the one argument of `Runtime::create()`. It holds the device filter (`devices`)
and the co-tenancy contract: worker count, core placement, pinning policy, the background
rebalancer, the forced scheduler path, `force_chain`, which keeps a graph that allows a replay on
the chain, and the submit funnel (`submit_funnel`, off by default: the
scheduler's kernel nodes are then submitted by one thread per device, on top of the workers; see
`docs/design/GRAPH_REPLAY/SUBMIT_FUNNEL.md`). `node_name` and `labels` describe the machine to
`topology()`. `device_profile_file` names the device profile file the runtime reads once at create
and prices its planned copies by (`modules/engine/device_profile/README.md`); empty prices them by their
bytes; a path with no file makes create measure every device and write the file, which the next start
reads; a file that cannot be read refuses the create. A runtime inside a game shares the machine with a render
thread, an audio callback and asset I/O, so every default is chosen for a tenant and not for an
owner.

The struct belongs to the api module. The engine keeps its own type,
`Internal::Execution::RuntimeSettings`, and may not depend on this one;
`Runtime::settings_of()` is the single place that maps one to the other. A field added to
`RuntimeSettings` reaches the public surface only when it is added here and in that function.
`modules/engine/execution/README.md` documents what each setting does to the engine.

Logging is process state and has no field here; `set_log_level()` and `set_log_sink()` set it.

### DeviceFilter, PinPolicy, WorkerPool

The three types are defined by the `machine` module, in `SushiRuntime`. The engine reads and fills
the same types, so a value passes to it without a conversion.

## host_runtime_config.hpp

### HostRuntimeConfig

The settings `create_host()` reads: the device count, the executor, the clock, the machine's name,
its labels and the collector the runtime's zones, values and counters record into (`profile_collector`, null for the
process-wide one; a forward-declared `SushiProfiler::Collector`, so the header includes no profiler header).

## advanced_controls.hpp

### AdvancedControls

`AdvancedControls` is the supported way to reach the few internal facts a caller needs (the device
list, the queue of a device, the rebalancer state, the worker pool, the number of live
allocations, cumulative transfer bytes) without the whole `RuntimeContext`. `Runtime::advanced()` returns it and nothing else can
construct it. It shares ownership of the context like every other handle; see Lifetime. The
surface gives no way to the `RuntimeContext` itself. Its members are defined in
`source/advanced_controls.cpp`.

### queue

`queue(device)` returns the plain `sycl::queue` the runtime submits that device's work on. A
library uses it for the host side of its own work, a `memcpy` between a host array and a Buffer
or a wait on one event, with no `RuntimeContext` in its code. Work a caller submits on it is
outside every graph: no rule is ordered against it, so the caller waits for it, or calls
`Runtime::wait()`, before a graph that touches the same memory runs. The reference stays valid
while any handle of the runtime lives. A device the runtime does not have stops the process, as
`DeviceQueueSet::require_index()` checks the index with `SR_VERIFY`.

### live_allocation_count

live_allocation_count() is the number of handles the runtime's residency directory has minted
and not retired. A Buffer holds one and a State two, a borrowed range one, and each retires its
own when it is destroyed, so a test that expects a library to free what it allocated reads the
count before and after. The allocator keeps no table, so memory that has no handle, such as a
recorder's scratch or a worker agent's cache, is not in the count.

### devices

`devices()` lists the devices in SYCL enumeration order, and entry `i` describes `DeviceIndex{i}`.
The runtime does not reorder them, so `DeviceIndex{0}`, the default of `buffer()` and `state()`, is
a CPU on one machine and a GPU on another. A program that needs a particular device reads the
`kind`, `backend` or `name` of each entry and picks the index. The reference the call returns stays
valid while any handle of the runtime lives.

### worker_pool

A caller that reserved cores for its own threads uses `worker_pool()` to verify that the reservation
took effect: a range that did not fit the machine is clamped, and `worker_pool()` reports the
clamped result, not the request.

### install_offload

`install_offload()` hands the runtime an `Internal::Graph::OffloadCoordinator` that the runs offer
their nodes to, and null removes it. A runtime starts with none. A test installs the in-memory
coordinator of `task` through it to see what a run offers. Installing or removing
one makes every compiled plan stale, so each graph compiles again on its next run. The call is not
refused while a run is in flight; the caller makes sure none is.

### install_transfer_observer

`install_transfer_observer()` hands the runtime an `SushiRuntime::TransferObserver` that every
transfer path tells of each copy it starts: the path's name, the two spaces and the byte count. Null
removes it, and a runtime starts with none. A test installs one to see which path each planned copy
took. The observer is called from the worker that runs the copy, so it must be safe to call from
several threads. The call is made between runs; the caller makes sure none is in flight.

### replay_placements

`replay_placements()` reads one placement journal, a `<device_profile_file>.<first problem hash>.journal`
another run wrote for one graph, and makes the graph whose first problem has that hash place from it: each
partition takes the journal's next first entry and each change of plan comes at the horizon the journalled
run made it at, and that graph does not rebalance on its own. Other graphs plan as before; call once per
journal to replay several. The runtime needs no profile file for it. A file that cannot be read, holds a
malformed line or opens with no first partition is `InvalidArgument`; a call after the graph with that
first problem was partitioned is `InvalidState`. A problem that differs from the
entry's fails the graph's commit with the error, which names the entry.
`modules/engine/partition_session/README.md`, "Placement journal", holds the rule.

## placement_scope.hpp

### PlacementScope

A `PlacementScope` is a class and a key, plus shared ownership of the context and of the machine
facts of the `Runtime` that made it. A copy shares both, so a scope outlives the `Runtime` and costs
a few words and two reference counts to pass. Its constructor taking a class and a key is public:
the class has no invariant for a private constructor to guard, since the name rule is checked by
`Runtime::placement_scope()` and a caller that holds a `PlacementClass` built it from a literal.
A scope built that way has no runtime behind it: `device()` gives device 0 and `buffer()` returns
`InvalidState`. `Recorder::within()` is what stamps a scope on nodes; see `api/graph/README.md`.

#### device and buffer

`device()` resolves the scope when it is asked, not when it was made. It reads the rules the
context holds now, so a scope opened before `set_placement_rules()` follows the new set. The
resolution is one function of `source/placement_scope.cpp`: `PlacementRules::resolve()` with the
scope's class, its key and an unnamed operation, so a rule that names an operation never matches a
scope. The winning rule's target is matched against `Runtime::topology()`:

| Part of the target | Gives |
| --- | --- |
| `machine_name`, `machine_label` | This machine when the name or the label is the machine's own; otherwise the target is absent here |
| `device_index` | That device, or absent when the machine has fewer devices |
| `device_key` | The device whose `DeviceFacts::key` equals it, or absent; the same card whatever order the devices are listed in |
| `device_kind` | The first device of that kind, or absent |
| `device_min_memory` | The first device with at least that many bytes, or absent |
| none of the device fields | Device 0 |

No rule, or no matching rule, gives device 0. An absent target follows the rule's `if_absent` on
one machine: `Local` gives device 0, `Wait` sleeps for the bound and then gives device 0, since a
local device never appears during a run, and `Fail` returns `InvalidArgument` naming the rule and
the device. An unnamed rule is named by its position.

`buffer<T>(count, residency, alignment)` calls `device()` and then allocates as `Runtime::buffer()`
does, through the same private `Buffer<T>::allocate()` that `PlacementScope` is a friend of. It
returns the error of `device()` unchanged, so a `Fail` rule allocates nothing. A placed device is a
stated device, so `Residency::Planned` there returns `InvalidArgument`.

The scope reads the rules without a lock, as a compile does, so `device()` and `set_placement_rules()`
are called from the thread that starts runs.

## runtime.hpp

### create

`create()` calls `require_matching_version()` before it builds anything. The function is defined in
the header, so it compares the release the caller compiled against with the release of the shared
library the loader found. A mismatch is a rule of the build that was broken, not a failure of the
run, so it stops the process through `SR_VERIFY` with a message naming both releases instead of
failing later in an unrelated call. `version_matches()` is the query for a program that wants to
decide for itself.

`create()` returns `Result<Runtime>`. Nothing else a runtime needs at construction is a rule of
the caller, so every other failure comes back as an `Error`: `Unsupported` when no device passes
`config.devices`, `DeviceLost` when SYCL cannot build a device queue, and `AllocationFailed` when
the host cannot give the runtime its memory or its threads. A label that breaks a rule returns
`InvalidArgument` before any context is built; see topology. A program that cannot go on without a
runtime writes `Runtime::create().value()`, which stops the process with the error's message on
standard error.

### create_host

`create_host(HostRuntimeConfig)` builds a runtime that makes no device, no queue and no thread,
for a test or a tool that needs the runtime's logic and not its hardware. Its buffers are host
memory from `HostUSMSource`; its devices are `config.device_count` host devices named
`host device <i>`; every run goes to `HostExecutionEngine`, never to the in-order chain, so a
report's layer is `Scheduler`. Host nodes and launches over a count or an `Extent` run as jobs of
`config.executor`, on the thread that waits for the run; an `NdRange`, a raw command group, a
queue body, a device-sized launch and a late-count launch have no host form and fail their run
with `Unsupported`. `advanced().queue()` stops the process, since there is no queue. Copies to and
from a buffer are `memcpy`.

The executor and the clock are the caller's to keep: a test holds the `SeededExecutor` to replay
an order from its seed or to step an asynchronous run, and advances a `ManualClock` to time a
dispatch. A null executor is one of seed 0, a null clock a `ManualClock` at 0. The machine's name
is `config.node_name`, "host" unless set, and the host name is never read. A label that breaks a
rule returns `InvalidArgument`, as for `create()`.

### topology

`topology()` returns the `NodeFacts` of the machine: its name, its operating system, its labels and
one `DeviceFacts` per device. `create()` builds them once, before it builds the context, so a refused
label creates nothing. The devices are copied from the inventory `advanced().devices()` returns, entry
for entry. The name is `RuntimeConfig::node_name`, or the host name when that is empty; a host name
that cannot be read returns `Unsupported`. The operating system is the one the library was compiled
for. The facts live in one shared allocation that the `Runtime` and its scopes hold, so the
reference stays valid until the `Runtime` is moved from or destroyed.

### device_key and device_index

`device_key(DeviceIndex)` returns the `DeviceKey` of a device: the backend and hardware id that
name the same card in every run. `device_index(DeviceKey)` returns the position that key holds in
this run, or `InvalidArgument` when no device of the runtime holds it. Both read the `key` of each
`DeviceFacts`, which `create()` copies from discovery; a host runtime keys its devices by their
position. A placement rule that names a `DeviceKey` resolves to the same card when the order of
the devices changes.

A label key is refused with `InvalidArgument` when it is empty, holds `=` or repeats the key of an
earlier label. The message names the label's position and key and says what to pass instead. A value
may be empty. Labels keep the order the config gave them.

### placement_scope

`placement_scope(name, key)` returns the scope of the class a dotted name spells, such as
`sushiai.replica`, and an optional key; the key 0 is a key like any other. The overload taking a
`PlacementClass` takes a class built elsewhere, as in `placement_scope("sushiai.replica"_class)`,
and builds the same scope as the string overload for the same name.

A name that is empty, starts or ends with `.`, or holds two `.` in a row is a rule the caller broke,
so the string overload stops the process through `SR_VERIFY`, as `buffer()` does for a device
index it does not drive. It does not return a `Result`: the name is a literal in the caller's
source, and a caller that wrote a bad one cannot recover at run time. The call touches no
context, so it costs one hash of the name. The scope it returns holds the runtime's context and facts,
which `PlacementScope::device()` and `buffer()` read.

### set_placement_rules and clear_placement_rules

`set_placement_rules(rules)` installs a `PlacementRules` for the whole runtime and
`clear_placement_rules()` removes it. Neither changes a plan that is already compiled: the rules take
effect at the next compile. The call bumps a generation the context keeps, a graph sees it in
`plan_is_stale()`, and its next `compile()`, `plan()` or run builds the plan again and resolves each
node against the new rules. A graph that no call changed is not rebuilt.

Both calls return `ErrorCode::InvalidState` while a run of the runtime is in flight, with a message
that says to wait for the run. The rules are plain data in the context and a compile reads them
without a lock, so the refusal is what keeps a write and a read apart. A caller that sets rules on a
thread other than the one that starts runs has to order the two itself: the refusal checks the run
state at the moment of the call and does not hold a run back.

### Copy and const

A `Runtime` moves and does not copy. A copy would be a second handle to the same context, which
reads as a second runtime and is not one. The factories and `set_rebalancer_enabled()` are `const`
because a `Runtime` is a handle: constness of the handle says nothing about the context it points
at, and a `const Runtime&` parameter can allocate and build graphs.

### dynamic_graph

`dynamic_graph()` is the mutable counterpart to `graph()`: work is grouped into regions
(`region(key)`) that can be added or dropped between steps, so a streaming open world re-stitches
only the cells that changed instead of recompiling the whole graph.

### buffer and state

`buffer()` allocates on the device its `DeviceIndex` names, and the buffer frees the memory when it
is destroyed; `state()` allocates the two fields of a State the same way. Both return `Result`:
a device that returns no memory is an `AllocationFailed` error, and a State whose second field
fails releases the first. An index outside `[0, advanced().device_count())` is a rule the caller
broke and stops the process; `api/data/README.md` has the checks.

`buffer<T>(count, Residency::Planned)` and `state<T>(count, Residency::Planned)` take no device and
allocate nothing: the runtime places the memory when a node uses it. The forms with a `DeviceIndex`
refuse `Residency::Planned`, and the forms without one refuse every other residency, both with
`InvalidArgument`. `api/data/README.md`, section "Residency::Planned", has the rules.

`buffer_from_file<T>(path, offset, count)` makes a `Residency::Planned` buffer of `count` elements
whose bytes are the file at `path` from byte `offset`. The file range is the buffer's one valid
copy: the runtime adds a read-only memory space for it and streams the bytes a device or the host
needs through its staging ring, never the whole file. A file that cannot be read or is shorter than
the range is an `InvalidArgument` error. Each call takes one memory space id for the life of the
runtime, and a residency entry names at most 64 spaces, host and devices included, so a runtime
holds at most that many file ranges; past that the call returns `AllocationFailed`.

Both take an alignment in bytes as their last argument. Zero, the default, asks for the device's
own alignment. Any other value must be a power of two and reaches `USMRequest::alignment`, so the
address is a multiple of it; a State aligns both fields. A value that is not a power of two stops
the process.

```cpp
auto lanes = runtime.buffer<float>(count, SushiRuntime::DeviceIndex{0},
                                   SushiRuntime::Residency::Shared, 128);
```

### state

`state()` is for data a stepped graph reads as "current" and writes as "next"; the runtime swaps the
two each step. It validates its `DeviceIndex` as `buffer()` does.

### add_run_observer

`Runtime::add_run_observer()` adds an observer that receives the capture of every finished run of the
runtime and returns a `RunObserverHandle`; the handle removes the observer when it is destroyed or
reset. Any number of observers may be added and each receives every run, in the order added. While at
least one is added, every run is timed. The choice is made per run, when the run is armed, so no plan is
rebuilt when an observer is added or removed: the run that starts after the call carries records, and a run
already in flight reaches the observer with none. The runtime shares ownership of the observer
until the handle removes it, and a handle that outlives the runtime removes nothing. The header
forward-declares `RunObserver` and `RunObserverHandle`; a caller includes
`SushiRuntime/report/run_observer.hpp`. An observer's `on_run()` runs on the thread that learned the
run finished and must not start a run or add or remove an observer. The `profiling` module's
`register_profiler_parts()` is the host's way to put the observations on a SushiProfiler timeline.

### partition_report

`Runtime::partition_report()` returns a `PartitionReport` (`SushiRuntime/report/partition_report.hpp`): one
row per device with the nodes and bytes it holds, the modelled and measured time of a step on it, its copies
by path and, for a refused job, the bytes its pool lacked; for the plan the modelled and measured step and the
floor's modelled step and devices; and `to_text()` for a log. Each graph keeps its own plan, so
`Graph::partition_report()` and `DynamicGraph::partition_report()` report that graph's plan and runs;
`Runtime::partition_report()` reports the graph that partitioned last, and has no rows once that graph is
gone. A runtime with a device profile file and two devices or more partitions a graph that touches a
`Residency::Planned` buffer. Before that the report has no rows. A job no plan fits is refused at compile
with `AllocationFailed` and each pool's shortfall. The rows come from `PartitionSession::report()` of the
`partition_session` module.

### set_rebalancer_enabled

The rebalancer is off unless `RuntimeConfig::rebalancer` asked for it. Running it trades run-to-run
reproducibility and the jitter floor for throughput, by migrating tasks between workers on a 5 ms
heartbeat. That is worth it for long batch work and wrong for a simulation frame.
`set_rebalancer_enabled()` is safe to call between runs.

### wait

The call is the stage zone `wait.runtime`.

`wait()` blocks until every device queue of the runtime is idle: the queues the graphs ran on and
any work a caller submitted through `advanced().queue()`. A blocking run has already waited for
its own steps, so `wait()` is for work outside a graph and for a caller that wants the devices
quiet before it reads memory. It does not settle a step started with run_async(); the RunHandle
does. A command that failed on a device arrives as `KernelFailed` with the device's text.

### The context is not reachable

Runtime holds a `ContextHandle` and has no member that returns the context. The members that need
it, `create()`, `set_rebalancer_enabled()`, `set_placement_rules()`, `clear_placement_rules()` and
`wait()`, are defined in `source/runtime.cpp`.
`create()` stays inline as far as the version check, which must compare the headers a program
was compiled with against the library it loaded.

The distributed layer sits above this one and enters through a `Runtime&`:
`Distributed::Cluster::attach()` takes the Runtime, reaches its context through
`Internal::ContextAccess` of the `context_access` module and installs its master there, so no
header here names a transport. Runtime befriends `Detail::ContextHandleAccess` of the `context`
module for that, which reads the handle and nothing else.

## errors.hpp

### The error names

errors.hpp declares nothing. It includes `core/error.hpp` and `core/result.hpp`, which declare
`Error`, `ErrorCode` and `Result` in `SushiRuntime`, so a program that handles a failed
`create()` or `run()` includes one header of `api/` for them.
