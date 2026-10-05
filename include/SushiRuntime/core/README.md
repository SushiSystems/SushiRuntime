# core

Types every other layer uses: errors, results, the logger, reference counting, resource regions and
the run report.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## bit.hpp

### C++17 stand-ins

bit.hpp holds bit helpers that stand in for C++20 \<bit> (std::popcount and std::countr_zero) on the
C++17 baseline. The std:: versions come back when the standard is raised;
docs/design/REMAINING_WORK.md lists that under "C++ standard migration".

## delegate.hpp

### Why Delegate exists

Delegate is a move-only, allocation-free, type-erased callable that replaces std::function for the
runtime's stored closures.

The runtime stores user callables that are polled every step (late-binding predicates, per-step
updaters) and replay closures built once per add(). Using std::function for these costs a heap
allocation per stored closure and drags in RTTI and target() machinery the runtime never uses.
Delegate erases the callable into a fixed inline buffer with a hand-written three-entry vtable, so
it never touches the heap. A capture that does not fit is a compile error, not a silent allocation,
which is the guarantee a real-time, deterministic runtime wants at its API surface.

### Delegate

A Delegate holds any callable matching `R(Args...)` whose size and alignment fit the inline storage.
The callable is relocated, not reference-counted, so the Delegate owns it outright and frees it on
destruction. Unlike std::function it is move-only (a stored closure has one owner) and never
allocates: a target too large for `Capacity` fails a static_assert instead of spilling to the heap.
A default-constructed Delegate is empty; calling an empty Delegate is a precondition violation,
asserted in debug builds.

### Delegate(F&&)

The converting constructor move-constructs the decayed callable into the inline buffer and binds the
static vtable for its type. The size and alignment fit is enforced at compile time, so the
constructor never allocates and never fails.

### vtable_for

vtable_for() gives one shared vtable instance per target type. It reaches the instance through a
function-local static, so it needs no out-of-line definition and has a single address across
translation units.

## event_timestamps.hpp

### read_event_timestamps

A backend with no start/end reading leaves the dispatch unrecorded; one with no submit reading
degrades to submit_ns == start_ns.

## logger.hpp

### AsyncLogger

The logger shuts down in four steps, and the order makes the shutdown free of races.

1. The destructor acquires the mutex. 2. It sets running_ to false while holding the mutex. 3. It
releases the mutex and notifies the condition variable. 4. It joins the worker.

If the destructor did not hold the mutex during the store, the worker could evaluate its wait
predicate (queue empty and running_ true), decide to sleep, and then park on the condition variable
after the destructor's notify_one fires. The notify is then lost and the worker sleeps forever,
deadlocking the join. Holding the mutex during the store closes that window: the worker is either
still inside the predicate, and will re-check and exit, or already parked, and will be woken by the
notify that follows the unlock.

### running_

`running_` is guarded by `mutex_`. It was an atomic at first; the atomic gave a false sense of
safety because the store and notify pair was not synchronized with the worker's wait predicate.

### process_logs: draining

process_logs() drains the queue whatever the state of running_, so that messages enqueued just
before shutdown are still flushed. It exits only once the queue is empty and shutdown has been
requested.

### ~AsyncLogger: shutdown flag

The destructor sets the shutdown flag while holding the mutex. When it then calls notify_one, any
worker inside cv_.wait is either already parked, and the notify wakes it, or has not yet
re-evaluated the predicate, and will see running_ false on the next check. Without the lock, the
store could land between the predicate check and the park, and the notify would be lost.

### dispatch_log

is_sync_logging_enabled() governs whether dispatch_log() logs synchronously or through the queue.
Switching modes mid-run is safe because the flag is atomic and each logger owns its own mutex and
queue.

### sr_format

sr_format() is a small std::format stand-in for C++17: it replaces positional {} markers left to
right by streaming each argument with operator\<\<, and treats {{ and }} as literal braces. It
intentionally supports no format specs ({:...}); a call site that needs width or hex formats the
value before logging. Extra arguments and extra placeholders are ignored, not diagnosed, which
matches a logger's best-effort role.

### Log macros

The format string is folded into \_\_VA_ARGS\_\_, so the macros take (...) and not (fmt, ...). This
keeps a single, always-present variadic list and avoids \_\_VA_OPT\_\_ and ##\_\_VA_ARGS\_\_,
neither of which is standard C++17. log_base's std::string_view fmt parameter then binds the first
argument.

## profiling_ledger.hpp

### ProfilingLedger

A ProfilingLedger is owned by CompiledGraph and reused across runs. Each row is sized to its own
node's expected dispatch count times how many internal nodes share its registration_index (F5).
reserve() grows it outside the poller, before a run's dispatch loop starts, so claim_slot() and
write() never allocate. The ledger also carries the run's clock pairing claim (design
2026-09-15-g1-node-event-records-design.md §C).

