# SushiRuntime v1.0.0 Stable API — Work Plan

> Decided 2026-06-16. This document captures the architectural decisions on the
> road to the v1.0.0 stable API and the implementation steps that follow. The
> discussion is closed; this is the implementation reference.

## Decisions (summary)

| Topic | Decision |
|---|---|
| SYCL toolchain | Compiler-agnostic **SYCL 2020**. We do not bind to Intel oneAPI. **AdaptiveCpp is the primary/main toolchain**; oneAPI is kept only as a secondary path for profiling (VTune/Advisor). |
| Radix tree | **Stays.** `LockFreePageMap` (TCMalloc-style, `src/memory/usm_allocator.cpp`). The "switch to hashing" advice was rejected — this is not a generic dictionary but pointer→page resolution; the jemalloc/tcmalloc trend runs the other way (hash → radix). |
| Boilerplate abstraction | **No macros.** An explicit graph object plus C++20 concept-based dispatch. A hidden/thread-local context was rejected. |
| Error handling | **C++23 + `std::expected`.** Two channels (synchronous + asynchronous). `run()` (throws) + `try_run()` (does not throw). |
| C++ standard | `CMAKE_CXX_STANDARD` **20 → 23**. |

---

## Target user-facing API (reference)

```cpp
#include <SushiRuntime/sushi.hpp>
using namespace SushiRuntime;

inline void blas_gemm_ops(size_t id) { /* GPU compute */ }
inline void matrix_scale(size_t id)  { /* GPU compute */ }

int main() {
    auto rt = Runtime::create();          // default device selection
    auto g  = rt.graph();

    g.add(1024, blas_gemm_ops)            // chaining
     .add(512,  matrix_scale);

    auto r = g.try_run();                 // std::expected<RunReport, Error>
    if (!r) std::println("error: {} @ {}",
                         r.error().message,
                         r.error().where.function_name());
    // or simply: g.run();                // throws SushiException
    return 0;
}
```

The only difference from a hypothetical `ADD_TASK` macro is the `g.` prefix —
a deliberate trade: the owner is explicit, it is testable, and multiple graphs
can coexist.

---

## Work package 1 — Build system (CMake) cleanup

Covers the three items in `docs/slop/build_system_issues.md` plus the AdaptiveCpp
and C++23 moves. Everything else sits on top of this, so **do it first**.

- [ ] **Add the AdaptiveCpp toolchain branch.** Alongside the existing
      `icpx|icx-cl|clang++` detection, add an `acpp` (AdaptiveCpp compiler) branch.
      Make AdaptiveCpp the primary/supported path; keep oneAPI as secondary.
      Investigate `find_package(AdaptiveCpp)` / the `add_sycl_to_target` equivalent.
