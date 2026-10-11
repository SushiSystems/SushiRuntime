# api/detail

Internals of the fluent API: callable traits, the replay callable of each node shape, late-count
resolution, the per-step machinery and report building.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## callable_traits.hpp

### The callable traits

callable_traits.hpp names four callable shapes as C++17 traits over std::is_invocable_v: a
per-element kernel, a range kernel, a raw kernel and a task body. The first three have two users.
The Recorder chooses between a per-element node and a range node by the first two:
untracked_kernel() by overload, and kernel(count, ...) inside one overload, after accepts_kernel()
has asserted the callable. callable_checks.hpp reads all three in accepts_per_element(),
accepts_kernel() and accepts_raw_kernel(). add() selects its overload by the launch and the access.

is_per_element_v and is_range_kernel_v test a `const std::decay_t<F>&`, as is_launch_kernel_v
does, because the emitters call those kernels as const objects. A `mutable` lambda that captures
state is therefore false for both and stops at the static_assert of its family. A captureless one
converts to a function pointer and stays callable as const. is_raw_kernel_v tests F itself:
raw_node() stores the command group in a std::function, which calls it as a non-const object, so
a `mutable` raw kernel runs. is_task_body_v tests a non-const `std::decay_t<F>&` for the same
reason, and also that the result converts to `sycl::event`; accepts_task() is its one reader.

### views_current

views_current() reads each coupled-input view's current pointer into a plain tuple of `const T*`,
host-side at submit time. A StateView holds the live current/next pointers for the step, so
dereferencing in views_current(), and not in the kernel, lets the device lambda capture raw pointers
and still follow the swap. read_only() makes the conversion to `const T*`, so the kernel of a
coupled rule cannot write a State its node registered as a read.

### read_keys_of_views

read_keys_of_views() gathers the read keys of a tuple of views, in order: the key of a State's
current field is the address `view->current` holds. add_coupled_node() calls it at every replay, so
the dependency tracker labels a coupled input with the key the single-State rule gives that field.

## handle_access.hpp

### HandleAccess

HandleAccess is the one place the graph layer reads a data handle's tracking state: the dependency
key of a Buffer, the read and write keys of a State, the liveness observer of either, the view a
State shares with its graphs, and the swap that makes a State's written field current. Buffer and
State name it a friend, so none of these has to be a public member, and a change to how a handle
keeps its key or its token is made in this
header and in the handle, with no recording code to follow. Its member templates are defined against
forward declarations of Buffer and State, so the handles' own headers include it.

view() returns the StateView a State owns (api/data/README.md), and a new empty view for a State
that owns no memory, so a caller never holds a null pointer. advance(State&) swaps the two fields
and writes the swap into the view; it is the only place a State is swapped. advance(StateView&) and
resync(StateView&) are what a step hook calls: each reaches the State through `view.owner`, which
the State keeps pointed at itself across a move, and does nothing once the State is gone. A hook
therefore holds the view and never a `State<T>*`.

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

fill_run_report() attributes every task of the run to the device class of device 0, the default
queue. It does not attribute tasks per device.

It asks the runtime's own inventory for the device class, not SYCL. fill_run_report() runs on every
tick, including the steady-state loop that run(RunReport&) promises will not allocate, and asking
SYCL could not keep that promise portably: queue::get_device() returns a device by value, and the
cost of that construction belongs to the SYCL runtime. On oneAPI's libsycl (the Linux CI toolchain)
it allocated once per call, which is why the steady-state test measured 65 allocations over 64 ticks
there and 0 on Windows, where the intel/llvm nightly does not allocate. The device class of a queue
is fixed for the life of the context, so the inventory's answer is the one SYCL would give.

## step_machinery.hpp

### StepMachinery

Both the static Graph and the DynamicGraph composer drive the same compile-once, replay-many loop,
and both need the identical four per-step hook lists run in the identical order. StepMachinery owns
those lists, plus the State double-buffer bookkeeping that produces them. The ordering invariant is:
re-point the live fields, refresh late-bound controls, execute, then advance every State. It
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

