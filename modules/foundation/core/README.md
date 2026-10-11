# core

Types every other module uses: assertions, errors, results, reference counting, the intrusive pointer, spans,
strong ids, bit helpers, the export macro and the version.

It depends on the C++ standard library and on no other module of the repository.

## Public entry points

The twelve public headers under `include/SushiRuntime/core/`:

| Header | What it declares |
| --- | --- |
| `assert.hpp` | `SR_ASSERT`, `SR_VERIFY` and `SR_FATAL`, `AssertionFailure`, `AssertionHandler`, `set_assertion_handler()` and `fail_assertion()` |
| `bit.hpp` | `popcount_u64` and `countr_zero_u64`, two 64-bit bit queries for the C++17 baseline |
| `common.hpp` | The `Backend` enumeration and the size constants `K_CACHE_LINE_SIZE`, `K_MAX_SUPPORTED_XPU`, `K_MAX_NUMA_NODES`, `K_MAX_WORKER_THREADS` and `K_DEFAULT_ALIGNMENT` |
| `delegate.hpp` | `Delegate`, a move-only callable stored inline, with no heap allocation |
| `error.hpp` | `Errc`, its `to_string` overload, `Error` and `SushiException` |
| `export.hpp` | `SUSHIRUNTIME_API`, `SR_API` and `SR_TYPE_API`, the symbol visibility macros |
| `ref_counted.hpp` | `RefCounted`, the intrusive reference count that `sushi_ptr` manages |
| `result.hpp` | `Result<T, E>`, which holds a value or an error, and `ErrorWrapper` with `make_error` |
| `span.hpp` | `span`, a non-owning view of contiguous elements, with conversions to and from `std::span` from C++20 |
| `strong_id.hpp` | `StrongID`, an integer identifier that is distinct per tag, with a `std::hash` specialisation |
| `sushi_ptr.hpp` | `sushi_ptr` and `make_sushi`, the intrusive smart pointer and its factory, with a `std::hash` specialisation |
| `version.hpp` | The version macros, `Version`, `runtime_version()`, `runtime_version_string()`, `version_matches()` and `require_matching_version()` |

The sections below record why the code is the way it is. Each one is named after a header and the
symbol or passage it explains, and that header's file block cites this page.

## assert.hpp

### SR_ASSERT, SR_VERIFY and SR_FATAL

The three macros are for a rule the caller broke, which no return value can report usefully.
`SR_VERIFY` checks its condition in every build. `SR_ASSERT` checks it where
`SR_ENABLE_ASSERTIONS` is 1, which is the default without `NDEBUG`; elsewhere the condition is
not evaluated. `SR_FATAL` has no condition. A check on a path that runs once per recorded node
or once per call is an `SR_VERIFY`; `SR_ASSERT` is for a check inside a loop over elements or
tasks.

A failed check builds an `AssertionFailure` with the text of the condition, the message, the file
and the line, and passes it to `fail_assertion()`. The message expression is evaluated only then.

### The handler

`fail_assertion()` calls the installed handler and then `std::abort()`. A handler that returns
does not keep the process alive. The default handler writes one line to standard error.
`set_assertion_handler()` installs another, such as one that writes to the consumer's log, and
returns the one it replaced; a null argument restores the default. The handler is one atomic
pointer for the whole process.

## bit.hpp

### C++17 stand-ins

bit.hpp holds bit helpers that stand in for C++20 \<bit> (std::popcount and std::countr_zero) on the
C++17 baseline. The std:: versions come back when the standard is raised;
docs/design/REMAINING_WORK.md lists that under "C++ standard migration".

## common.hpp

### Backend

`Backend` names the SYCL, CUDA and HOST backends. No code in the repository reads it, so the CUDA
enumerator is unused.

### Size constants

`K_CACHE_LINE_SIZE` is the alignment of per-worker and per-thread state in the dependency tracker,
the task scheduler, its work-stealing deque and epoch reclamation. `K_MAX_WORKER_THREADS` bounds the
scheduler's worker arrays, and `K_MAX_NUMA_NODES` bounds the per-node cursors in
`device_queue_set.cpp`. `K_MAX_SUPPORTED_XPU` and `K_DEFAULT_ALIGNMENT` have no reader in the
repository.

## delegate.hpp

### Why Delegate exists

Delegate is a move-only, allocation-free, type-erased callable that replaces std::function for the
runtime's stored closures.

The runtime stores user callables that are polled every step (late-binding predicates, per-step
updaters) and replay closures built once per add(). Using std::function for these costs a heap
allocation per stored closure and drags in RTTI and target() machinery the runtime never uses.
Delegate erases the callable into a fixed inline buffer with a hand-written three-entry vtable, so
it never touches the heap. A capture that does not fit is a compile error, not a silent allocation.

### Delegate

A Delegate holds any callable matching `R(Args...)` whose size and alignment fit the inline storage.
The callable is relocated, not reference-counted, so the Delegate owns it outright and frees it on
destruction. Unlike std::function it is move-only (a stored closure has one owner) and never
allocates: a target too large for `Capacity` fails a static_assert instead of spilling to the heap.
A default-constructed Delegate is empty; calling an empty Delegate is a precondition violation,
asserted in debug builds. When `R` is `void`, a callable with any return type is accepted, and its
result is discarded.

### Delegate(F&&)

The converting constructor move-constructs the decayed callable into the inline buffer and binds the
static vtable for its type. The size and alignment fit is enforced at compile time, so the
constructor never allocates and never fails. The callable must also have a noexcept move
constructor, because the vtable's relocation is noexcept; a callable that does not fails a
static_assert.

