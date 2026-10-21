# BB-1: Vulkan interop, the missing half

BB-1a is the `interop` module, shaped by SushiEngine's
`docs/design/SYCL_VULKAN_INTEROP.md`, which supersedes this page where the two differ. BB-1b is
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
