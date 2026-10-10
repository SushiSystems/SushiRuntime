# api/data

The data handles of the fluent API: Buffer and State.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## buffer.hpp

### Range calls

read_range(), write_range() and region() take an ElementRange, on a Buffer and on a State alike, and
follow one rule: the window `[offset, offset + count)` must lie inside `[0, size())`. A window that
does not fit throws `Core::SushiException` with `Errc::invalid_access`, and the message holds the
call, the offset, the count and the size. Nothing is copied and nothing is clamped, because a
shortened copy hides the wrong count until the missing elements are read somewhere else. The check
subtracts and does not add, so an offset and a count whose sum wraps around are rejected too.

A zero-length window is accepted at every offset up to and including size(), and it touches neither
the device nor the source. write_range() with a non-zero count also throws `invalid_access` when
its source is null or holds fewer than `count` elements; a longer source is accepted and its first
`count` elements are copied.

### Allocation

The byte size of a handle is `count * sizeof(T)`, checked before anything is allocated: a count
whose byte size does not fit `std::size_t` throws `Errc::allocation_failed` and names the largest
count that fits. When the device returns no memory, the message states what was asked for (element
count, element size, bytes, residency and device) and gives no cause, since the allocator reports
none.

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

The memory is shared USM, so the host may index it directly (to seed or read back data) and a device
kernel may capture data() and use it on the device. The handle owns the allocation and frees it on
destruction, so it must outlive every graph run that touches it. A Buffer is move-only.

### Buffer(ctx, count, device_index, residency)

The two constructors that take a runtime context are private. Runtime::buffer() and
Runtime::state() reach the allocating one and Interop reaches the adopting one, through friendship,
so a caller never spells `Execution::RuntimeContext`.

The allocation is pinned to `device_index` and its owner recorded, so buffers used together (a
State's two fields, for example) share a context and a kernel touching them never makes a
cross-context access. `ctx` is the runtime context that owns the allocation, and `residency` is one
of the three kinds the Residency section describes. A Buffer must not be constructed over foreign
USM.

### Buffer(Buffer&&)

The liveness token moves with the allocation, by design: replay and factory patterns (`auto s =
rt.state<T>(n)`, returning a handle, moving one into place before recording it) rely on the token
following the value. The moved-from Buffer is left empty.

A fluent graph records a handle by its object address: State kernels capture `&state` and
dereference it each step. Once a Buffer or State has been passed to `Graph::add(...)` it must not be
moved, nor stored in a container that may reallocate (push_back into a `std::vector<State>` after
recording an element, for example). The graph would keep pointing at the old address while the
object lives at the new one, and because the token travels with the value the liveness guard cannot
detect it. Recorded handles stay fixed for the graph's life.

### operator[]

Device-resident memory is not host-addressable, so indexing it on the host is invalid. operator[]
throws on a device-resident buffer instead of dereferencing into undefined behaviour, which is a
deterministic failure in every build and not only a debug assert. For device residency use
read_range() and write_range(), or host() for a bulk host view. Kernels capture data() directly and
never go through this operator, so the residency check costs nothing on the device path.

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

The overload that takes a `const T*` is deprecated and forwards to the span overload with
`range.count` elements at the pointer.

### Tracking state

The graph layer needs three things from a handle that a caller never does: the address the
dependency tracker keys on, the liveness observer, and the swap of a State's two fields. It reaches
them through `detail::HandleAccess` (api/detail/handle_access.hpp), a friend of both handles.

The key of a Buffer is the allocation's base address, which the tracker treats as the whole-buffer
region. The observer stays valid while the buffer lives and expires when the buffer is destroyed or
moved from. That lets a run detect a buffer freed before it ran and fail cleanly instead of
dispatching against released memory. A handle that owns no allocation has an empty observer.

The members dependency_key(), liveness(), State::read_key(), State::write_key() and State::advance()
forward to HandleAccess and stay for the 0.4 releases. liveness() and advance() are marked
`[[deprecated]]`. The three key members carry no attribute while headers under api/graph and
api/detail, and one distributed test, still call them.

### region

Two tasks declaring regions of the same buffer are serialized only when their ranges overlap, so
disjoint slices (an array's halves, a grid's interior versus its halo) run in parallel. Pass the
result to Reads(...)/Writes(...) in place of the buffer itself. Offsets are in elements; the buffer
keeps the whole-buffer default when used bare. region() returns `API::BufferRegion`, the api
module's name for the byte window of one allocation, and follows the Range calls rule.

### distributed()

distributed() hands the allocation to the offload coordinator installed on the runtime's context,
through Graph::OffloadCoordinator::share(). It is opt-in: only a shared buffer can cross to a worker,
and the master stays authoritative for it. It does nothing on an empty handle, and nothing when no
coordinator is installed, so a buffer is shared only if Distributed::Cluster::attach() ran first.

Two overloads exist to make chaining safe. The lvalue overload (on a named buffer) returns *this.
The rvalue overload (on a temporary returned by Runtime::buffer()) moves the buffer into the caller
so the allocation survives the chain: `auto a = rt.buffer<float>(n).distributed();`

A State\<T> is also distributable (State::distributed()); it shares both of its allocations and
the offload path resolves the step's read/write handles from the live pointers, so the swap is
followed without breaking the version-by-pointer coherence model.

## state.hpp

### StateView

A State kernel captures a shared_ptr to one of these instead of raw pointers. Because the kernel's
host-side body runs once per step (at submit time), it reads cur/next fresh every step. The runtime
updates these two fields when it swaps the State between steps, so one compiled node follows the
double-buffer without being recompiled.

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
next field, so HandleAccess::read_key() and write_key() return those two addresses. A graph that
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
or when no offload coordinator is installed.

### State::read_range

read_range() is the selective-readback seam for a device-resident field. It pulls a window of the
current state for telemetry or IO without host-mapping the whole field, as Buffer::read_range does
for a buffer, under the same Range calls rule.

### State::write_range

write_range() is how to initialise a device-resident State that the host cannot index. It mirrors
Buffer::write_range, with the same rule, the same span source and the same deprecated pointer
overload.
