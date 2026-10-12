# SushiRuntime: Engine Backbone Refactor Plan (BB)

**Status:** Open — BB-0's runtime side, BB-1a, BB-2, BB-3, BB-4 and BB-6 item 4 landed; BB-1b is unbuilt; BB-5, BB-7, BB-8 and the rest of BB-6 were not re-checked against the tree on 2026-10-05.

The reductions, the prefix scan and the counting sort this document names are SushiBLAS's;
[the design map](README.md#names-that-left-the-runtime) says how to read them.

Engineering plan, authored 2026-07-30 on the branch `feature/physics-substrate-seams`, which has
since merged. What landed in the first wave is recorded in
`docs/archive/slop/BACKBONE_WAVE_STATUS.md`.

**Evidence base.** Four full surveys taken on 2026-07-30: (1) the runtime's public API and
execution model, (2) the runtime's build/packaging/deployment posture, (3) every SushiRuntime
call site in SushiEngine, (4) SushiEngine's design-doc corpus and its forward requirements.
File:line citations below come from those surveys; where a claim matters, the citation names the
exact site.

**Mandate.** SushiEngine is the head; SushiRuntime is the backbone. This document plans the
runtime refactor that makes that relationship the fastest, strongest, most operable one available:
shaped by what the engine *actually does and actually needs*, not by what a general compute
runtime might want to become. It extends, and does not replace,
[`SIMULATION_ENGINE_SUBSTRATE_PLAN.md`](SIMULATION_ENGINE_SUBSTRATE_PLAN.md) (WP-0…WP-7) and
[`PHYSICS_SUBSTRATE_IMPLEMENTATION.md`](../archive/slop/PHYSICS_SUBSTRATE_IMPLEMENTATION.md) (R1–R7). New work
packages are numbered **BB-0…BB-8** to avoid colliding with the WP series.

---

## 0. The one-paragraph verdict

The runtime's simulation core is already the right machine: dependency-inferred DAG, compile-once
replay, late-bound sizes and bases, fixed-order deterministic reductions, interval-exact region
boundaries, an async step; R1–R7 all built and verified on the branch. The gaps that remain are
not in the compute model; they are at the **edges**: the branch is unmerged and unadopted
(BB-0), simulation results cannot reach Vulkan without a host round-trip (BB-1), the runtime
behaves as the machine's sole tenant (BB-2), the build flags undermine the determinism the API
promises (BB-3), no out-of-tree consumer can link it (BB-4), and no player machine can run it
(BB-5). Those six, in that order, are the backbone refactor. BB-6/7/8 are the maturation tail.

---

## 1. Ground truth: what changed under everyone's feet

Three facts discovered in the surveys reorder every plan written before them.

**1. R1–R7 exist, and the engine does not know.** The engine's own requirement register,
`sushiengine/docs/slop/physics_system.md` §18, was corrected on 2026-07-29 to read *"R1, R2, R3
and R5 must be read as open"*: a check made against `main`, which carries only doc commits on
top of the pre-seams state. The seams landed on `feature/physics-substrate-seams` on 2026-07-28
(`800cb5c` add_untracked, `db3904b` add_reduce, `eaa390d` sized_from_device, `01c84f4` interval
boundaries, `5536b02` run_async + based_at, `d46cd7f` magazine teardown). Functional suite
154 → 190, ASan-clean. Both documents were right on their own day; together they describe a
delivery that never arrived. **The cheapest unit of value in this whole plan is a merge.**

**2. The engine has adopted part of the new surface: this fact is now half stale.** The original
"call-site count: all zero" claim does not hold. Re-counted 2026-08-17 against
`D:/Projects/sushiengine` (grep over `engine/`, `applications/`, `tests/`, excluding `build/`):

- `add_reduce`: **has callers.** The physics solver calls
  `graph_->add_reduce(*motion_, *motion_maximum_, capacity, Maximum<T>{}, T(0))` at
  `engine/domain/physics/include/SushiEngine/physics/solver/runtime_graph_builder.hpp:1240`,
  wrapped by the execution seam at
  `engine/foundation/execution/include/SushiEngine/execution/backend/runtime_backend.hpp:377`
  (Graph) and `:479` (Region).
- `DynamicGraph`: **has callers.** `SushiRuntime::API::DynamicGraph` is wrapped by the engine's
  own `DynamicGraph` in `runtime_backend.hpp:513-569`.
- `RunReport::node_timings`: **has callers.** `physics_node_timings_from_report()` iterates
  `report.node_timings` at
  `engine/domain/physics/include/SushiEngine/physics/core/statistics_from_report.hpp:101`.
- `based_at*`: **has one caller**, not zero: `dynamic.and_based_at(...)` at
  `runtime_backend.hpp:138`. The plain `based_at()` spelling has none.
- Still zero: `add_segmented_reduce`, `sized_from_device`, `add_untracked`, `run_async`, and
  `SushiRuntime::API::State<T>` (the `State<...>` hits in the engine are its own domain types,
  e.g. `JointAngularState<T>`).

The hand-built stand-ins this fact listed are also mostly gone. `MOTION_SEGMENT` **does not exist
anywhere in the engine**: the two-node fixed-order reduction it belonged to has been replaced by
the `add_reduce` call above. What remains of the list is unverified here and should be re-checked
before it is acted on: the island partition and the CPU-only broadphase/narrowphase transfers
were not re-confirmed in this pass, and the file:line citations for them
(`islands.hpp:48-53`, `physics_simulation.hpp:72-79`) are stale; no `islands.hpp` was found at
that path.

`core/blas_placeholder.hpp` is **not** a duplicate to delete. Its fold operators are live: `Sum`
(line 504), `Product` (511), `Minimum` (525) and `Maximum` (532), and `Maximum<T>{}` is the
combiner passed to `add_reduce`. Deleting them breaks the build. The one member with no callers
is `fold_range` (line 555), re-exported as `placeholder::fold_range` in `core/types.hpp:111`.

**3. Where the runtime was absent, the engine routed around it, and recorded why.** Audio's
real-time mix sidesteps the runtime entirely (`audio/accelerator.hpp:31-37`). The atmosphere tier
chose Vulkan compute and wrote the reason down so it would not be re-proposed
(`atmosphere_system.md` §11). Cosmetic particles and skinning shipped as Vulkan compute, with the
runtime's SYCL kernel surviving only as a correctness oracle (`render/shaders/skinning.comp:87`).
The cross-platform plan reduces the runtime's blast radius from 52 files to ~3 behind an `Exec`
seam. Every one of these is a boundary lesson this plan takes as *input*, not as damage to undo.

---

## 2. Principles

**P1: Usage shapes the API; v0.x is the time to break.** The owner's decision: no
`supports()`-style capability hedging, no compatibility shims before v1.0.0. The API is shaped
once, from the real consumer's call sites, and then frozen. Every package below states its API
changes plainly as changes.

**P2: The boundary rule, kept.** The engine's governing rule (its `ARCHITECTURE.md` §1): *"A
feature that only composes the runtime's public API belongs in the engine. A feature that needs
the runtime to do something it cannot yet do belongs in SushiRuntime."* Corollaries already
locked: graph colouring stays engine-side (L8); DSP math stays engine/SushiBLAS-side; the runtime
owns ordering, memory, scheduling, and determinism of the *mechanism*.

**P3: Determinism is the product.** Same-binary, same-device determinism is the one property
that makes this runtime a better backbone for SushiLoop than raw Vulkan compute. Anything that
weakens it silently (build flags, default-on rebalancing, schedule-dependent arithmetic) is a
defect even when it is faster.

**P4: The sim domain, not render resources.** The atmosphere rejection is scope guidance:
*"[the runtime's] value is in the deterministic simulation domain, where the data does not have
to become something the rasteriser samples."* BB-1 exists to let simulation *output* flow to the
renderer; it is not an invitation to move render-resource computation into SYCL.

**P5: A claim is only real if a TU instantiates it.** The `add_reduce` incident: the engine
called an API that did not exist, and `-fsyntax-only` blessed it for weeks because nothing
instantiated the template. Both repos adopt the build rule: every public runtime API the engine
leans on has at least one engine-side TU that instantiates it, and every runtime feature ships
with a consumer-shaped example compiled in CI.

**P6: Co-tenant, not sole tenant.** The runtime today assumes it owns the machine: workers
pinned to cores 0…N−1, `hardware_concurrency()` threads, a background rebalancer on by default, a
process-global allocation mutex. Inside a game it shares the machine with a render thread, an
audio callback, asset I/O, and the OS compositor. Every scheduling default gets re-examined under
that assumption.

---

## 3. The program at a glance

| Pkg | Title | Size | What it unblocks |
|---|---|---|---|
| BB-0 | Merge & adopt R1–R7 | S (runtime) + M (engine) | P8 collision-in-graph, islands, engine deletes its reduction; ends the two-truths state |
| BB-1 | Vulkan interop: external memory import + sync export | L | Zero-copy palettes, soft-body vertices, VFX sim, render extract: the four recorded consumers |
| BB-2 | Co-tenancy: thread & scheduling policy | M | Editor/game co-existence, audio thread safety, jitter floor |
| BB-3 | Determinism contract: flags, fp64, conformance | S–M | SushiLoop rollback guarantee; kills the silent `-march=native`/`/fp:fast` hazard |
| BB-4 | Consumer linkage & packaging | M | `find_package(SushiRuntime)`, out-of-tree engine builds, versioned pinning |
| BB-5 | Ship path: multi-vendor, AOT, cache, zero-device | L | A game binary a player can run |
| BB-6 | Data-plane maturation | M, incremental | Batched transfer, buffer growth, report-free runs, pool decision, device sort |
| BB-7 | Async depth: pipelined steps, deadlines | M–L | Sim/render overlap, audio batch-DSP on the fluent API |
| BB-8 | Platform posture: decide, document, spike | S (decide) | The cross-platform plan stops guessing |

Sequencing is §14. BB-0 is first and alone; BB-2/3/4 are small and parallel; BB-1 is the big
rock; BB-5 follows BB-4; BB-6 is continuous; BB-7 after BB-2; BB-8 is a decision now and spikes
later.

---

## 4. BB-0: Merge & adopt (the free win)

**Runtime side: DONE.**
1. Merged, and tagged: `git tag` lists `v0.2.0`, and `CMakeLists.txt` /
   `modules/foundation/core/include/SushiRuntime/core/version.hpp` both read 0.2.0.
2. TODO #33 (EventPolling `Task*`→`Node*` downcast) is closed: `Task::on_complete(ReadySink&)` +
   `record_completion()`, called from `modules/engine/scheduler/include/SushiRuntime/scheduler/event_polling.hpp:426,457`.
3. The doc truth pass ran. `TODO.md` (now `docs/archive/slop/TODO.md`) and this file were reconciled against the tree on 2026-08-17;
   `docs/archive/slop/CODE_AND_ARCHITECTURE_REVIEW.md` now carries per-finding status.

**Engine side (filed here because the backbone-head relationship is one program).**
4. **Essentially done; the instruction as written was wrong.** The two-node reduction and
   `MOTION_SEGMENT` are gone: the solver calls `Graph::add_reduce` directly
   (`runtime_graph_builder.hpp:1240`). Do **not** delete `blas_placeholder`'s fold operators:
   `Maximum<T>{}` from that header is the combiner handed to `add_reduce`, so deleting it breaks
   the build. Only `fold_range` (`blas_placeholder.hpp:555`) has no callers and is a candidate for
   removal. `add_segmented_reduce` is still unadopted; whether the engine has a segmented case at
   all was not established in this pass.
5. Correct `physics_system.md` §18 (the "must be read as open" note) with the merge commit as
   evidence.
6. Adopt per seam, each with its instantiating TU (P5): `sized_from_device` → the P8
   broadphase→narrowphase→solver chain in one composition (retires the per-tick body-transfer
   pair); R3 + `DynamicGraph` regions → one region per island, waking/sleeping via
   `region()`/`drop()` (the partition in `islands.hpp` is already built); `run_async` → overlap
   the solve with the engine's own extract work inside the tick (bounded scope; full pipelining
   is BB-7).

