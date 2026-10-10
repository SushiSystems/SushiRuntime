# api/detail

Internals of the fluent API: callable traits, the replay callable of each node shape, late-count
resolution, the per-step machinery and report building.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## callable_traits.hpp

### The three callable traits

The fluent add() overloads accept three callable shapes, and callable_traits.hpp expresses each as a
C++17 SFINAE trait over std::is_invocable_v. The C++20 concepts they replace read the same, only as
`requires`-clauses. enable_if on each add() selects exactly one. A wrong signature removes every
overload, so the compiler reports "no matching call", which is less pointed than a concept
diagnostic but still names the offending callable.

### views_current

views_current() reads each coupled-input view's current pointer into a plain tuple, host-side at
submit time. A StateView holds the live current/next pointers for the step, so dereferencing in
views_current(), and not in the kernel, lets the device lambda capture raw pointers and still follow
the swap.

### read_keys_of

read_keys_of() gathers the read keys of a tuple of State pointers, in order. It is called at compile
time so the dependency tracker labels every coupled input consistently with the canonical
single-State overload.

## handle_access.hpp

### HandleAccess

HandleAccess is the one place the graph layer reads a data handle's tracking state: the dependency
key of a Buffer, the read and write keys of a State, the liveness observer of either, and the swap
that makes a State's written field current. Buffer and State name it a friend, so none of the five
has to be a public member, and a change to how a handle keeps its key or its token is made in this
header and in the handle, with no recording code to follow. Its member templates are defined against
forward declarations of Buffer and State, so the handles' own headers include it.

construct() calls a private constructor of either handle. Runtime and Interop do not need it, since
they are friends of the handles; it serves code that builds a handle without a runtime, which today
is the unit test of the adopting constructor (tests/unit/test_allocation.cpp).

## run_report_builder.hpp

### fill_run_report

fill_run_report() maps scheduler counters and device placement onto the public RunReport structure.
`before` and `after` are the scheduler execution metrics snapshotted before the run started and
after it completed. It writes into a caller-owned report so a steady-state tick can hand back the
same object every run: `report` is reset first, which keeps the vector storage the previous run
allocated.

### Device class attribution

fill_run_report() attributes the tasks to the device class the default queue targets. Per-task
placement and the reserved steal/byte counters are filled in as the engine exposes them.

It asks the runtime's own inventory for the device class, not SYCL. fill_run_report() runs on every
tick, including the steady-state loop that run(RunReport&) promises will not allocate, and asking
SYCL could not keep that promise portably: queue::get_device() returns a device by value, and the
cost of that construction belongs to the SYCL runtime. On oneAPI's libsycl (the Linux CI toolchain)
it allocated once per call, which is why the steady-state test measured 65 allocations over 64 ticks
there and 0 on Windows, where the intel/llvm nightly does not allocate. The device class of a queue
is fixed for the life of the context, so the answer was never worth re-deriving per tick on any
platform.

## step_machinery.hpp

### StepMachinery

Both the static Graph and the DynamicGraph composer drive the same compile-once, replay-many loop,
and both need the identical four per-step hook lists run in the identical order. StepMachinery owns
those lists, plus the State double-buffer bookkeeping that produces them. The ordering invariant is:
re-point distributed live IO, refresh late-bound controls, execute, then advance every State. It
lives in one place instead of being copied into each driver.

The building side (view_for / register_advance / register_binding_updater) is used by Graph's add()
overloads as they record tasks. The driving side (resync / before_step / after_step) is used by both
drivers. The aggregation side (reset_updaters / aggregate_from) lets DynamicGraph gather every live
region's hooks into one before a run.

### StepHook

A StepHook is a per-step hook stored without heap allocation. Its capacity is derived from the
largest hook and not written as a literal: a binding updater captures a whole Dynamic (one provider
slot per late-bound control) plus a LateBindings shared pointer, so adding a control widens the
capacity automatically instead of tripping Delegate's static_assert in every translation unit.

### view_for

view_for() returns the shared view for a State, creating it and registering its resync on first use.
Every kernel touching a State captures the same view, so one advancer keeps all references current.
Correct coupled (multi-field) updates rest on that.

### register_advance

register_advance() registers the once-per-step advance for a written State. It deduplicates, so a
State advances exactly once per step (one update rule per field).

### register_binding_updater

register_binding_updater() wires a Dynamic's providers to a node's bindings: it pushes a per-step
updater that polls when()/sized() on the driver thread and stores the results into the shared block
the kernel and scheduler read. It is skipped when the Dynamic carries no providers, because the
defaults (always enabled, full range) already hold, so an unused tag costs nothing.

