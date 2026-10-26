# api/data

The data handles of the fluent API: Buffer and State.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## buffer.hpp

### Range calls

read_range(), write_range() and region() take an ElementRange, on a Buffer and on a State alike, and
follow one rule: the window `[offset, offset + count)` must lie inside `[0, size())`. A window that
does not fit is a rule the caller broke, so the call stops the process through `SR_VERIFY`, and the
line the handler writes holds the call, the offset, the count and the size. Nothing is copied and
nothing is clamped, because a shortened copy hides the wrong count until the missing elements are
read somewhere else. The check subtracts and does not add, so an offset and a count whose sum wraps
around stop the call too.

A zero-length window is accepted at every offset up to and including size(), and it touches neither
the device nor the source. write_range() with a non-zero count also stops the process when its
source is null or holds fewer than `count` elements; a longer source is accepted and its first
`count` elements are copied.

### Allocation

A device index that names no device of the runtime stops the process before anything is
allocated. The message names the calling function (`Runtime::buffer` or `Runtime::state`), the
index, the device count, and `advanced().devices()`, which lists the devices by index.

An alignment that is neither 0 nor a power of two stops the process the same way, and the message
names the calling function and the value.

The byte size of a handle is `count * sizeof(T)`, checked before anything is allocated: a count
whose byte size does not fit `std::size_t` stops the process, and the message names the largest
count that fits. Both are rules the caller keeps, checked with `SR_VERIFY`; running out of device
memory is not one, and `Runtime::buffer()` and `Runtime::state()` return it as an
`ErrorCode::AllocationFailed` error. When the device returns no memory,
the message states what was asked for (element count, element size, bytes, residency and
device) and gives no cause, since the allocator reports
none.

### MemoryBlock

A Buffer holds its memory as a `Detail::MemoryBlock`, which holds one `Detail::Allocation` from
the `core` module by value: the base address, the byte count and the `Detail::AllocationOwner`
that releases the memory. Both are move-only, so the Buffer is the one owner and no shared count
exists. The header names nothing of how the memory was made. `Detail::allocate_block()` of
`source/buffer.cpp` allocates USM through the runtime with a `USMAllocationOwner`; Interop moves
the `Allocation` the engine returns for memory it exported or imported into the block. data(),
operator[] and host() read the allocation's address and the size and call nothing.

A window copy is the other call into the library: `Detail::copy_to_host()` and
`Detail::copy_to_device()` find the queue that owns the allocation, copy, wait and count the
bytes, and return a `Result<void>`. The vector a read fills is sized in the header, where the
element type is known.

Buffer::adopt() is the one place a Buffer is built over a block. It takes the block by value, so
the caller moves its allocation in and a refused adoption releases it. It runs the constructor inside
`Detail::translate_exceptions()`, because the constructor allocates the liveness token, and
returns `AllocationFailed` when that allocation fails. Buffer::allocate() and the two Interop
calls end there, so no exception leaves any of the three.

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

A Buffer is built in one of three private ways. Runtime::buffer() and Runtime::state() reach
allocate(), Interop::export_host() and Interop::import_device() reach the adopting constructor,
and Interop::borrow() reaches the borrowing constructor, through friendship. All three take a
`ContextHandle`, so no member of Buffer names `Internal::Execution::RuntimeContext`.