**Proof.** Engine conformance suite green against tagged `v0.2.0`; substep counts bit-identical
across the reduction swap; `compile_count()==1` invariants hold; a grep proves
`blas_placeholder`'s fold operators have no remaining physics callers.

---

## 5. BB-1: Vulkan interop: the missing half

**Status.** BB-1a landed in 0.3.0 as the `interop` module, shaped by SushiEngine's
`docs/design/SYCL_VULKAN_INTEROP.md`, which supersedes this section where the two differ. BB-1b is
unbuilt.

**The problem, precisely.** Every GPU result reaches the renderer today through a host round-trip
(solver → `read_range` → mirror → ECS → extract → Vulkan upload). The engine shipped its half:
exportable, UUID-stamped, dedicated allocations behind `render/interop.hpp`, with the explicit
note that *"the importing half belongs to whoever owns the other API, which for SushiRuntime
means the runtime"*, and `vulkan_interop_buffer.cpp:249` has **no callers** because there is
nothing to call into. Four consumers are recorded waiting: animation palettes, soft-body deformed
vertices, VFX sim, and the render extract itself. On the runtime side there are exactly two
missing capabilities: memory import and completion export. (`IExecutionEngine::execute` returns
an empty `sycl::event{}` with the warning *"do not chain SYCL dependencies on this value"*:
`execution_engine.hpp:152`.)