### reset_updaters

reset_updaters() clears this machinery's aggregation of other machineries. A composer calls it and
then re-aggregates the live regions before a run. A region's own hooks, and the State bookkeeping
that owns the views, are untouched, since the region still owns them.

### aggregate_from

aggregate_from() references another machinery's per-step hooks from this one, so a composer drives
every live region's hooks without copying them (the hooks are move-only). The referenced machinery
must outlive every run driven through this one. It is the region's own, owned for the region's
lifetime.

## emit_dynamic.hpp

### adopt_device_bindings

adopt_device_bindings() copies a Dynamic's device count source onto the node's bindings and
registers that slot as a read. Registering inside adopt_device_bindings(), and not asking the caller
for it, is what makes "the producer runs first" a property of the graph instead of a documented
convention: the read region orders this node behind whoever writes the count.

### register_device_slot

register_device_slot() names one four-byte slot as a read of the node that will consume it. It names
just the one element, not the whole buffer, so two nodes reading different slots out of one buffer
stay unordered against each other.

## emit_*.hpp

### Replay callables

Each emit header builds the replay callable of one node shape: a lambda invoked as
emit(BackingGraph&, registration) every time a Recorder replays. The lambda adds one node, hands
the registration through (empty means the backing graph draws a fresh index) and stamps the launch
width where the shape has one. None of them knows the Recorder, so a shape can be read, changed or
added without touching the recording state.

## host_slot_check.hpp

### require_host_slot

require_host_slot() asks the slot's own address first. When that answers anything but host, it
resolves the address to the allocation containing it and asks again: a caller offsetting into a plan
array hands over an interior pointer, which SYCL reports as unknown. It runs while add() builds a
node, so the containment search never meets a dispatch. A null slot passes.

## late_count_resolution.hpp

### late_count_source and read_live_count

A resolver node reads a host count on the host and a device count in its kernel.
late_count_source() runs once per step, when the resolver is submitted. For a device count it copies
the address of the slot and reads nothing. For a host count it calls the provider, and for a fixed
count it takes the capacity; a provider that returns more than the capacity throws `invalid_graph`
there. read_live_count() runs in the kernel: it reads the slot, and a value above the capacity
becomes zero and a DEVICE_FAULT_LATE_COUNT record.

Neither validates the LateCount. Its factories checked the capacity, the residency of the slot and
the index when the count was built (api/vocabulary/README.md), so a step compares nothing but the
live count with the capacity.

### gather_device_read

gather_device_read() gives a recorder what a device count adds to its resolver: the region of the
slot's buffer as a read, which orders the resolver after the kernel that writes the count, and the
buffer's liveness observer. It returns whether the count has a device slot, so the recorder knows
the resolver needs a fault record.

## device_fault.hpp

### late_count_overflow

A count over its capacity is found in two places: on the host by late_count_source(), and after the
step by throw_if_device_fault() reading the record a kernel left. late_count_overflow() builds the
error for both, so the two paths report one message.

## late_window.hpp

### LiveRange

A LiveRange is the half-open element window a late-bound node iterates this step: where it starts
and how many elements it covers. The two are resolved and validated together, because only their
combination says whether the node stays inside the buffers it was compiled for.

### submit_live_range

submit_live_range() submits a late-bound node's kernel over its live size. A step with nothing to do
is ordinary (a tick with no contacts), and a zero-width parallel_for is not portable across SYCL
backends, so an empty count submits an empty kernel instead: the node still completes and still
releases its successors.

### submit_live_range: base offset

submit_live_range() applies the offset itself and does not leave it to the caller, so a kernel
written against absolute indices stays correct whether or not its node happens to carry a base.

## node_spec.hpp

### NodeSpec

NodeSpecs are the recorded task specifications: each replays one add() onto a fresh backing graph.
The fluent Graph is the source of truth so it can rebuild and recompile when mutated. A spec is
recorded as {emitter, rule} and not as a bare callable. The name of a rule lives in the recorder,
which owns the text and hands it to every node of the rule when the plan is built; that is what
lets name_last() attach one after the add() call has already returned.

## usm_scratch.hpp

### make_usm_scratch

make_usm_scratch() allocates on the device and with the residency the caller names. A rule passes
those of the data it works on, so its nodes never straddle a device context and a device-resident
field is not dragged into host-addressable memory. The returned pointer owns the
allocation through a deleter that keeps the context alive, so scratch cannot outlive the runtime
that owns it.