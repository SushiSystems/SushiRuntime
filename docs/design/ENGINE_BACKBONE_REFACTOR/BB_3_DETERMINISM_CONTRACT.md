# BB-3: Determinism contract, flags, fp64, conformance

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