**BB-1a: External memory import (ship first).**
- New public surface: `memory/external.hpp`;
`Runtime::import_buffer<T>(const ExternalMemoryDescriptor&) → Buffer<T>`. The descriptor
  carries what the engine's export half already produces: OS handle (Win32 handle / POSIX fd),
  allocation size, and the device UUID to match against the runtime's device (the engine stores
  it in `DeviceInfo` for exactly this). The returned handle participates in dependency tracking
  like any `Buffer<T>`: `region()`, `Reads`/`Writes`, residency `Device`.
- Implementation is per-toolchain behind one seam: intel-llvm/DPC++ via
  `sycl_ext_oneapi_external_memory`; AdaptiveCpp via its native interop handles (CUDA external
  memory / Level Zero import under the hood). The seam mirrors `sycl_compat.hpp`'s existing
  pattern: detection idiom, not `#ifdef` spray. A toolchain with no import path fails at
  `import_buffer` with `Errc::unsupported`, at the call site, not mid-run.
- Ownership: imported memory is *borrowed*; the runtime never frees it, `usm_owner` records it
  as import-typed so `get_queue_for_ptr` routes correctly and `free_usm` on it is an error.
- Sync in 1a is the coarse-but-correct contract: the importing consumer calls
  `RunHandle::wait()` (or `run()` returns) before the Vulkan queue reads. That alone deletes the
  copy and the staging upload; latency overlap comes next.

