# BB-4: Consumer linkage and packaging

**The problem.** There is no out-of-tree consumption path: no `install(EXPORT)`, no
`SushiRuntimeConfig.cmake`, no version header, no namespaced target; the documented integration
is "drop your .cpp into applications/examples/". The engine consumes the runtime by `add_subdirectory` of a
sibling checkout (`sushiengine/cmake/Runtime.cmake:11-19`): workable for the monorepo-adjacent
layout, but it means "pin a known-good runtime" is a checkout convention, not an artifact, and
any third consumer (the VST host case in the audio plan, a dedicated server image, CI shards)
inherits the whole source tree.

**The changes.**
1. `install(EXPORT SushiRuntimeTargets NAMESPACE SushiRuntime::)` +
   `SushiRuntimeConfig.cmake.in` + `write_basic_package_version_file`. The exported target
   carries usage requirements honestly: include dirs, C++17, `-fsycl` (or the
   `add_sycl_to_target` requirement surfaced as an interface property), and the BB-3 FP contract.
   What it must **not** carry: warnings, `-Werror`, sanitizers, `-march` (BB-3's split makes this
   true; a consumer-side test asserts it).
2. `modules/foundation/core/include/SushiRuntime/core/version.hpp`: `SR_VERSION_{MAJOR,MINOR,PATCH}` + a runtime
   `version()` accessor, generated from the CMake project version. The engine's seam files can
   then `static_assert` the contract they were written against (P5 mechanized).
3. The consumer-toolchain truth gets documented as the contract it is: kernels are header
   templates instantiated in the consumer's TUs, therefore **the consumer's SYCL-touching TUs
   must be compiled by the SYCL toolchain**. Not a defect to fix (it is what makes one-source
   kernels work) but it is the single most important fact an integrator needs and it appears
   nowhere. A `docs/guides/INTEGRATION.md` owns it, plus the two-lane pattern the engine already proves
   (SYCL-free subtrees like `atmosphere/` and `sushi_render` link nothing).
4. ~~`SR_DISTRIBUTED` stops being a PUBLIC define.~~ **Resolved as "keep PUBLIC": the premise
   was wrong.** `NodeExtensions` did fix the layout coupling this item was written about, but two
   others survive it, and both were found by reading rather than by the ABI check the item asked
   for (which would have passed on a standalone consumer that never touched the cluster API):
   - The library compiles **none** of `include/SushiRuntime/api/`, so every `#ifdef
     SR_DISTRIBUTED` there is *consumer*-compiled surface, not library-internal: `Buffer`/`State`
     registration, the entire cluster API (`cluster.hpp`, `runtime.hpp`), and
     `DynamicGraph::rearm()`'s reset of each node's offload attempt counter. PRIVATE would
     compile the cluster API out of every consumer and silently break offload retry: a
     functional regression wearing an ABI cleanup's clothes.
   - `NodeExtensions` itself still changes size with the flag. `Node` does not, but the
     `unique_ptr`'s deleter is instantiated wherever a `Node` is destroyed, and consumers destroy
     nodes from header-inline API code.
   Making it private is therefore a design change, not a keyword change: `NodeExtensions` would
   have to become opaque (declared in the header, defined and deleted only in the library) and
   the distributed API surface would need a deliberate consumer opt-in. Not a v1.0.0 blocker:
the freeze list needs BB-3 and BB-4's packaging, not this. The stale comment that motivated
   the item has been replaced with the real reasoning, and the installed package reports
   `SushiRuntime_DISTRIBUTED` so a consumer can see what it linked.
5. Decide `Core::Result<T,E>` **permanent** (the open TODO item). It is the freeze-safe choice:
   the shape already mirrors `std::expected`, so a C++23 migration is an alias swap later, and
   the public return types never break. Document it, close the item.
6. Version-tagged releases from BB-0 onward; the engine's `Runtime.cmake` learns to check the
   tag (fail loudly on mismatch, not drift silently).

**Proof.** A fresh out-of-tree "hello consumer" project (three files, in CI):
`find_package(SushiRuntime REQUIRED)`, one kernel, runs on the CPU lane; the first such build in
the project's history. The flag-hygiene assertion from BB-3 runs in the same project.
