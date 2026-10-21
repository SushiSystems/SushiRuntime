# BB-8: Platform posture

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