**BB-1b: Completion export (the overlap half).**
- A run whose outputs feed Vulkan must end in something a `VkQueue` can wait on. Two candidate
  designs, decided at implementation against both toolchains:
  (i) export a timeline-semaphore signal: `run_async(SignalDescriptor)` where the sentinel's
  completion signals an imported `VkSemaphore` (DPC++: `sycl_ext_oneapi_external_semaphore`);
  (ii) expose the run's final events per imported buffer and let the engine bridge them.
  Design (i) is the target: it keeps SYCL types out of the engine and matches the sentinel
  mechanism the engine never sees.
- Non-goal: importing Vulkan *images*. The engine's export half deals in buffers; hardware-
  filtered volume sampling is exactly the render-resource domain P4 excludes.

**Proof.** A new `interop_demo` in `applications/examples/` (compiled in CI when the toolchain supports it):
device kernel writes positions into an imported buffer, a host-side Vulkan stub validates
contents with zero copies; the engine's `vulkan_interop_buffer.cpp:249` gains its first caller;
animation's palette readback loop (`device_batch_evaluator.hpp:241-250`, element-by-element
`operator[]`) is deleted in the adoption pass.

---

## 6. BB-2: Co-tenancy: thread & scheduling policy

**The problem.** `Runtime::create()` today spawns `hardware_concurrency()` workers and pins
worker *i* to core *i* (`scheduler.cpp:63-67, 343`): worker 0 lands on the same core as the
engine's main thread; the render thread, audio callback and job system fight the full-width pool.
A background rebalancer thread runs a 5 ms heartbeat by default and is documented as a
determinism/jitter hazard (`runtime.hpp:191-197`). Idle workers wake every 10 ms regardless.
There is no knob for any of it: `RuntimeConfig` has exactly one field.

**The change: `RuntimeConfig` becomes the co-tenancy contract:**

```cpp
struct RuntimeConfig
{
    bool profiling = false;
    std::size_t worker_count = 0;      // 0 = all cores (today's behavior)
    std::size_t first_core   = 0;      // pin workers to [first_core, first_core + count)
    PinPolicy   pinning      = PinPolicy::Pin;   // Pin | Float
    bool        rebalancer   = false;  // BREAKING: default flips to off (P3 — sim is the
                                       // primary consumer; batch throughput opts in)
};
```

- The engine already turns the rebalancer off at every solver construction and *asserts the
  runtime default is on* in a test (`test_runtime_graph_builder.cpp:266-277`), that test
  inverts, deliberately, and the engine deletes its workaround. Pre-1.0, no shim (P1).
- Worker priority stays untouched in this package (no cross-platform RT-priority pretense);
  what BB-2 guarantees is *placement*: the engine can reserve cores for its main/render/audio
  threads and give the runtime the rest.
- Idle wake: the 10 ms `wait_for` park and the 100 µs spin-sleep are re-examined once workers
  can be fewer than cores; a parked worker on a reserved-core config must cost nothing.
- `RunReport` allocation on every run (`numa_statistics.push_back`:
`run_report_builder.hpp:92`) gets a caller-owned variant: `run(RunReport& reuse)` /
  `wait(RunReport& reuse)` clearing and refilling capacity-retained vectors. Zero-allocation
  steady-state ticks become measurable, then enforceable.