view_for() returns the view the State owns, through HandleAccess::view(), and registers that view's
resync the first time this machinery sees it. It creates no view for a State that owns memory.
Every kernel touching a State captures the same view, so one advance keeps all references current.
Correct coupled (multi-field) updates rest on that.

### register_advance

register_advance() registers the once-per-step advance for a written State. The hook captures the
view and calls HandleAccess::advance() on it. It deduplicates by view, which a move does not change,
so a State advances exactly once per step (one update rule per field).

### register_binding_updater

register_binding_updater() wires a Dynamic's providers to a node's bindings: it pushes a per-step
updater that polls when()/sized() on the driver thread and stores the results into the shared block
the kernel and scheduler read. It is skipped when the Dynamic carries no providers, because the
defaults (always enabled, full range) already hold, so an unused tag costs nothing.

### reset_updaters

reset_updaters() clears this machinery's aggregation of other machineries. A composer calls it and
then re-aggregates the live regions before a run. A region's own hooks are untouched, since the
region still owns them.

### aggregate_from

aggregate_from() references another machinery's per-step hooks from this one, so a composer drives
every live region's hooks without copying them (the hooks are move-only). The referenced machinery
must outlive every run driven through this one. It is the region's own, owned for the region's
lifetime.

## before_step_hooks.hpp

### BeforeStepHooks

BeforeStepHooks owns the hooks a caller gave a graph through before_each_step() and runs them in
the order they were added. It is apart from StepMachinery on purpose: StepMachinery belongs to
the recorded work and is rebuilt by Recorder::clear(), and a DynamicGraph aggregates one per
region, while these hooks belong to the graph and are the caller's.

A hook is stored in a Delegate of 64 bytes, with no allocation for the callable. add() holds a
callable that is larger, over-aligned or whose move may throw through a `std::unique_ptr`
inside the Delegate, so every callable is accepted and only the ones that do not fit pay an
allocation, once, when they are added. The list is a vector, so the first hook allocates the
list.

## emit_dynamic.hpp

### adopt_device_bindings

adopt_device_bindings() copies a Dynamic's device count slot and its device base slot onto the
node's bindings. For each slot the Dynamic has, it registers the slot as a read and hands the
caller the liveness observer of the slot's buffer. The read region orders the node behind whoever
writes the slot, and the caller did not have to name it. The observer goes to Recorder::record()
with the node's other observers, so a run after the buffer was destroyed is refused with
`DanglingResource`.

### register_device_slot

register_device_slot() names one four-byte slot as a read of the node that will consume it. It names
just the one element, not the whole buffer, so two nodes reading different slots out of one buffer
stay unordered against each other. It appends the slot's observer in the same call, so no caller
can register the read and leave the buffer unwatched.

## callable_checks.hpp

### One static_assert per family

Up to v0.3 every add() overload was enabled by the signature of its callable. A callable of the
wrong signature removed every overload, and the compiler answered "no matching member function"
with the whole overload set as candidates; the two variadic forms did not constrain the callable
at all and failed inside a SYCL parallel_for.

add() now selects an overload by its launch and its access, and hands the callable to one
accepts_*() function of this header. Each holds one static_assert whose text states the signature
the family expects. A wrong callable produces that sentence as its one error, with a note that
points at the caller's add() line.

| Function | Family |
| --- | --- |
| accepts_kernel | add(launch, reads, writes, kernel), add_untracked(launch, kernel) |
| accepts_captured_kernel, accepts_captures | add(launch, In/Out/InOut..., kernel) |
| accepts_state_rule | add_rule(state, kernel), add_rule(launch, state, kernel), add_dynamic(Dynamic, state, kernel) |
| accepts_coupled_rule | add_coupled(Coupled, kernel), add_coupled(launch, Coupled, kernel) |
| accepts_per_element | add_dynamic(Dynamic, capacity, reads, writes, kernel) |
| accepts_raw_kernel | add(reads, writes, kernel), add_untracked(kernel), add_untracked(On, kernel) |
| accepts_task | add_task(reads, writes, body) |
| accepts_host_task, accepts_extension | add_host, extend |
| accepts_step_hook | before_each_step(hook) |

