# BB-5: The ship path, multi-vendor, AOT, cache, zero-device

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
