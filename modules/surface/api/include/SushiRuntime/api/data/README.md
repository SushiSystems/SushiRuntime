# api/data

The data handles of the fluent API: Buffer and State.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## buffer.hpp

### Range calls

read_range(), write_range() and region() take an ElementRange, on a Buffer and on a State alike, and
follow one rule: the window `[offset, offset + count)` must lie inside `[0, size())`. A window that
does not fit throws `SushiException` with `Errc::invalid_access`, and the message holds the
call, the offset, the count and the size. Nothing is copied and nothing is clamped, because a
shortened copy hides the wrong count until the missing elements are read somewhere else. The check
subtracts and does not add, so an offset and a count whose sum wraps around are rejected too.

A zero-length window is accepted at every offset up to and including size(), and it touches neither
the device nor the source. write_range() with a non-zero count also throws `invalid_access` when
its source is null or holds fewer than `count` elements; a longer source is accepted and its first
`count` elements are copied.

### Allocation

A device index that names no device of the runtime throws `Errc::invalid_access` before anything is
allocated. The message names the calling function (`Runtime::buffer` or `Runtime::state`), the
index, the device count, and `advanced().devices()`, which lists the devices by index.

The byte size of a handle is `count * sizeof(T)`, checked before anything is allocated: a count
whose byte size does not fit `std::size_t` throws `Errc::allocation_failed` and names the largest
count that fits. When the device returns no memory, the message states what was asked for (element
count, element size, bytes, residency and device) and gives no cause, since the allocator reports
none.

### Argument types

`DeviceIndex` is a strong typedef over `StrongID`, so a device selector cannot be confused
with an element count or an offset at a call site. Its constructor is explicit, and a swapped
argument is a compile error rather than a silent allocation on the wrong device, so
`buffer(count, DeviceIndex{1})` reads unambiguously.

`ElementRange` bundles an offset and a count into one named value. A caller cannot pass them in the
wrong order, which a pair of bare `std::size_t` arguments invites. Its counts are in elements, not
bytes.

### Element type

`Buffer<T>` copies its elements with `memcpy` on the device path, so `T` must be trivially copyable.
The class states this with a `static_assert`, and `State<T>` inherits it through its two buffers. A
struct of plain members, such as two floats, satisfies it; `std::string` and `std::vector` do not.

### Residency

Shared USM is host-addressable, so the host may seed and read it directly, but at large element
counts the driver may migrate it between host and device. Device USM stays resident on its device,
which is the right choice for a world kept on the GPU across frames. It is not host-addressable, so
the host touches it only through read_range() and write_range(). Host USM is pinned host memory that
device kernels on the owning context write and the host reads at any moment, including while kernels
run. Kernels reach host USM across the bus and it is page-locked RAM, so it suits small values the
host reads mid-run, not bulk kernel data. The handle API is otherwise identical; residency is chosen
once at allocation.

### Buffer

A Buffer is the fluent API's unit of data: a typed USM allocation that the runtime tracks by owning
queue, so a task touching it is dispatched to the device that holds it. Passing a Buffer to
Graph::add() is what lets the runtime register the task's reads and writes automatically, because
the handle's address is the identity the dependency tracker keys on.

The host may index a Shared or Host buffer directly, to seed or read back data. It may not index a
Device buffer, which goes through read_range() and write_range(). A device kernel may capture data()
and use it on the device for all three residencies. The handle owns the allocation and frees it on
destruction, so it must outlive every graph run that touches it. A Buffer is move-only.

### Buffer(ctx, count, device_index, residency)

The two constructors that take a runtime context are private. Runtime::buffer() and
Runtime::state() reach the allocating one and Interop reaches the adopting one, through friendship,
so a caller never spells `Internal::Execution::RuntimeContext`.

