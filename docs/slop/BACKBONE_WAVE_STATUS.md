# Backbone refactor — wave 1 status

Where the BB-3 + BB-2 + BB-4 wave stands, what is verified, and what the next
session has to pick up. Companion to `ENGINE_BACKBONE_REFACTOR.md`, which is the
plan; this is the state.

Last updated: 2026-07-30.

---

## 1. What landed

Everything below is committed. The verification column is what it says — several
items are code-complete and compiled but their test run has not been seen green.

| Item | What | Verified |
|---|---|---|
| BB-3.1 | Perf/arch flags split out of the global interface into `cmake/Optimization.cmake`; policy applied per target, PRIVATE, via `sushiruntime_apply_build_policy()` | tests green |
| BB-3.2 | `/fp:fast` removed; `SR_DETERMINISTIC_FP` (default ON) + `sushiruntime_deterministic_fp`, linked PUBLIC | tests green |
| BB-3.3 | `HardwareFilterConfig::require_fp64`; empty-device-list failure is a precise `unsupported` error | tests green (normal build) |
| BB-2.1 | `RuntimeConfig` is a co-tenancy contract: `worker_count`, `first_core`, `pinning`; clamped-and-warned; `advanced().worker_pool()` reports as built | tests green |
| BB-2.2 | Rebalancer off by default (behaviour-breaking) | tests green |
| BB-2.3 | Caller-owned run reports: `run(report)`, `try_run(report)`, `wait(report)`, `try_wait(report)`, `RunReport::reset()` | compiles; test run not confirmed |
| BB-3.4 | `Integration_Determinism` conformance suite; contraction switch probed for the MSVC frontend | compiles; test run not confirmed |
| BB-2.4 | `TaskScheduler::disarm()`, untimed idle park, lost-wakeup fix, per-worker drain scratch | compiles; test run not confirmed |
| BB-2.5 | `Integration_SteadyStateAllocation` in its own binary | compiles; test run not confirmed |
| BB-4.1 | `install(EXPORT)` + `SushiRuntimeConfig.cmake` + version file; `SushiRuntime::SushiRuntime` alias | configures; install flow not exercised |
| BB-4.2 | `version.hpp` + `runtime_version()` + `version_matches()`, with a configure-time drift check | compiles |
| BB-4.3 | `docs/INTEGRATION.md` | — |
| BB-4.5 | `Core::Result<T,E>` declared permanent | — |
| BB-4.6 | `tests/package/` out-of-tree consumer + flag-hygiene assertion | **not run** — needs an install prefix |

Two findings worth carrying forward, both recorded in `CHANGELOG.md`:

- **BB-2.4 was bigger than the plan's item.** The armed flag was never cleared
  between runs, so idle workers never reached the dispatch loop's park at all —
  they sat in the steal backoff for the life of the runtime. The 10 ms timeout the
  plan named was separately covering a lost wakeup (`arm_for_execution` and `stop`
  published their flags outside `pending_mutex` while the wait predicate read them
  under it). Both fixed; the park is now untimed.
- **BB-4.4 is closed as "keep PUBLIC" — the plan's premise was wrong.** See item 4
  of §8 in `ENGINE_BACKBONE_REFACTOR.md`, which has been rewritten with the
  reasoning. Do not re-attempt it from the original premise.

---

## 2. Open: three ASan failures

`sr build --type asan && sr test --asan` fails three tests. **This is the first
thing the next session should resolve.**

```
148 - Integration_DeviceSelection.AnEmptyFilterFailsWithUnsupported
161 - Integration_AsyncStep.TaskExceptionSurfacesOnWaitAndLeavesTheGraphUsable
162 - Integration_AsyncStep.TryWaitReportsFailureAsAValue
```

All three fail with `SEH exception with code 0xc0000005` (access violation) — an
exception is thrown *correctly* (the expected error is logged) and the crash comes
during propagation.

### What is known

**The pattern is a bare `throw;` rethrow.** `Integration_ErrorIsolation`, which
also exercises a throwing kernel, passes — and it differs in exactly this: `run()`
goes through `drive()`, which *catches the exception and converts it to a value*.
The three failures all rethrow:

- `RunHandle::wait()` — `catch (...) { owner->discard_async_step(); throw; }`
- `probe_hardware` — an enclosing catch logs the failure and rethrows (the
  "Critical initialization failure" line in the output is that catch)

**None of the three has ever run under ASan before.** The ASan lane was made to
work in `f502d7d` (2026-07-28 14:22); `test_async_step.cpp` was added in `5536b02`
(2026-07-28 15:28), an hour later, and `test_device_selection.cpp` was added in
this wave. That points away from a regression, but the evidence is circumstantial.

### The two commands that settle it

Neither has been run yet. Do not change code before both have.

```powershell
# 1. A real stack. GoogleTest's SEH handler swallows the fault and prints an empty
#    trace; disabling it lets ASan report the actual access.
$env:GTEST_CATCH_EXCEPTIONS=0
.\build-asan\tests\sr_functional_tests.exe `
    --gtest_filter=Integration_DeviceSelection.AnEmptyFilterFailsWithUnsupported
```

```bash
# 2. The same three tests in the ordinary build. If they pass there, the fault is
#    ASan-lane-specific rather than a logic error.
sr build
sr test --suite functional
```

The two outcomes need opposite treatments, which is why guessing is not an option:
a real memory error gets fixed; an ASan/toolchain interaction with MSVC-ABI
rethrow gets documented and handled through `ASAN_OPTIONS` or a lane exclusion —
C++ semantics do not get bent to suit a sanitizer.

### Also unconfirmed

The normal-build (`sr build && sr test --suite functional`) result for this wave
has never been reported. BB-2.3, BB-3.4, BB-2.4 and BB-2.5 are compiled but their
tests are unconfirmed in *any* lane.

---

## 3. Build-lane fixes made along the way

Three things broke the Windows ASan lane and are fixed; noting them because each
was a latent trap rather than anything this wave introduced.

1. **`stl_asan.lib` not found.** MSVC STL autolinks it for container annotations
   and it ships with the Visual Studio "C++ AddressSanitizer" component, which is
   not installed here. The CLI disabled the annotations per container
   (`_DISABLE_STRING_ANNOTATION`, `_DISABLE_VECTOR_ANNOTATION`) and MSVC 14.51
   added a third (`optional`), so the autolink kept firing. Now uses the umbrella
   `_DISABLE_STL_ANNOTATION`, which covers whatever is added next.
2. **The CLI's configure stamp did not track its own flag constants.** Editing
   `ASAN_FLAGS_WINDOWS` changed what a fresh tree would get while an existing tree
   kept building with the old flags — a fix that looks like it did not work.
   `_flags_fingerprint()` now folds them into the stamp.
3. **Parallel post-build DLL copies raced.** Every example and test target copied
   `sushiruntime.dll` into a shared output directory from its own POST_BUILD step;
   a clean parallel build links several at once and Windows answers the loser with
   a sharing violation. Replaced by one staging target per directory that the
   binaries depend on.

---

## 4. Next

1. Resolve §2. Nothing else should start before the wave is green.
2. **BB-0 engine-side adoption** in `C:\Projects\sushiengine` — the R1–R7 seams,
   and the engine's `Runtime.cmake` learning to consume the runtime as a package
   rather than by `add_subdirectory` of a sibling checkout (BB-4 makes that
   possible for the first time).
3. Exercise `tests/package/` once against a real install prefix.

The engine build-name rename to a `sushiengine_` prefix was dropped by the owner.