- [ ] **Remove hardcoded paths** (build_system_issues #1):
  - `C:/vcpkg` (lines 6, 13, 95) → rely on the `VCPKG_ROOT` env var; keep a
    fallback but do not force it.
  - Intel oneAPI path (line 19) → `find_package`/env, not hardcoded.
  - `-I/usr/include/c++/13`, `--gcc-toolchain=/usr` (lines 124-126) → leave to
    toolchain discovery or CMakePresets. (Moving to AdaptiveCpp simplifies this
    intel/llvm branch anyway.)
  - hardcoded `pkgconf.exe` path (line 95) → make portable.
- [ ] **Make the CUDA architecture configurable** (build_system_issues #2):
  - hardcoded `--cuda-gpu-arch=sm_61` (lines 149-152) → a `SUSHI_CUDA_ARCH` option
    or `CMAKE_CUDA_ARCHITECTURES`. Default `native` or a list. (The sm_61 value is
    not wrong — the dev GPU is Pascal — but hardcoding it is.)
- [ ] **Move the C++ standard to 23.** `set(CMAKE_CXX_STANDARD 23)` (line 56).
      Required for `std::expected`, `std::source_location`, `std::println`.
      AdaptiveCpp/clang + libstdc++13 supports it; verify.
- [ ] Update CMakePresets.json for the new toolchain/standard.
- [ ] Vendor lock-in (build_system_issues #3) is resolved by this package — no
      separate work.

## Work package 2 — Error-handling infrastructure

Sits below the fluent API, so it comes first.

- [ ] **Domain error types** (new header, e.g. `include/SushiRuntime/core/error.hpp`):
  ```cpp
  enum class Errc { ok, invalid_graph, compile_failed,
                    allocation_failed, kernel_failed, device_lost };
  struct Error {
      Errc code;
      std::string message;
      std::source_location where = std::source_location::current();
  };
  class SushiException : public std::runtime_error { public: Error err; /* ... */ };
  ```
- [ ] **Two channels:**
  - Synchronous errors (compile, allocation, invalid graph) → at the call site.
  - Asynchronous errors (a kernel faulting on the device) → land in the SYCL queue
    **async exception handler**; catch them and map to `Errc::kernel_failed`.
  - Attach the async handler when building the queue (otherwise device errors are
    silently lost):
    ```cpp
    sycl::queue q{selector, [](sycl::exception_list el) {
        for (auto& e : el) std::rethrow_exception(e); // → routed to the Error channel
    }};
    ```
- [ ] **Two entry points:** `void run()` (throws SushiException) and
      `[[nodiscard]] std::expected<RunReport, Error> try_run() noexcept`.
- [ ] Define the `RunReport` type (run summary: duration, tasks executed, etc. —
      scope to be finalized).

## Work package 3 — Fluent Graph/Runtime layer

Sits ON TOP of the existing `TaskGraph` (`add_node`/`add_task`/`compile`) and
`Scheduler::RangeTask` (`split`/`is_splittable`). We do not rewrite the lower
layer, we wrap it.

- [ ] **`Runtime` type:** `Runtime::create(...)` (device selection),
      `rt.graph()` → returns a `Graph`. Holds the RuntimeContext internally.
- [ ] **`Graph` type** — concept-based `add()` overloads:
  ```cpp
  template <class F> concept PerElement  = std::invocable<F, size_t>;
  template <class F> concept RawKernel   = std::invocable<F, sycl::handler&>;
  template <class F> concept RangeKernel = std::invocable<F, size_t, size_t, sycl::handler&>;

  template <PerElement F>  Graph& add(size_t n, F&& fn);  // → auto parallel_for, RangeTask
  template <RawKernel  F>  Graph& add(F&& fn);            // → raw handler, add_node
  template <RangeKernel F> Graph& add(size_t n, F&& fn);  // → RangeTask(0,n,fn)
  ```
  - `PerElement` case: wrap as `h.parallel_for(range<1>(n), [=](id<1> i){ fn(i[0]); })`.
  - All return `*this` → chaining.
  - Concepts produce a readable error on a wrong signature (what a macro can't).
- [ ] `run()` / `try_run()` → internally drive `TaskGraph::compile()` + the
      execution engine, wiring errors into the work-package-2 channel.
- [ ] **Single public header** `include/SushiRuntime/sushi.hpp` (umbrella) — one
      include for users.

## Work package 4 — Bring existing code into conformance

Once the coding conventions are settled (CONTRIBUTING §4, including the Doxygen
rule), sweep the existing tree so every file matches before v1.0.0 freezes the API.

- [ ] **Apply the Doxygen rule** (CONTRIBUTING §4 → "Doxygen documentation") to all
      public headers: every function states *why* and *how* in plain English, with
      every `@param`/`@return` documented and no open points — concise, technical,
      objective.
- [ ] **Audit coding style** across `include/` and `src/`: Allman braces, nested
      namespaces, naming, no separator comments, license headers.
- [ ] Do this as a dedicated, behavior-free pass (one PR, no logic changes) so it
      stays reviewable and revertible.

## Work package 5 — Documentation and examples

The new public surface changes several documents. These must be updated **after**
the API lands, in the same pass:

- [ ] **`README.md`** — the "How it works" quick start currently shows the
      `RuntimeContext` + `TaskGraph` + `compile()` + `execute()` flow. Replace with
      the `Runtime::create()` / `g.add(...)` / `g.run()` fluent flow. Also update the
      Requirements/Building sections: AdaptiveCpp as primary toolchain, C++23, the
      configurable CUDA arch, and the removal of hardcoded `C:/vcpkg`/oneAPI paths.
- [ ] **`docs/guides/ARCHITECTURE.md`** — add the fluent layer above `graph/` in the
      layer diagram (§2) and the execution model (§3); document the error-handling
      design and the two channels; update the build-time configuration table (§13)
      and the toolchain notes (AdaptiveCpp primary, C++23, `SUSHI_CUDA_ARCH`).
- [ ] **`docs/guides/INTRODUCTION.md`** — the user-facing "how to write against it"
      guide; rewrite its examples onto the fluent API and `std::expected` error
      handling.
- [ ] `docs/guides/CLI_GUIDE.md` — AdaptiveCpp build/setup instructions where the
      CLI drives the toolchain.
- [ ] `examples/` — add a fluent-API quick-start example (the `main` above).
- [ ] Sweep remaining references to the old `RuntimeContext`/`TaskGraph`-first flow,
      the two-oneAPI-toolchain framing, and `sm_61`/C++20 mentions across all docs.

---

## Suggested order

1. **Work package 1 (CMake/AdaptiveCpp/C++23)** — the foundation; do it first.
2. **Work package 2 (error infrastructure)** — the channel the fluent API binds to.
3. **Work package 3 (fluent layer)** — the actual API surface.
4. **Work package 4 (code conformance)** — once conventions are settled.
5. **Work package 5 (docs/examples)** — once the surface is stable.

## Open notes / pending decisions

- The exact contents of `RunReport`.
- The AdaptiveCpp CMake integration detail (SSCP targets, `--acpp-targets`).
- Whether the existing oneAPI/intel-llvm branches are kept during the transition or
  simplified once AdaptiveCpp settles — the migration strategy.
