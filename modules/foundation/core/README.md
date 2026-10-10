# core

Types every other module uses: errors, results, reference counting, the intrusive pointer, spans,
strong ids, bit helpers, the export macro and the version.

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

## ref_counted.hpp

### RefCounted

The counter starts at 0. Each sushi_ptr copy increments it with relaxed ordering; no synchronisation
is needed, because ownership cannot change without an existing reference. The last release() issues
a release fence before calling delete, so all prior writes are visible to any thread that had access
through an earlier reference.

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

## Implementation

Sources of the `Core` layer that need a translation unit. The headers are in
`include/SushiRuntime/core/`.

### version.cpp

`runtime_version()` and `runtime_version_string()` read the same `SUSHIRUNTIME_VERSION_*`
macros a consumer sees in `version.hpp`, but the values are frozen when the library is
compiled. A consumer that compares its own macros with these two functions is comparing two
builds. They agree only when the binary it loaded is the one its headers describe.