The allocation is pinned to `device_index` and its owner recorded, so buffers used together (a
State's two fields, for example) share a context and a kernel touching them never makes a
cross-context access. `ctx` is the runtime context that owns the allocation, and `residency` is one
of the three kinds the Residency section describes. A Buffer must not be constructed over foreign
USM.

### Buffer(Buffer&&)

The liveness token moves with the allocation, by design: replay and factory patterns (`auto s =
rt.state<T>(n)`, returning a handle, moving one into place before recording it) rely on the token
following the value. The moved-from Buffer is left empty.

A graph records no handle by its object address, so a Buffer or a State may be moved after
`Graph::add(...)` recorded it, into a new variable or by a `std::vector` that reallocates. What each
form of add() keeps is listed here.

| Named in add() as | The graph keeps | After a move |
| --- | --- | --- |
| a Buffer in `Reads`, `Writes`, `In`, `Out` or `InOut` | the allocation's address and the observer, by value | the node runs as recorded |
| a Buffer or a State in `On` | the device index and the observer, by value | the node runs as recorded |
| a Buffer in `sized_from_device` or `based_at_device` | the slot's address and the observer, by value | the node runs as recorded |
| a State in `Reads` or `Writes` | the address of one field as it stood at add(), and the observer | the node runs as recorded; the State::region section says which field that address is after a step |
| a State as the rule's own State, or in `Coupled` | the State's StateView, shared | the rule steps the moved-to State |

Two things still end a recording. A handle destroyed or move-assigned over releases its token, and
the next run is refused with `dangling_resource`. A State must not be moved while a step that names
it is running or pending: the step advances the State through the view's `owner` when it ends.

### operator[]

Device-resident memory is not host-addressable, so indexing it on the host is invalid. operator[]
throws on a device-resident buffer instead of dereferencing into undefined behaviour, which is a
deterministic failure in every build and not only a debug assert. For device residency use
read_range() and write_range(), or host() for a bulk host view. Kernels capture data() directly and
never go through this operator, so the residency check costs nothing on the device path.
State::operator[] and State::host() run the same check under their own names, so the message names
the State the caller holds.

### host

host() is the branch-free bulk seam for host code. The residency is checked once, inside host(), and
the returned span indexes with no further checks, so a tight loop can seed or read back without
paying per-element validation. It is valid only for shared or host USM. A device-resident buffer
throws, since its memory is not host-addressable; read_range() and write_range() serve that case.

### read_range

read_range() is the selective-readback seam. It copies just [offset, offset+count) through the
owning queue into a host vector, for telemetry, IO or a debug window, without host-mapping a whole
device-resident field. It is synchronous: the copy is waited on before it returns. A window that
does not fit throws, as the Range calls section states.

### write_range

write_range() is the counterpart to read_range() and the way to seed a device-resident buffer the
host cannot index. It is synchronous in the same way and follows the Range calls rule. The source
is a span, so a vector or an array is passed as it is and the call can compare the source's length
with the window. write_range() is const: it changes the elements, not the handle.

### Tracking state

The graph layer needs four things from a handle that a caller never does: the address the
dependency tracker keys on, the liveness observer, the view of a State and the swap of its two
fields. It reaches them through `detail::HandleAccess` (api/detail/handle_access.hpp), a friend of
both handles.

The key of a Buffer is the allocation's base address, which the tracker treats as the whole-buffer
region. The observer stays valid while the buffer lives. It expires when the buffer is destroyed,
or when another buffer is move-assigned over it, because the assignment releases the token. A move
construction hands the token to the new handle, so the observer stays valid across a move. That lets
a run detect a buffer freed before it ran and fail cleanly instead of dispatching against released
memory. A handle that owns no allocation has an empty observer.

### region

Two tasks declaring regions of the same buffer are serialized only when their ranges overlap, so
disjoint slices (an array's halves, a grid's interior versus its halo) run in parallel. Pass the
result to Reads(...)/Writes(...) in place of the buffer itself. Offsets are in elements; the buffer
keeps the whole-buffer default when used bare. region() returns `BufferRegion`, the api
module's name for the byte window of one allocation, and follows the Range calls rule.

### distributed()

distributed() hands the allocation to the offload coordinator installed on the runtime's context,
through Internal::Graph::OffloadCoordinator::share(). It is opt-in: only a shared buffer can cross to a worker,
and the master stays authoritative for it. It does nothing on an empty handle, and nothing when no
coordinator is installed, so a buffer is shared only if Distributed::Cluster::attach() ran first.
A handle that owns memory and is device-resident throws `Errc::invalid_access` before the
coordinator is consulted, with or without a cluster attached. The offload path reads the allocation
on the host, so the handle must be Shared or Host.

Two overloads exist to make chaining safe. The lvalue overload (on a named buffer) returns *this.
The rvalue overload (on a temporary returned by Runtime::buffer()) moves the buffer into the caller
so the allocation survives the chain: `auto a = rt.buffer<float>(n).distributed();`

A State\<T> is also distributable (State::distributed()); it shares both of its allocations and
the offload path resolves the step's read/write handles from the live pointers, so the swap is
followed without breaking the version-by-pointer coherence model.

## state.hpp

### StateView

A StateView is what a graph holds of a State: the base pointers of the current and the next
field, the element count, and the address of the State that owns the view. A State that owns memory
creates one view when it is allocated and shares it, through a shared_ptr, with every graph that
records the State. A rule's replay reads the two pointers as its read key and its write key, and a
kernel's host-side body reads them at each submit, so one compiled node follows the double buffer
without being recompiled.

The State writes the view and nothing else does. It writes it when it is allocated, when its fields
are swapped after a step, when a run resyncs, and when the State is moved, assigned over or
destroyed. A move hands the view to the moved-to State and changes only `owner`: the allocations do
not move, so the pointers a graph reads stay valid. A State that is destroyed or assigned over
leaves its old view with null pointers, a size of zero and no owner, so a graph that still holds it
reads an empty view; the liveness observer is what refuses the run.

The State's own accessors do not read the view. operator[], host(), current(), next(), size() and
the range calls read the two Buffer members directly, so the view adds no indirection to an element
access. It is written at most once per step.

### State(State&&)

A State is move-only and declares its moves, because its view names the State's address. The
moved-to State takes both fields, the liveness tokens and the view. The moved-from State owns no
memory and holds no view; it may be assigned to or destroyed. An empty State has no view of its
own, and HandleAccess::view() returns a new empty one for it, so a rule recorded on an empty State
runs over nothing, and still does after the State is assigned to.

### State

Many stepped workloads (stencils, PDE solvers, the digital-twin driver) must read the current state
and write the next one; they cannot update in place. A State owns two equally sized buffers,
current() and next(), and the runtime swaps their roles after each step, an O(1) pointer exchange.
The user writes one step as "read current, write next" and never manages the ping-pong.

A State answers every query a Buffer does: size(), empty(), device_index(), residency(),
is_host_accessible(), operator[], host(), read_range(), write_range(), region() and distributed().
Each one acts on the current field. data() is absent because a State has two buffers; current() and
next() take its place.

### State(ctx, count, device_index, residency)

Both fields land on `device_index`, so a step kernel that reads current and writes next stays within
one context. `ctx` is the runtime context that owns the allocations. The constructor is private and
Runtime::state() is its caller.

### Tracking state of a State

A task that reads a State depends on the current field and a task that writes it depends on the
next field, so HandleAccess::read_key() and write_key() return those two addresses. A rule recorded
on the State reads the same two addresses from the State's view at every replay, so a plan rebuilt
after an odd number of steps is ordered against the fields as they stand then. A graph that
records a State holds the current field's observer to detect a State freed before a run; that token
stands for the State because both fields are freed together with it. An empty State has an empty
observer.

### State::region

region() names a window of the current field. Which field is current changes at every step, so a
region taken before a run names the other field after an odd number of steps.

### distributed()

State::distributed() is opt-in and mirrors Buffer::distributed(). It exposes each of the State's two
allocations to the cluster so a stepped graph can offload its update rule. The two allocations keep
stable handles for the State's lifetime; only which one is read and which is written rotates each
step, and the offload path follows that from the live pointers. It does nothing on an empty State
or when no offload coordinator is installed. A device-resident State throws `Errc::invalid_access`,
as Buffer::distributed() does, and the message names `State::distributed()`.

### State::read_range

read_range() is the selective-readback seam for a device-resident field. It pulls a window of the
current state for telemetry or IO without host-mapping the whole field, as Buffer::read_range does
for a buffer, under the same Range calls rule.

### State::write_range

write_range() is how to initialise a device-resident State that the host cannot index. It mirrors
Buffer::write_range, with the same rule and the same span source.