### reserve

reserve() sizes row i to expected_invocations * multiplicities[i], or to the row's own last observed
demand if that was larger. An overflowing row therefore grows on the very next call (item 4), and a
shared registration_index (F5) gets a row big enough for every node behind it.

## ref_counted.hpp

### RefCounted

The counter starts at 0. Each sushi_ptr copy increments it with relaxed ordering; no synchronisation
is needed, because ownership cannot change without an existing reference. The last release() issues
a release fence before calling delete, so all prior writes are visible to any thread that had access
through an earlier reference.

## resource_liveness.hpp

### Why the liveness token exists

A LivenessToken is a token a data handle owns and a graph weakly observes.

A fluent graph records the base pointers of the Buffers and States its tasks touch, but it does not
own them. If a handle is destroyed before the graph runs, those pointers dangle and a run is silent
undefined behaviour. A handle holds one shared LivenessToken; the graph keeps a weak observer of it
per recorded resource. When the handle dies the token expires, so the graph can detect a stale
resource at run time and fail cleanly instead of dispatching against freed memory. The token carries
no state: its existence is the signal, so observing it costs a weak_ptr lock and never touches the
dependency hot path.

## resource_region.hpp

### ResourceRegion

The dependency tracker keys ordering on regions, not bare pointers, so it can tell apart two
sub-ranges of the same allocation (an array's two halves, a grid's interior and its halo). Two
accesses are ordered if and only if their regions overlap; disjoint sub-ranges of the same buffer
stay parallel, and distinct allocations never serialize.

A bare pointer converts to the whole-buffer region implicitly, so every caller that still speaks
base pointers keeps the previous behaviour: a whole region overlaps any region of the same base,
degenerating the overlap test to base-pointer equality.

### overlaps

overlaps() is the test the tracker uses to decide ordering: same base and intersecting half-open
intervals. WHOLE is treated as an unbounded upper end, so the comparison never overflows on the
sentinel.

### contains

contains() is true if `r` lies entirely within this region. It lets the tracker stop walking history
early: once a write that covers the accessed region is found, every earlier access to that range is
already ordered behind it transitively.

## run_report.hpp

### NodeEventRecord

There is one NodeEventRecord per node dispatch, never one per SYCL command group a host-work node
submits internally. `invocation` is the step index within `run(steps)`, zero for a single run.
`node_index` is the node's public registration index, so several internal nodes one `add()` call
created share it and the pair may repeat within a run.

### reset

reset() is what makes a report reusable. Assigning a fresh RunReport over an old one would free
every vector's storage and the next run would allocate it again; clearing keeps the buffers, so a
report handed to `run(RunReport&)` every tick stops allocating after the first run.

The retained capacity covers the rows, not what a row owns: clearing node_timings destroys each
row's name string. That is why the zero-allocation claim is stated for a steady state with profiling
off. With profiling on, the node labels are rebuilt each run by design.

## span.hpp

### Choosing std::span or the shim

span.hpp keys the choice on \_\_cplusplus (the language standard), not on the library macro
\_\_cpp_lib_span. The library macro is only defined after a header that provides it (\<version> or
\<span>) has been included, so its visibility varies per translation unit. That silently splits
SushiRuntime::span into std::span in one TU and the shim in another, and the mismatched mangled
signatures produce link errors. \_\_cplusplus is defined uniformly for every TU in the build, so all
TUs agree.

## strong_id.hpp

### Why StrongId exists

StrongId is a zero-overhead strong typedef for integer identifiers.

A bare std::size_t parameter lets unrelated quantities be passed in each other's place, a device
index where an element count belongs for example, and the compiler stays silent. StrongId wraps an
integer in a distinct type keyed by a tag, so the compiler rejects the mix while the value still
occupies exactly one integer and the explicit constructor keeps construction intentional.

### StrongId

Two StrongId instantiations with different tags are unrelated types even when their underlying
representation matches, so one cannot be passed where the other is expected. The constructor is
explicit, so a raw integer never converts implicitly; the value is read through value(). A StrongId
is trivial and the size of `Rep`, so passing one costs nothing over passing the integer.

## sycl_compat.hpp

### has_host_task

has_host_task uses SFINAE to test at compile time whether a command group handler has the standard
host_task method.

### enqueue_host_task

enqueue_host_task() picks the host execution mechanism the toolchain has. A handler with host_task
uses it. AdaptiveCpp has none, so the callable goes through AdaptiveCpp_enqueue_custom_operation;
the custom operation runs on the host once its dependencies are satisfied. Its interop_handle
argument is unused, and the lambda accepts it generically so that its type need not be named. The
callable takes no arguments.