The allocation is pinned to `device_index` and its owner recorded, so buffers used together (a
State's two fields, for example) share a context and a kernel touching them never makes a
cross-context access. `context` is the handle of the runtime that owns the allocation, and `residency` is one
of the three kinds the Residency section describes. Only Interop::borrow() builds a Buffer over
memory the runtime did not allocate; the next section says what that Buffer holds.

### Borrowed Buffer

A borrowed Buffer views memory another owner keeps alive. Its block's allocation has no owner, so
destroying the handle frees nothing, and it holds no liveness token of its own. It keeps an observer of the
token its owner passed to Interop::borrow() instead, and Detail::HandleAccess::liveness() hands a
graph that observer. A run that names the buffer after the owner released its token is refused with
`DanglingResource`; a run after the borrowed handle alone was destroyed runs as recorded, because
the memory is still the owner's. An owner token that has expired when the buffer is recorded is
not watched, as an empty handle is not.

The runtime's allocator does not record borrowed memory, so a task over it runs on the device its
placement states, and read_range() and write_range() copy on the queue of the buffer's
device_index(). The Buffer moves like an owning one: the observer moves with the memory.

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

Two things still end a recording. An owning handle destroyed or move-assigned over releases its
token, and so does the owner of borrowed memory; the next run is refused with `DanglingResource`. A State must not be moved while a step that names
it is running or pending: the step advances the State through the view's `owner` when it ends.

### operator[]

Device-resident memory is not host-addressable, so indexing it on the host is invalid. operator[]
checks the residency with `SR_ASSERT`, because it runs once per element: where
`SR_ENABLE_ASSERTIONS` is 1 it stops the process on a device-resident buffer, and elsewhere the
check is compiled out. For device residency use read_range() and write_range(), or host() for a
bulk host view. Kernels capture data() directly and never go through this operator, so the check
costs nothing on the device path. State::operator[] runs the same check under its own name, so the
message names the State the caller holds.

### host

host() is the branch-free bulk seam for host code. The residency is checked once, inside host(),
with `SR_VERIFY`, and the returned span indexes with no further checks, so a tight loop can seed or
read back without paying per-element validation. It is valid only for shared or host USM. On a
device-resident buffer it stops the process in every build; read_range() and write_range() serve
that case. State::host() names the State in its message.

### read_range

read_range() is the selective-readback seam. It copies just [offset, offset+count) through the
owning queue into a host vector, for telemetry, IO or a debug window, without host-mapping a whole
device-resident field. It is synchronous: the copy is waited on before it returns. A window that
does not fit stops the process, as the Range calls section states. It returns
`Result<std::vector<T>>`: a copy the device fails is a `KernelFailed` error, and a host that
cannot hold the vector is `AllocationFailed`.

### write_range

write_range() is the counterpart to read_range() and the way to seed a device-resident buffer the
host cannot index. It is synchronous in the same way and follows the Range calls rule. The source
is a span, so a vector or an array is passed as it is and the call can compare the source's length
with the window. write_range() is const: it changes the elements, not the handle. It returns
`Result<void>`, with `KernelFailed` for a copy the device fails.

### Tracking state

The graph layer needs four things from a handle that a caller never does: the address the
dependency tracker keys on, the liveness observer, the view of a State and the swap of its two
fields. It reaches them through `Detail::HandleAccess` (api/detail/handle_access.hpp), a friend of
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

## source/context_memory.hpp

A private header of `source/`.

### usm_kind_of

`Detail::usm_kind_of()` is the one place the api module asks what kind of USM an address is.
Memory this runtime allocated answers from its own record: `USMAllocator` keeps the `USMKind` of
every allocation it makes, so a runtime-made first address costs one hash probe and no SYCL call.
Only memory with no recorded kind reaches `sycl::get_pointer_type`, which answers only for the
context it is given, so the function picks the context by one rule. An address adopted with
no kind is recorded with its device only, and is asked in that device's context. Unrecorded memory is
asked in the context of the device the caller states, which is what Interop::borrow() does for
memory another API made. With no stated device the allocator's `owning_device()` scan picks the
context, which is what the late-bound slot check does for an interior pointer. A
`sycl::exception` from the query reads as `unknown`, and the function logs it at `DEBUG` under
`sushiruntime.api` with the address, the device and the exception text.

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
is_host_accessible(), operator[], host(), read_range(), write_range() and region().
Each one acts on the current field. data() is absent because a State has two buffers; current() and
next() take its place.

### State(ctx, count, device_index, residency)

Both fields land on `device_index`, so a step kernel that reads current and writes next stays within
one context. `context` is the runtime context that owns the allocations. The constructor is private and
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

### State::read_range

read_range() is the selective-readback seam for a device-resident field. It pulls a window of the
current state for telemetry or IO without host-mapping the whole field, as Buffer::read_range does
for a buffer, under the same Range calls rule.

### State::write_range

write_range() is how to initialise a device-resident State that the host cannot index. It mirrors
Buffer::write_range, with the same rule and the same span source.
