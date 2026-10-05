# api/data

The data handles of the fluent API: Buffer and State.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## buffer.hpp

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
device-resident field. It is synchronous: the copy is waited on before it returns. The range is
clamped to what the buffer holds.

### write_range

write_range() is the counterpart to read_range() and the way to seed a device-resident buffer the
host cannot index. It is synchronous in the same way, and the range is clamped to what the buffer
holds.

### dependency_key

Graph::add() passes dependency_key() to the dependency tracker as the read/write key. It is the
allocation's base address, which the tracker treats as the whole-buffer region.

### liveness

A graph that records this buffer keeps the observer liveness() returns. The observer stays valid
while the buffer lives and expires when the buffer is destroyed or moved from. That lets a run
detect a buffer freed before it ran and fail cleanly instead of dispatching against released memory.
A handle that owns no allocation returns an empty observer.

### region

Two tasks declaring regions of the same buffer are serialized only when their ranges overlap, so
disjoint slices (an array's halves, a grid's interior versus its halo) run in parallel. Pass the
result to Reads(...)/Writes(...) in place of the buffer itself. Offsets are in elements; the buffer
keeps the whole-buffer default when used bare.

### distributed()

distributed() is opt-in: only a registered buffer can cross to a worker, and the master stays
authoritative for it. It does nothing on an empty handle.

Two overloads exist to make chaining safe. The lvalue overload (on a named buffer) returns *this.
The rvalue overload (on a temporary returned by Runtime::buffer()) moves the buffer into the caller
so the allocation survives the chain: `auto a = rt.buffer<float>(n).distributed();`

A State\<T> is also distributable (State::distributed()); it registers both of its allocations and
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
current() and next(), and the runtime swaps their roles after each step (advance()), an O(1) pointer
exchange. The user writes one step as "read current, write next" and never manages the ping-pong.

### State(ctx, count, device_index, residency)

Both fields land on `device_index`, so a step kernel that reads current and writes next stays within
one context. `ctx` is the runtime context that owns the allocations.

### liveness

State::liveness() mirrors Buffer::liveness(): a graph that records this State holds the observer to
detect a State freed before a run. The current field's token stands for the State, because both
fields are freed together with it. An empty State returns an empty observer.

### distributed()

State::distributed() is opt-in and mirrors Buffer::distributed(). It exposes each of the State's two
allocations to the cluster so a stepped graph can offload its update rule. The two allocations keep
stable handles for the State's lifetime; only which one is read and which is written rotates each
step, and the offload path follows that from the live pointers. It does nothing on an empty State.

### State::read_range

read_range() is the selective-readback seam for a device-resident field. It pulls a window of the
current state for telemetry or IO without host-mapping the whole field, as Buffer::read_range does
for a buffer.

### State::write_range

write_range() is how to initialise a device-resident State that the host cannot index. It mirrors
Buffer::write_range.