### vtable_for

vtable_for() gives one shared vtable instance per target type. It reaches the instance through a
function-local static, so it needs no out-of-line definition and has a single address across
translation units.

## error.hpp

### Errc, Error and SushiException

`Errc` is a `std::uint8_t` enumeration with one code per failure class, and `to_string` returns the
name of each code. `Error` pairs an `Errc` with a message. `SushiException` derives from
`std::runtime_error`, holds an `Error`, and returns it from `error()`. The throwing entry points,
such as `Result::value()` and `require_matching_version()`, throw `SushiException`.

New enumerators go at the end of `Errc`. `tests/test_error.cpp` pins the number of each one.

## export.hpp

### SUSHIRUNTIME_API, SR_API and SR_TYPE_API

On Windows, `SUSHIRUNTIME_API` expands to `__declspec(dllexport)` when `SUSHIRUNTIME_EXPORTS` is
defined and to `__declspec(dllimport)` otherwise. Elsewhere it expands to
`__attribute__((visibility("default")))`. `SR_API` is a short spelling of `SUSHIRUNTIME_API`.
`SR_TYPE_API` marks a type defined whole in a header that crosses the library boundary inside an
exception. It expands to the default visibility attribute outside Windows and to nothing on Windows.

## ref_counted.hpp

### RefCounted

The counter starts at 0. Each sushi_ptr copy increments it with relaxed ordering; no synchronisation
is needed, because ownership cannot change without an existing reference. The last release() issues
an acquire fence before calling delete, so all prior writes are visible to any thread that had access
through an earlier reference.

## result.hpp

### Result

`Result<T, E>` holds a value of type `T` or an error of type `E` in a `std::variant`. `has_value()`
and `explicit operator bool` report which one is held. `make_error` wraps an error payload in
`ErrorWrapper`, and the `Result` constructor that takes an `ErrorWrapper` stores it as the error.

### Accessors

`value()`, `operator*` and `operator->` return the success value. When the result holds an error,
they throw `SushiException`: the exception carries the stored `Error` when `E` is
`Error`, and otherwise carries `Errc::invalid_state`. `error()` throws
`SushiException` with `Errc::invalid_state` when the result holds a value.

## span.hpp

### One class in every language mode

SushiRuntime::span is one class under every language standard, so a function that takes a span has
the same signature in a C++17 and a C++20 translation unit. The class definition is the same text
in both, so a C++17 library and a C++20 consumer agree on it. From C++20 it also converts to and
from std::span, through two declarations outside the class: a trait that marks std::span as a view
an rvalue of which may be viewed, and std::ranges::enable_borrowed_range for SushiRuntime::span.
Both are keyed on \_\_cplusplus, not on the library macro \_\_cpp_lib_span, which is defined only
after a header such as \<span> has been included and so varies per translation unit.

## strong_id.hpp

### Why StrongID exists

StrongID is a strong typedef for integer identifiers. It stores one `Rep` and no other state.

A bare std::size_t parameter lets unrelated quantities be passed in each other's place, a device
index where an element count belongs for example, and the compiler stays silent. StrongID wraps an
integer in a distinct type keyed by a tag, so the compiler rejects the mix while the value still
occupies exactly one integer and the explicit constructor keeps construction intentional.

### StrongID

Two StrongID instantiations with different tags are unrelated types even when their underlying
representation matches, so one cannot be passed where the other is expected. The constructor is
explicit, so a raw integer never converts implicitly; the value is read through value(). A StrongID
is trivially copyable and has the size of `Rep`. It is not a trivial type, because its default
constructor sets the value to zero. It has `==`, `!=` and `<`, and its `std::hash` forwards to
`std::hash<Rep>`, so it can key a `std::map` or a `std::unordered_map`.

## version.hpp

### Headers and the loaded library

version.hpp gives the library's version in two forms. The macros hold the version of the headers
being compiled against. `runtime_version()` reports the version the loaded library was built from.
The two answers can differ, and a consumer that pins by tag needs both to catch that case. Without
that check, a stale shared library found first on the loader path fails late, and nothing points
back at the cause, when it could have failed at startup. `version_matches()` is that check.

`cmake/SushiRuntimeVersionCheck.cmake` compares the literals in version.hpp with the version in
`project()` when CMake configures the build, and stops the configure on a difference.

### runtime_version

`runtime_version()` is compiled into the library, not into the caller. It answers which binary the
program runs against, and not which headers the program compiled against.

### version_matches

`version_matches()` is the check a consumer makes once at startup. It returns true when the loaded
library is compatible with these headers. A mismatch means the headers and the binary came from
different releases. One source of a mismatch is an older shared library earlier on the loader path.
Without the check, the failure is a crash or a wrong answer at a later point, and nothing points
back at the cause.

It compares major and minor only. A patch release is defined not to change layout or behaviour, so
refusing to run against one would reject a compatible upgrade.

## Implementation

Sources of the `Core` layer that need a translation unit. The headers are in
`include/SushiRuntime/core/`.

### assert.cpp

Holds the handler pointer and the default handler. `fail_assertion()` is compiled into the
library, so a failed check costs its caller one call and no formatting code.

### version.cpp

`runtime_version()` and `runtime_version_string()` read the same `SUSHIRUNTIME_VERSION_*`
macros a consumer sees in `version.hpp`, but the values are frozen when the library is
compiled. A consumer that compares its own macros with these two functions is comparing two
builds. They agree only when the binary it loaded is the one its headers describe.
