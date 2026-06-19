# C++ Standard Migration Notes — C++23 → C++17 (and the road back)

> Recorded 2026-06-19, branch `refactor/v1-toolchains-acpp`. The project was moved
> from C++23 down to **C++17** to get a portable, CUDA/nvptx-friendly baseline that
> every supported SYCL toolchain (llvm, AdaptiveCpp, oneAPI) compiles cleanly.
>
> **Important:** the C++23 move was NOT what caused the CUDA segfault we were
> chasing. That was an oneAPI/llvm-sycl Unified Runtime library mix (see
> `cuda-segfault-oneapi-llvm-mix` in memory / the `env.py` + Dockerfile fix). The
> C++17 migration is kept on its own merits (portability), not as a bug fix.
>
> This document is the checklist for **re-introducing C++20 and C++23** once the
> SYCL toolchains we target support them comfortably across the CUDA device pass.
> Each row says what we did for C++17, and what to restore (and where) when the
> standard is raised.

## How to raise the standard later

1. `CMakeLists.txt`: `set(CMAKE_CXX_STANDARD 17)` → `20` (then `23`).
2. Work through the tables below in file order.
3. Some shims are **self-retiring** (span) — they switch to the std type
   automatically and only need their now-dead branch deleted at leisure.

---

## C++20 — restore when the standard is raised to 20

| Feature | C++17 workaround (current) | Where | Restore action at C++20 |
|---|---|---|---|
| **Concepts** (`PerElement`/`RawKernel`/`RangeKernel`) | `inline constexpr bool is_*_v` traits over `std::is_invocable_v` + `std::enable_if_t` on each `add()` overload | `include/SushiRuntime/api/runtime.hpp` | Turn the `is_*_v` traits back into `concept`s and constrain `add()` with them (`template <PerElement F>`). Restores the pointed "constraint not satisfied" diagnostics. |
| **`requires`-expression** (capability detection) | `void_t` detection idiom: `has_host_task<>` trait + `if constexpr (has_host_task_v<...>)` | `include/SushiRuntime/core/sycl_compat.hpp` | Replace with `if constexpr (requires { h.host_task(fn); })` and delete the trait. |
| **`std::span`** | `SushiRuntime::span` shim; the header already aliases `std::span` automatically when `__cplusplus >= 202002L` | `include/SushiRuntime/core/span.hpp`; used across `distributed/**` + distributed tests + `examples/distributed_demo.cpp` | Nothing forced — the alias kicks in at C++20. Optionally delete the `#else` shim branch. Call sites stay `SushiRuntime::span` (or sed back to `std::span`). |
| **`std::barrier`** | `SushiRuntime::Test::Barrier` (condition_variable, reusable, generation-counted) | `tests/common/test_barrier.hpp`; used in `test_memory_pool.cpp`, `test_allocator_perf.cpp` | Swap `SushiRuntime::Test::Barrier` → `std::barrier`, `#include "test_barrier.hpp"` → `<barrier>`, delete the helper. |
| **`[[likely]]`/`[[unlikely]]`** | attribute removed | `src/scheduler/scheduler.cpp` (the `offloadable_` branch, ~L448) | Re-add `[[unlikely]]` on the `if (gnode->offloadable_)` branch. (Cosmetic/perf hint only.) |
| **Abbreviated function template** (`auto` parameter) | explicit `template <typename Func>` constructor | `include/SushiRuntime/scheduler/task.hpp` (`RangeTask` ctor) | Optional: collapse back to `RangeTask(size_t, size_t, auto func, ...)`. |
| **`std::popcount` / `std::countr_zero`** (`<bit>`) | `Core::popcount_u64` / `Core::countr_zero_u64` (compiler intrinsics, MSVC fallback) | `include/SushiRuntime/core/bit.hpp`; used in `include/SushiRuntime/memory/memory_pool.hpp` | Replace the helpers with `std::popcount` / `std::countr_zero`, `#include <bit>`, delete `bit.hpp`. |
| **`std::atomic_ref<Task*>`** | the deque ring buffer is now `std::atomic<Task*>* items` (atomic slots), accessed via `.load()/.store()` | `src/scheduler/scheduler.cpp` (`Deque`) | Optional: revert to a plain `Task** items` + `std::atomic_ref<Task*>(items[i & MASK])`. Functionally equivalent; atomic_ref avoids storing atomics. |
| **`std::format` / `std::vformat`** | hand-rolled `sr_format()` — positional `{}` only, streams args with `operator<<`; `snprintf` for the timestamp | `include/SushiRuntime/core/logger.hpp` | Replace `sr_format` with `std::vformat(fmt, std::make_format_args(args...))`. NOTE: this also restores format specs (`{:04x}`, `{:.2f}`) — see the two PCI sites below. |
| **`std::source_location`** | dropped from `Error`; logger passes `__FILE__`/`__LINE__`/`__func__` via macro | `include/SushiRuntime/core/error.hpp` (Error had a `where` field), `include/SushiRuntime/core/logger.hpp` | Re-add `std::source_location where = std::source_location::current();` to `Error`; change `log_base` to take a `source_location` again and the macros to pass `::current()`. |
| **`__VA_OPT__`** in log macros | macros take `(...)` and fold the format string into `__VA_ARGS__` (so the list is never empty) | `include/SushiRuntime/core/logger.hpp` | Optional: revert to `(fmt, ...)` + `__VA_OPT__(,)` if preferred. The `(...)` form also works in C++20, so this is cosmetic. |
| **Format specs at call sites** | two PCI log lines pre-format hex with `snprintf` into a buffer, then log `{}` | `src/topology/hw_discovery.cpp`, `src/topology/numa_mapper.cpp` | Once `std::format` is back (logger uses it), revert to inline `{:04x}:{:02x}:{:02x}.{:x}` and drop the `snprintf` scratch buffers. |

