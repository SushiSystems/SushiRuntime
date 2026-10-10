# api/vocabulary

The data-flow vocabulary of the fluent API: Reads and Writes, the capture wrappers, Coupled, On,
Dynamic and LateCount.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## captures.hpp

### In

In wraps a Buffer so add() can both register the read and hand the kernel the raw element pointer.
The caller writes the data flow once and gets pointer capture and dependency ordering together. Out
and InOut are the same wrapper for the other two access intents.

## coupled.hpp

### Coupled

Coupled(out, in0, ...) names the States of one update rule: the first is written, the others are
read. add(Coupled(out, in0), fn) hands the kernel the current and next field of `out`, then the
current field of each State read, in argument order. Every read sees the previous step's data,
because a State advances only after all of a step's kernels have run.

Up to v0.3 the same rule was written add(out, in0, ..., fn). Two States side by side said nothing
about which one is written, and the form had no place for a launch. Coupled is one access
descriptor, like Reads with Writes or a list of captures, so it sits after the launch the way they
do: add(Extent, Coupled(u, v), fn) runs a coupled rule over a sub-box.

Coupled holds references and no copies. A rule over one State is add(state, fn), so a Coupled with
nothing to read does not compile.

## placement.hpp

### On

A kernel added with add_untracked() names no region, so the runtime cannot read its device from
its data. On states it. On(buffer) and On(state) take the device of the handle and keep its
liveness observer, so a graph whose handle was destroyed is refused with `dangling_resource`
like any other. On(DeviceIndex) names the device outright and watches nothing.

Without an On the kernel runs on DeviceIndex{0}, the default device of Runtime::buffer() and
Runtime::state(). A default buffer and a default untracked kernel therefore meet on one device.
Up to v0.3 an untracked kernel went to whichever device a rotation reached, which on a machine
with two devices could be one that did not own the pointers the kernel had captured.

On orders nothing. A kernel that must run before or after another names its data with Reads and
Writes and is added with add().

## dependencies.hpp

### read_key_of and write_key_of

read_key_of() and write_key_of() pull the read or write dependency region out of either data handle.
A Buffer maps to its whole-buffer region; a State exposes its current buffer as the read region and
its next buffer as the write region. These free functions let the Reads/Writes wrappers accept
handles uniformly. An explicit ResourceRegion, such as buf.region({off, len}), passes through, so a
caller can name a sub-range to read or write.

### liveness_of

liveness_of() returns the liveness observer of a tracked handle. A Buffer or State yields its token
observer; a bare ResourceRegion (a raw sub-range the caller manages) yields an empty observer, so
only typed handles are watched.

### gather_life

gather_life() appends a handle's observer only when it actually watches an allocation. Empty
observers (raw regions) never enter the list, so a later expired() test means a once-live handle was
freed, not "never tracked".

## dynamic.hpp

### Dynamic

A Dynamic is passed as the leading argument to add() to make a node's iteration size or its
participation vary every step without recompiling the graph. when() gates whether the node runs at
all (a sleeping island, an LOD-disabled system); sized() sets how many elements it iterates, which
must stay within the capacity its buffers were allocated for. The two compose as
when(pred).sized(count). Each provider is polled once per step, on the driver thread before the plan
replays, so the compiled topology is reused across steps and the graph compiles exactly once.

The providers are stored in allocation-free Core::Delegate slots, so a Dynamic is move-only (a
stored provider has one owner). A caller builds one from a when()/sized() temporary and passes it
straight to add(), or moves a named Dynamic into the call with std::move.

The controls are private. A caller sets them through the members and the free builders, and the
recording code reads them through detail::DynamicAccess, so the way a Dynamic stores a control can
change without a caller or an emitter noticing.

### Members and free builders

Each control has one member, named after what it sets: when(), sized(), sized_from_device(),
based_at(), based_at_device(). The free function of the same name builds an empty Dynamic and calls
the member, so `when(p).sized(c)` and `Dynamic d; d.when(p).sized(c);` run the same code and a
device slot passes the same check on both paths.

The members are ref-qualified so a move-only Dynamic chains in both shapes. On a named Dynamic the
lvalue overload returns a reference. Inline, as in when(p).sized(c), the rvalue overload calls the
lvalue one and returns by value, so the result moves straight into add()'s by-value parameter.

The free builders are `[[nodiscard]]`: a Dynamic that is built and dropped does nothing. The members
are not, because a chain on a named Dynamic drops its last reference on purpose.

### One source per control

The live size has two possible sources, a host provider and a device slot, and so does the base.
The member that sets one source clears the other, so the source set last decides. Up to v0.3,
and_sized() after sized_from_device() kept both and the device slot won without saying so.

### checked_device_slot and DeviceSlot