Each function returns what it asserted. The overload records inside `if constexpr` on that
value, so after a failed assertion the recording call is not instantiated and the caller reads
one error instead of the assertion followed by a chain from inside the emitter.

### is_launch_kernel_v

is_launch_kernel_v\<Launch, F, Extra...> says whether a launch can call `kernel(index, extra...)`,
where the index is the one that launch hands over. It is specialised once per launch, so every
family checks the index the same way and a new launch adds one specialisation. An NdRange accepts
two shapes, with and without the shifted id. The callable is tested as a const object because a
SYCL kernel is called through a const reference.

## emit_launch.hpp

### Launch traits

is_launch_v tells a launch from every other first argument of add(), which is how the overloads
that start with a launch are told from those that start with a State, a Coupled, a Dynamic or an
OperationID. Any integer other than bool counts as an element count, so `add(1024, ...)` and
`add(buffer.size(), ...)` both compile; launch_of() turns it into the std::size_t the emitters
record.

### submit_launch

submit_launch() is the one parallel_for of each launch. Every emitter that runs a kernel over a
launch calls it with the values the kernel receives after its index: none for a plain kernel, the
capture pointers, or the State fields. A launch is therefore written once, and add_rule(NdRange,
state, kernel) or add(count, In(a), kernel) exist because the emitters are generic over it and for no
other reason. The Extent form adds the origin to each id. The NdRange form adds it only for a
kernel that takes the id as a second parameter.

### launch_reach and launch_refusal

A State rule or a coupled rule hands its kernel the fields of a State, so the recorder knows the
size the kernel indexes into and compares the launch with it when the rule is recorded.
launch_refusal() returns an `InvalidGraph` error naming the call, the rule, the launch with its
origin and the size it was compared with, or nothing for a launch that fits. The recorder keeps
the error as its refusal and records nothing.

launch_reach() is the bound compared. A count reaches its own value. A rank-1 Extent or NdRange
reaches origin plus size, because its kernel indexes with the id it is handed. A higher rank
reaches its work-item count: the runtime does not know how the kernel folds an N-D id into the
field, so the count is the largest bound that never refuses a correct graph. A 2-D launch at an
origin whose kernel indexes with the unshifted position still passes this check and is the
caller's to get right.

saturated_reach() computes origin plus size and returns SIZE_MAX when the sum does not fit a
std::size_t. An origin near SIZE_MAX would otherwise wrap to a small reach and pass the
comparison; SIZE_MAX is larger than any handle, so the launch is refused.

A launch that names its data with Reads and Writes or with captures is not compared. A region can
be a window of a buffer and a kernel can stride, so the element count of the handle does not
bound the index the way a State's does.

## emit_*.hpp

### Replay callables

Each emit header builds the replay callable of one node shape: a lambda invoked as
emit(Internal::Graph::TaskGraph&, registration) every time a Recorder replays. The lambda adds one node, hands
the registration through (empty means the backing graph draws a fresh index) and stamps the launch
width where the shape has one. None of them knows the Recorder, so a shape can be read, changed or
added without touching the recording state.

The emitters are generic over the launch: launch_node, untracked_launch_node, captured_node,
state_launch_node and coupled_launch_node each take a count, an Extent or an NdRange and call
submit_launch(). A rule over the whole field has no emitter of its own: the Recorder passes the
State's size as the count.

### Launch width

Every emitter that knows how many work-items its node launches stamps that number on
Node::baked_width, which a profiled run reports as the node's launched width. A width stated
through NodeOptions replaces the one in the table on every shape: NodeStamp::apply() writes it
after the emitter has run.