## C++23 — restore when the standard is raised to 23

| Feature | C++17 workaround (current) | Where | Restore action at C++23 |
|---|---|---|---|
| **`std::expected` / `std::unexpected`** | `Core::Result<T, E>` (a `std::variant`-based subset) + `Core::make_error()` | `include/SushiRuntime/core/result.hpp`; `try_run()` returns it in `include/SushiRuntime/api/runtime.hpp` | Replace `Core::Result<RunReport, Error>` with `std::expected<RunReport, Error>`, `make_error(x)` with `std::unexpected(x)`, `#include <expected>`, delete `result.hpp`. The public shape (`operator bool`, `.value()`, `.error()`, `*`) was kept identical, so call sites are unchanged. |
| **`std::println`** | never actually adopted (only appears in the v1 plan/example comments); examples use `std::printf`/`fprintf` | `examples/fluent_quickstart.cpp` comment, `docs/slop/v1_api_plan.md` | Optionally switch example output to `std::println`. No code depends on it. |

## Not touched (already C++17-clean)

The scheduler, memory (radix `LockFreePageMap`), graph, topology, and EBR layers
were already C++17-compatible (`std::is_invocable`, `if constexpr`,
`std::string_view`, `std::optional`, structured bindings, `if`-init). No
down-porting was needed there.

## Build-system cleanup that came with C++17 (do NOT re-add)

`CMakeLists.txt` previously carried workarounds that only existed to make C++23's
`<expected>` compile under clang's `-fsycl` device pass — these were **deleted**
and should stay deleted unless C++23 returns:

- `-Wno-builtin-macro-redefined -U__cpp_concepts -D__cpp_concepts=202002L`
  (forcing libstdc++'s `<expected>` feature-test gate open).
- The `file(GLOB ...)` libstdc++ version-dir discovery / explicit `-I` injection.

Only the portable `--gcc-toolchain=${SR_GCC_TOOLCHAIN}` pin remains. If C++23 is
restored, the `__cpp_concepts` redefinition may need to come back for older
libstdc++/clang combos (see Dockerfile history: clang-17 + libstdc++ 13).