checked_device_slot() is the one check a device slot passes, for sized_from_device(),
based_at_device() and LateCount::from_device() alike: the buffer is Residency::Host and the index
is inside it. It returns a DeviceSlot, the buffer's first word and the index, which is what
Graph::LateBindings stores. The members take the Buffer and never a bare pointer, because a pointer
carries neither a residency nor a size to check.

### and_when, and_sized, and_based_at

The three `and_` members are deprecated in v0.4.0 and forward to when(), sized() and based_at().
and_sized_from_device(pointer, index) and and_based_at_device(pointer, index) are removed: they took
a bare pointer and skipped the check above.

### sized_from_device

sized() answers "how many elements does the host already know we want", polled at the step boundary.
sized_from_device() answers the question a producer and consumer chain actually asks: how many did
the device just produce. A broadphase writes its pair count and the narrowphase must iterate exactly
that many. That number does not exist yet when the step boundary runs, so a host-side provider
cannot supply it without a mid-step readback or a second run.

The count is instead read when the node is dispatched, which is after every predecessor's event has
completed, so a value a predecessor kernel wrote is visible. The task that consumes the count is
automatically ordered after whoever writes `counter`, because add() registers `counter` as one of
its reads. The ordering is structural, not a note the caller must remember.

Two constraints are enforced in code. `counter` must be Residency::Host, and sized_from_device()
checks it. The live count must not exceed the capacity the node was compiled for; dispatch checks
that, and exceeding it fails the run instead of silently truncating the work.

sized_from_device() replaces any sized() provider on the same Dynamic, and sized() replaces the
device slot; when() still composes.

### based_at

sized() says how many elements a node iterates; based_at() says from where. A graph-coloured solver
needs the pair: each colour is a contiguous slice of one constraint buffer, and every colour's
offset shifts as soon as constraints are added or removed. Without based_at() the kernel has to
reach its own work through an index-indirection buffer, paying an extra load per element for
something the launch could have carried for free.

The offset is applied by the runtime, so the callable still receives absolute indices: `fn(base +
i)` for i in [0, count). The window must fit inside the capacity the node was compiled for; one that
does not fails the run instead of reading past the end.

based_at() composes with when() and with sized() or sized_from_device().

### based_at_device

based_at_device() is the device-sourced form of based_at() and the companion of sized_from_device().
A colouring pass that computes both a slice's offset and its length on the device hands over both
the same way, and the slot is registered as one of the consuming task's reads so the producer is
necessarily ordered first.

It replaces any based_at() provider on the same Dynamic, and based_at() replaces the device slot;
when() and the size controls still compose.

## late_count.hpp

### LateCount

A LateCount names a count that is planned for its capacity when it is recorded and resolved once per
step; the live count must not exceed the capacity. Recorder::resolve() records the node that reads
it.

It has three factories and no public constructor, so a LateCount has exactly one source:

| Factory | Live count | Read |
| --- | --- | --- |
| `fixed(count)` | `count` | never; it is the capacity |
| `from_host(capacity, provider)` | what the provider returns | on the host, when the resolver is submitted |
| `from_device(capacity, slot, index)` | the word `slot[index]` | in the resolver's kernel, after its producer |

Each factory checks its arguments where it is called. A capacity past the largest uint32 throws
`invalid_graph`, because a live count is published as a uint32 word. from_host() throws
`invalid_access` for an empty provider. from_device() runs checked_device_slot(), so a slot outside
Residency::Host or an index past the buffer throws `invalid_access` before any graph sees the count.

### LateCount::Provider

Provider is Dynamic::CountProvider, a Core::Delegate with 48 bytes of inline storage. A callable is
stored without a heap allocation and a capture larger than 48 bytes fails to compile.

A Delegate is move-only, and a LateCount must copy: Recorder::resolve() takes its counts in a
std::array, and the resolver's replay callable copies that array each time the plan is built. So
from_host() moves the provider into one shared block, and every copy of the LateCount points at it.
That is one allocation per from_host() call and none per step. A step pays one pointer load and one
indirect call for a host count, and neither for a fixed or a device count.

### What from_device keeps

from_device() keeps the address of the word, the dependency key of the buffer and the buffer's
liveness observer, and no pointer to the Buffer handle. The resolver reads the buffer, so it is
ordered after the kernel that writes the count, and a graph whose slot buffer was destroyed is
refused with `dangling_resource` like any other.

### LateCountAccess

detail::LateCountAccess reads the source of a LateCount for the recording code, as
detail::DynamicAccess does for a Dynamic.

## op_literal.hpp

### The _op literal

op_literal.hpp promotes `Graph::Literals::operator""_op` into `SushiRuntime`. After
`using namespace SushiRuntime;` or `using SushiRuntime::operator""_op;` a program writes
`"vec.add"_op` for the OpID of a registered kernel. `distributed/distributed.hpp` includes this
header, so a worker program that never includes the fluent API has the literal too.