**Proof.** A two-runtime co-tenancy test (engine-shaped: N workers on cores 4…11, a busy "main
thread" pinned to core 0) shows no runtime thread ever schedules on the reserved cores; replay
determinism test passes with the new defaults; steady-state tick allocation count == 0 under the
reuse API (asserted with the benchmark suite's counting allocator).

---

## 7. BB-3: Determinism contract: flags, fp64, conformance

Three defects, all cheap to fix, all currently undermining the property the runtime sells (P3).

**1. The flag policy contradicts the API.** `cmake/CompilerWarnings.cmake` applies
`-march=native` unconditionally (Linux) and `/fp:fast /arch:AVX2` in Release (Windows) to *every
target in the tree* via directory-scope `link_libraries`: including, in-tree today and via BB-4's
export tomorrow, the consumer's own TUs. `/fp:fast` licenses reassociation: the fixed-order
reduce's host-side folds, and every consumer kernel's host pass, can differ between two builds of
the same source. Meanwhile the engine compiles itself with `-fno-fast-math -ffp-contract=off`
under `SE_DETERMINISTIC_FP`: the two repos are actively fighting. The fix:
- Perf/arch flags move off the global interface. `sushiruntime_warnings` keeps warnings; a separate
  **PRIVATE** `sushiruntime_perf` target carries `-O3` and any `-march`; `-march=native` becomes an
  opt-in dev flag (`SR_NATIVE_ARCH=ON`, default OFF), never a default a shipped binary inherits.
- `/fp:fast` is deleted from Release. A new `SR_DETERMINISTIC_FP` (default **ON**, matching the
  primary consumer) pins `-ffp-contract=off` and forbids fast-math on every TU the runtime
  compiles; the flag is exported so consumer TUs (which instantiate the kernels) inherit the
  same contract. Batch users who want fast math opt out per the usual "sim is the default"
  posture.

**2. fp64 is assumed, never checked.** SushiLoop locks *"GPU XPBD in double precision"*; the
engine's `Scalar` is unconditionally double; no device-selection path checks
`sycl::aspect::fp64`. A device without fp64 today fails somewhere inside a kernel launch with a
driver-shaped error. The fix: `HardwareFilterConfig` gains `require_fp64` (engine sets it);
discovery filters non-conforming devices out with a log line, and `Runtime::create` reports
clearly when the filter empties the device list (composes with BB-5's `try_create`).

**3. The conformance suite tests the primitive, not the machine.** `test_fixed_order_reduce.cpp`
varies worker count for the reduce; nothing does it for a full graph replay, and the engine's own
§15.5 requirement (byte-equal replay *with the runtime's worker count varied*) has no test on
either side (engine grep for `worker_count` in tests: nothing). The fix, runtime-side so every
consumer inherits it: a determinism conformance suite that runs a physics-shaped graph (late-bound
sizes, reduce, host nodes) for K steps at worker counts {1, 2, max}, rebalancer off, and asserts
byte equality of every output buffer. BB-2's `worker_count` knob is what makes this expressible.

**Proof.** The conformance suite exists and is red if `/fp:fast` is restored (verified by
temporarily restoring it); a `require_fp64` runtime on a POCL lane still constructs (POCL reports
fp64); the flags a consumer inherits from the exported target are exactly:
`-fsycl`, include dirs, `SR_DETERMINISTIC_FP`-derived FP flags, and nothing else (asserted by a
consumer-side CMake test in BB-4).

---

## 8. BB-4: Consumer linkage & packaging

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

---

## 9. BB-5: The ship path: multi-vendor, AOT, cache, zero-device

The deployment reality check was blunt: today, no player machine runs a game built on this
runtime; NVIDIA needs a CUDA-toolkit build machine and eats a PTX JIT stall; AMD needs a
separate exact-ISA build; no-GPU machines **throw in `Runtime::create()`** unless an OpenCL CPU
runtime happens to be installed; and Windows builds are hardwired to `SR_GPU_BACKEND=cpu` by the
CLI. The saving grace, also from the survey: the *runtime* half is already vendor-agnostic
(discovery enumerates everything, queues route by data) and the CMake accumulator
(`sushiruntime_gpu_add_clang_triple` → one joined `-fsycl-targets=a,b,c`) already supports lists. The
single-vendor constraint lives in ~20 lines of exclusive dispatch in `DetectGpu.cmake`.

**BB-5a: Fat binary.** `SR_GPU_BACKEND` accepts a semicolon list (`cuda;rocm;cpu`); `DetectGpu`
dispatch becomes additive over the list; each vendor module keeps owning its triple + arch flags.
`SR_CUDA_ARCH`/`SR_AMD_ARCH` become *lists* (multi-arch offload), and the `nvidia-smi`-probed
default is demoted to a dev convenience that a `SR_SHIP=ON` configure refuses (a ship build must
name its targets, not inherit the build machine's card). `auto` keeps meaning "probe the dev
box"; ship lanes are explicit.

**BB-5b: Zero-device survivability.** `Runtime::try_create(...) → Result<Runtime, Error>` joins
`create()` (which keeps throwing: same dual-channel convention as `run`/`try_run`). A game boots
on a machine with no SYCL device by catching the empty-device case cleanly and falling back to
its own host path (the engine's host solver exists for exactly this shape). Follow-on, aligned
with substrate WP-7 (native CPU execution path): a built-in native host device so the answer is
"the CPU always exists" rather than "install an OpenCL ICD"; WP-7 owns that; BB-5b only
guarantees the failure is clean and queryable today.

**BB-5c: First-launch JIT.** Two layers: (i) enable and manage the persistent cache the SYCL
runtimes already have; `SYCL_CACHE_PERSISTENT` plumbing with a game-writable cache dir exposed
as `RuntimeConfig::kernel_cache_dir`, so the stall is first-launch-only; (ii) AOT by default in
ship lanes: `spir64_gen` device lists for Intel, `sm_*`/`gfx*` lists from BB-5a, and the JIT
path kept as the dev-iteration default. The measured failure ("cold dispatch alone overran a
50 ms budget") becomes a tracked number: a cold-start benchmark in the perf suite, budget stated.

**BB-5d: Redist.** An `install(COMPONENT redist)` that gathers what a shipped binary needs
beside `libsushiruntime`: the SYCL runtime libs, `ur_loader` + the adapters for the vendors built,
and hwloc; plus `docs/SHIPPING.md` naming what the target machine must already have (GPU driver)
versus what travels with the game. `sr package` in the CLI wraps it. The `env.py` disclaimers
("does not replace deployment for a shipped build") finally get their counterpart.

**BB-5e: Windows GPU lane.** Remove the CLI's hardwired `SR_GPU_BACKEND=cpu` on Windows when
the configured toolchain can target nvptx there; where it genuinely cannot, the constraint gets
said out loud in configure output instead of silently downgrading.

**Proof.** One binary built `cuda;cpu` runs the functional suite on both an NVIDIA lane and the
POCL lane with no rebuild; `try_create` returns (not throws) on a lane with `SYCL_DEVICE_FILTER`
emptied; cold-start time with warm cache ≤ 10% of first-run; the redist component's file list is
asserted against a manifest so a new dependency cannot sneak in unpackaged.

---

## 10. BB-6: Data-plane maturation

The call-site survey's category B: things the engine built that the runtime should own. Ordered
by leverage.

1. **Batched transfer.** The engine hand-coalesces every host↔device movement (dirty-span
   staging, per-colour band scatter/gather: `runtime_graph_builder.hpp:966-991, 1132-1155`)
   because `read_range`/`write_range` are one synchronous `memcpy(...).wait()` each. Add
   `Buffer::write_ranges(span<const RangeWrite>)` / `read_ranges(...)` (one submit, many
   ranges, one wait) and graph-resident `add_upload`/`add_download` nodes so boundary transfers
   can live *inside* the composition and overlap with compute. The engine's staging layer then
   shrinks to bookkeeping (its dirty tracking is domain knowledge and stays).
2. **Growable buffers, honestly scoped.** In-place resize is impossible by construction (nodes
   capture raw pointers) and the fail-fast overflow contract is *correct*: keep both. What the
   runtime can give is the blessed reallocation path the animation evaluator hand-rolls today
   (reallocate → rebuild → recompile on every `bind_clip`): `Buffer::rebind(new_count)`
   returning a fresh allocation plus a graph-side invalidation that turns the next `run()` into
   a rebuild instead of a use-after-free. One primitive, two current call sites (animation clips,
   any future streaming table), and the failure mode changes from "silent stale pointer" to
   "explicit recompile".
3. **Report-free and reusable runs**: folded into BB-2 (the `run(RunReport&)` overloads); listed
   here because it is data-plane hygiene as much as RT hygiene.
4. **The pool decision: DONE 2026-08-18, by deletion (commit `6e4b87b`).** The choice was
   between routing the fluent handles through the allocator and demoting the pool. Measurement
   settled it: instrumenting every USM entry point showed allocation happens once per
   `Buffer`/`State` the caller constructs, at setup, and never during a run; 1000 steps, a
   reduction, and 32 regions streaming in and out all allocate nothing. Pooling amortises
   high-frequency small allocation and there is none, and the pool's 4 KB ceiling sat below the
   buffers the runtime actually allocates, so routing through it was never viable. Deleted rather
   than demoted, since demoting hides dead code instead of removing it. `Memory::USMRegistry` now
   owns pointer ownership, which is the part that had a real invariant. Design in
   `docs/archive/superpowers/specs/2026-08-18-usm-allocation-seam-design.md`.
5. **Deterministic device sort.** The engine's contact ordering rule ("sorted by
   `(body_a, body_b, feature_id)` before solving; broadphase order must never reach the solver")
   currently runs as a host sort and will otherwise be hand-built in the graph like the
   reduction was. Same argument as R2, same home: a fixed-order `add_sort(keys, values, n)`
   (bitonic or radix with layout-only ordering), WP-4's second item in all but name. Schedule
   behind BB-0 adoption: it is the next "delete two engine nodes" in waiting.
6. **`State<T>`: adopt or demote.** Zero engine references. Either the ECS's double-buffered
   columns adopt it (unlikely: the engine built chunk-level buffering with different semantics)
   or it is demoted from the front page of the API to a convenience wrapper before the freeze.
   An API frozen at v1 should not lead with a type its only real consumer never uses.

---

## 11. BB-7: Async depth

`run_async()` is one step, one in flight, and `~RunHandle` blocks: deliberately minimal (R4's
scope). What the head will ask for next, in order of real demand:

1. **Deadline-aware waiting.** `RunHandle::try_wait()` exists (non-blocking poll);
   `wait_for(duration)` / `wait_until(time_point)` complete the set so a frame loop can spend
   its budget elsewhere and make a rollback decision at the deadline instead of parking on a
   condvar. Small: the completion latch is already a mutex+condvar.
2. **Pipelined steps.** Allow step N+1 to be *submitted* while N drains, bounded depth 2: the
   double-buffered plan problem (in-degree re-arming under a live DAG is the documented hazard,
   so this is plan cloning or per-step in-degree arrays, not a flag flip). The payoff is hiding
   the submit latency of small graphs, and it is what makes the engine's
   sim-tick/render-extract overlap complete rather than half (BB-1b gives the render side; this
   gives the sim side).
3. **RT-shaped submit.** Audio's indictment is specific: *"per-step submit takes a mutex and may
   allocate."* After BB-2's report reuse and the shadow-counter fast path, the remaining hot-path
   work is bounded; measure whether the single `pending_mutex` acquire per step is actually a
   problem at audio's k-block cadence before designing around it. If it is, a wait-free submission
   ring for pre-armed plans is the shape, but only the measurement can justify it. (The
   real-time *mix* stays off the runtime regardless, that decision is closed and correct.)

BB-7 is deliberately after BB-1/BB-2: overlap without interop just moves the stall, and
pipelining under a sole-tenant scheduler overlaps with the wrong threads.

---

## 12. BB-8: Platform posture

The engine's cross-platform plan calls this the largest undocumented gap: *"SushiRuntime's SYCL
toolchains have zero path to macOS, iOS, Android, or PlayStation… not even listed as a
non-goal."* The engine has already chosen its own answer (an `Exec` seam; the runtime's blast
radius collapses to ~3 files off-desktop; GPU compute there becomes the RHI's compute path). The
runtime's obligations are therefore modest and mostly *declarative*:

1. **State the support matrix in `docs/`**: desktop Linux + Windows, the three toolchains, CPU
   fallback; everything else explicitly out of scope, served by the engine's seam. One page, and
   the sharpest finding in the whole cross-platform plan is answered.
2. **State the JIT/AOT posture** per platform alongside it (BB-5c's work makes the desktop story
   true; AOT-mandatory platforms are out of scope by the same page).
3. **Take the two timeboxed spikes** the engine's plan names (RUNTIME-PORT7): AdaptiveCpp
   `omp.library-only` under AppleClang: does the *host* half of the runtime even compile?, and
   a dependency audit (hwloc, TCP transport, thread affinity) for mobile viability. Outcomes are
   recorded, whatever they are; they change the *engine's* plan, not this one.
4. **Do not** build a Metal or Vulkan-compute backend. The engine explicitly rejected both; the
   runtime's identity is SYCL, and pretending otherwise dilutes the one thing it is best at (P4).

---

## 13. What this plan deliberately does not do

Recorded so it is not re-proposed: the same discipline the atmosphere doc uses.

- **No render-resource workloads.** Cloud noise, LUT bakes, PSO-adjacent work stay in Vulkan
  compute. BB-1 moves simulation *output* toward the renderer, never the reverse (P4).
- **No cross-architecture determinism.** L3 ruled it out; the engine's conformance tolerance
  (`1e-9`, FMA codegen differences) is the accepted boundary. BB-3 hardens *same-binary,
  same-device* determinism only.
- **No DSP math, no BLAS.** FFT/convolution/filters belong to the engine and the future
  SushiBLAS; the runtime provides the data plane and the batch offload path. The audio descope
  stands.
- **No capability-flag hedging.** Per the owner's direction: the API is shaped to the sim
  profile and broken freely until the freeze; `supports()`-style surface is explicitly rejected.
- **No engine-side colouring or islanding in the runtime.** L8's split is working: the tracker
  orders, the domain colours. R3+R6 gave the mechanism; the policy stays above.
- **No distributed-layer work in the game path.** The master/worker layer stays optional,
  zero-cost when off, and untouched by this program except where BB-4's define hygiene brushes
  it.

---

## 14. Sequencing and dependencies

```
BB-0  merge & adopt          ──►  everything (it is the current-truth reset)
BB-2  co-tenancy      ┐
BB-3  determinism     ├── parallel, small, no cross-deps; land before or beside BB-1
BB-4  linkage         ┘
BB-1a interop import         ──►  BB-1b sync export ──► engine zero-copy milestones
BB-5a fat binary  ──► BB-5c AOT/cache ──► BB-5d redist   (BB-5b any time; BB-5e with 5a)
BB-6  continuous behind BB-0 (item 5 sort after adoption; item 4 decision before freeze)
BB-7  after BB-1 + BB-2 (overlap needs both halves)
BB-8  decision + docs now; spikes whenever a desktop lull allows
v1.0.0 freeze: after BB-4 + BB-3 land and BB-6 items 4/6 are decided — the freeze list in
docs/design/REMAINING_WORK.md gains exactly those entries.
```

The dependency worth stating twice: **v1.0.0 must not be tagged before BB-3 and BB-4.** A frozen
API whose build flags still leak `-march=native` to consumers, or that still cannot be consumed
out of tree, freezes the two things most worth fixing.

---

## 15. Proof obligations

Every package lands with the test that would fail without it, per house rule.

| Pkg | Test | Fails without the change |
|---|---|---|
| BB-0 | engine conformance vs tagged v0.2.0; substep counts bit-identical across reduction swap | engine still solving on hand-built reduce, or drift between the two reducers |
| BB-1a | `interop_demo`: kernel writes imported Vulkan buffer, contents verified, zero copies | no import entry point exists |
| BB-1b | Vulkan stub waits on exported semaphore, reads without host sync | only host-side `wait()` orders the two APIs |
| BB-2 | reserved-core test: no runtime thread on cores the config excludes; zero allocs in steady-state tick with `run(RunReport&)` | worker 0 pins to core 0; report allocates every run |
| BB-3 | graph-level byte-equality at worker counts {1,2,max}; red if `/fp:fast` restored | reassociation changes host folds between builds |
| BB-3 | `require_fp64` filters devices; empty-list error is precise | fp64-less device fails mid-kernel with a driver error |
| BB-4 | out-of-tree `find_package` consumer builds and runs on CPU lane; inherited-flags assertion | no Config package; consumer inherits `-Werror`/`-march` |
| BB-5a | one `cuda;cpu` binary passes suite on NVIDIA lane and POCL lane unmodified | one vendor per binary |
| BB-5b | `try_create` returns error (not throw) with device filter emptied | `Runtime::create()` throws; game cannot boot |
| BB-5c | cold-start ≤ stated budget with warm persistent cache | first-run JIT stall unbounded and unmeasured |
| BB-5d | redist component matches manifest exactly | shipped binary missing `ur_loader`/adapter/hwloc |
| BB-6.1 | N-range write is one submit (asserted via profiling counters) | N synchronous memcpys |
| BB-6.2 | `rebind` + next `run()` rebuilds; stale-pointer path impossible by construction | animation-style manual realloc + silent capture hazard |
| BB-7.1 | `wait_for` returns at deadline with step still running; later `wait` completes it | only blocking `wait()` exists |

---

*Companion documents: the request register lives in the engine
(`sushiengine/docs/slop/physics_system.md` §18; to be corrected under BB-0); the runtime-side
histories are `PHYSICS_SUBSTRATE_REQUIREMENTS.md` and
`docs/archive/slop/PHYSICS_SUBSTRATE_IMPLEMENTATION.md`; the
long-horizon substrate program is `SIMULATION_ENGINE_SUBSTRATE_PLAN.md`, whose WP-4 (reduction
(delivered), WP-5 (multi-device) unchanged, long horizon) and WP-7 (native CPU path: referenced
by BB-5b) this plan slots around.*