| Emitter | Width stamped |
| --- | --- |
| launch_node, range_node, raw_node, captured_node, task_node, host_node, device_sized_node, late_count_resolver_node | the width NodeOptions states |
| dynamic_node | the width NodeOptions states, or the capacity |
| add_state_node, add_coupled_node, untracked_launch_node | launch_count() of the launch |
| untracked_range_node | the count |
| dynamic_state_node | the State's size |
| untracked_raw_node | none |

untracked_raw_node() stamps none: a raw command group submits its own work and takes no launch,
so its width is the one NodeOptions states, or unknown.

### task_node

task_node() adds its node with TaskGraph::add_task() and a host-side work only, which is the
engine's form for a body that receives the queue. The engine calls such a node "host" because the
body runs on a worker thread; the node still counts as one that submits device work, so the chain
layer takes it. The body is wrapped in the engine's HostWork explicitly: add_task() has a second
overload that takes a device work first, and a generic lambda would convert to both.

### State emitters

state_launch_node(view, launch, kernel), coupled_launch_node(out_view, views, launch, kernel)
and dynamic_state_node(view, capacity, ...) take the
State's view and no `State<T>*`, so a rule does not depend on where the State object lives. A
State's size does not change after it is allocated, so it is passed by value. The keys of a State
swap at every step, so they are read at each replay, from the view: the read key is the address in
`view->current` and the write key the address in `view->next`. The State keeps its view written
(api/data/README.md), so a replay needs no resync before it.

## live_fields.hpp

### LiveFields and with_live_fields

A State rule that names an operation may be run by something other than the local pool, and
whatever runs it must know which field is current at that step. LiveFields holds two shared
lists of addresses: the current field of every State the rule reads, the one written first, and
the next field of the State written. with_live_fields() wraps the rule's replay callable: after
the callable has added its node, the wrapper hands the lists to the node with
`Node::set_live_io()`. A rule that names no operation carries null lists and its node is left
as the callable added it. The Recorder builds the lists and registers the hook that rewrites them
before each step, because the fields swap at every step.

### untracked_launch_node

An untracked node names no region, so its replay states the device with Node::place_on(). The
device is always stated, device 0 included, so the node's placement does not depend on what the
engine does with a node that states none.

### captured_access

captured_access() turns a list of In, Out and InOut captures into the Access the node is ordered
by. It lives with the captured emitter so that the builder's capture overload only splits its
argument pack.

## host_slot_check.hpp

### host_slot_refusal

host_slot_refusal() asks the slot's own address first. When that answers anything but host, it
resolves the address to the allocation containing it and asks again: a caller offsetting into a plan
array hands over an interior pointer, which SYCL reports as unknown. It runs while add() builds a
node, so the containment search never meets a dispatch. A null slot passes. A slot outside host
USM comes back as an `InvalidAccess` error that names the builder call and the rule;
host_device_slots_refusal() asks it for the count slot and then for the base slot of a Dynamic.

## late_count_resolution.hpp

### late_count_source and read_live_count

A resolver node reads a host count on the host and a device count in its kernel.
late_count_source() runs once per step, when the resolver is submitted. For a device count it copies
the address of the slot and reads nothing. For a host count it calls the provider, and for a fixed
count it takes the capacity. A provider that returns more than the capacity is a fault driven by
run-time data: late_count_source() writes a K_DEVICE_FAULT_LATE_COUNT record and returns nothing,
and the resolver submits an empty kernel in place of the fill. read_live_count() runs in the
kernel: it reads the slot, and a value above the capacity becomes zero and the same record.

Neither validates the LateCount. Its factories checked the capacity, the residency of the slot and
the index when the count was built (api/vocabulary/README.md), so a step compares nothing but the
live count with the capacity.

### gather_device_read

