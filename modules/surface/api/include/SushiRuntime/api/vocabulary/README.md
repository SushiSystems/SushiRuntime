# api/vocabulary

The data-flow vocabulary of the fluent API: Reads and Writes, the capture wrappers, Dynamic and
LateCount.

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## captures.hpp

### In

In wraps a Buffer so add() can both register the read and hand the kernel the raw element pointer.
The caller writes the data flow once and gets pointer capture and dependency ordering together. Out
and InOut are the same wrapper for the other two access intents.

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
when(pred).and_sized(count). Each provider is polled on the driver thread before the plan replays,
so the compiled topology is reused across steps and the graph compiles exactly once.

The providers are stored in allocation-free Core::Delegate slots, so a Dynamic is move-only (a
stored provider has one owner). A caller builds one from a when()/sized() temporary and passes it
straight to add(), or moves a named Dynamic into the call with std::move.

### Ref-qualified chaining setters

The chaining setters are ref-qualified so a move-only Dynamic chains in both shapes. Mutating a
named Dynamic in place uses the lvalue overload, which returns a reference. Building one inline, as
in when(p).and_sized(c), uses the rvalue overload, which returns by value so the result moves
straight into add()'s by-value parameter.

### and_sized_from_device

and_sized_from_device() drops any host-side sized() provider, since the two cannot both decide the
iteration size. The sized_from_device section gives the contract.

### and_based_at_device

and_based_at_device() drops any host-side based_at() provider, since the two cannot both decide
where the node starts.

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

sized_from_device() replaces any sized() provider on the same Dynamic; when() still composes.

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

It replaces any based_at() provider on the same Dynamic; when() and the size controls still compose.

## late_count.hpp

### LateCount

A LateCount names a count that is planned for `capacity` when it is recorded and read once per run;
the live count must not exceed the capacity. Recorder::resolve() is what reads it. Leaving `host` empty and `device` null makes the live count `capacity` itself, matching a
fixed-count call. `host` takes precedence over `device` when both are set, which callers should not
do; exactly one should be set.

A fixed-count call spells its plain capacity as `LateCount lc; lc.capacity = n;`. Plain brace init
(`LateCount{n}`) trips `-Wmissing-field-initializers`, and this type stays an aggregate, so it takes
no constructor. The designator form (`LateCount{.capacity = n}`) reads better but is a C++20
feature. On the non-MSVC compiler path this tree builds with (clang++, `-pedantic -Werror`,
`cmake/CompilerWarnings.cmake`), it fails as `-Wc++20-designator`, confirmed against that compiler,
so it is not offered as the idiom.