gather_device_read() gives a recorder what a device count adds to its resolver: the region of the
slot's buffer as a read, which orders the resolver after the kernel that writes the count, and the
buffer's liveness observer. It returns whether the count has a device slot, so the recorder knows
whether the resolver's fault record must be host USM a kernel can write.

## device_fault.hpp

### The fault record

A fault record is four words: the kind, the base, the count and the capacity. A node writes its
own record when the data of a step does not fit what the node was planned for, and nothing is
thrown at that point: the writer is a kernel on the device, or the host code that dispatches the
node on a worker thread. Once the step has finished, StepMachinery::take_fault() reads the records
through take_device_fault(), which clears the record and returns an `InvalidGraph` error, and the
step driver returns that error from the run. The States do not advance for a step that faulted.

Every late-bound node and every resolver has a record. One a kernel writes is host USM on the
slot's device; one only host code writes is host memory the recorder owns.

### late_count_overflow

A count over its capacity is found in two places: on the host by late_count_source(), and in the
kernel by read_live_count(). Both write the same record, and late_count_overflow() builds the
error take_device_fault() returns for it, so the two paths report one message.

### saturated_fault_word

A fault record holds uint32 words and the base, count and capacity it reports are std::size_t.
saturated_fault_word() stores a value above UINT32_MAX as UINT32_MAX, so the message never shows
a value modulo 2^32. It is a constexpr comparison with no call, which a device kernel can run.

## late_window.hpp

### LiveRange

A LiveRange is the half-open element window a late-bound node iterates this step: where it starts
and how many elements it covers. The two are resolved and validated together, because only their
combination says whether the node stays inside the buffers it was compiled for.
resolve_live_range() returns nothing for a window that does not fit and writes a
K_DEVICE_FAULT_WINDOW record; submit_late_window() then submits an empty kernel, and the run
returns the fault when the step ends.

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
recorded as {emitter, stamp, rule} and not as a bare callable. The name of a rule lives in the
recorder, which owns the text and hands it to every node of the rule when the plan is built; that
is what lets name() attach one after the add() call has already returned.

## node_stamp.hpp

### NodeStamp

A NodeStamp is the part of a NodeOptions a recorded node keeps. It borrows nothing from the
caller: the name points into the recorder's label set, and the parameters are copied into an
inline array of K_MAX_NODE_PARAMETERS words, so recording a node allocates nothing for them.

apply() writes the stamp on the node an emitter added, once per replay, and writes only what was
stated. An emitter therefore needs no parameter for a name, an operation or a parameter list, and
a shape added later gets all three with no code of its own.

### parameters_refusal

parameters_refusal() returns an `InvalidGraph` error for more parameters than a node carries.
Recorder::record() asks it before it touches the recorder, so a refused node leaves no trace.

## usm_scratch.hpp

### make_usm_scratch

make_usm_scratch() allocates on the device and with the residency the caller names. A rule passes
those of the data it works on, so its nodes never straddle a device context and a device-resident
field is not dragged into host-addressable memory. The returned pointer owns the
allocation through a deleter that keeps the context alive, so scratch cannot outlive the runtime
that owns it. It returns `Result<std::shared_ptr<T>>`, with `AllocationFailed` when the device
returns nothing.

## error_translation.hpp

### translate_exceptions

translate_exceptions() is the one catch ladder of the surface. It runs a callable and returns an
`Error` whose code is `Ok` when the callable returned. A `sycl::exception` takes the code the
caller names for a device failure, and every other exception takes the fallback. The library
throws nothing itself, so what the ladder catches comes from SYCL, the standard library or the
caller's own callable. The ladder is written out in the function that owns the `try`, with
no rethrow, because a rethrow from a helper called inside a catch block crashes the process on
the Windows intel-llvm toolchain. Each `Error` is built by `Detail::error_without_throwing()` of
`core/error.hpp`, which returns the code with an empty message when the text cannot be allocated.
StepDriver::guarded() calls it with `KernelFailed` for a device failure; the factories and
read_range() and write_range() call it too.